package com.nikhil.yt.utils

import java.text.Normalizer
import java.util.Locale
import kotlin.math.exp
import kotlin.math.ln
import kotlin.math.pow
import kotlin.math.sqrt

/** Pure, deterministic recommendation policy. Network/Room models stay in the adapter. */
object QuickPicksRecommender {
    private const val DAY = 86_400_000L

    data class Track(
        val id: String,
        val title: String,
        val artists: Set<String>,
        val durationSeconds: Int = 0,
        val albumId: String? = null,
    )

    data class Listen(val track: Track, val at: Long, val playedMs: Long)
    data class Skip(val count: Int, val at: Long)
    data class Seed(val track: Track, val weight: Double)
    data class Candidate(
        val track: Track,
        // One vote per independent seed, already weighted by seed quality and result position.
        val related: Map<String, Double> = emptyMap(),
        val trend: Double = 0.0,
    )

    data class Profile(
        val artistAffinity: Map<String, Double>,
        val songStrength: Map<String, Double>,
        val lastHeard: Map<String, Long>,
        val recentIds: Set<String>,
        val likedIds: Set<String>,
        val skipPenalty: Map<String, Double>,
        val seeds: List<Seed>,
        val hasTaste: Boolean,
    )

    fun artistKey(name: String): String = Normalizer.normalize(name, Normalizer.Form.NFKC)
        .lowercase(Locale.ROOT).trim().replace(Regex("\\s+"), " ")

    private fun decay(age: Long, halfLifeDays: Double): Double =
        0.5.pow(age.coerceAtLeast(0).toDouble() / (DAY * halfLifeDays))

    fun profile(
        listens: List<Listen>,
        likes: List<Track>,
        skips: Map<String, Skip>,
        now: Long,
        lastListenOnly: Boolean = false,
        excludedSeedIds: Set<String> = emptySet(),
    ): Profile {
        val history = listens.filter { it.playedMs > 0 && now - it.at <= 90 * DAY }
            .sortedByDescending { it.at }
        val bySong = history.groupBy { it.track.id }
        val penalties = skips.mapValues { (id, skip) ->
            // A single next-button press is weak evidence; repeated recent skips matter much more.
            val successful = bySong[id]
                .orEmpty().count { quality(it) >= 0.7 }
            (skip.count.coerceAtLeast(0).toDouble() / (skip.count.coerceAtLeast(0) + successful + 2)) *
                decay(now - skip.at, 14.0)
        }
        val longTerm = mutableMapOf<String, Double>()
        val shortTerm = mutableMapOf<String, Double>()
        val strengths = mutableMapOf<String, Double>()
        bySong.forEach { (id, plays) ->
            // Cap repeated listening per day so one loop or a long video cannot own the profile.
            val daily = plays.groupBy { it.at / DAY }.values
            val longScore = daily.sumOf { day ->
                day.sumOf { quality(it) }.coerceAtMost(3.0) * decay(now - day.maxOf { it.at }, 30.0)
            }
            val shortScore = daily.sumOf { day ->
                day.sumOf { quality(it) }.coerceAtMost(2.0) * decay(now - day.maxOf { it.at }, 3.0)
            }
            val acceptance = 1.0 - (penalties[id] ?: 0.0)
            strengths[id] = ln(1 + longScore) * acceptance
            val artists = plays.first().track.artists.filter { it.isNotBlank() }
            artists.forEach { artist ->
                val share = sqrt(artists.size.toDouble())
                longTerm[artist] = (longTerm[artist] ?: 0.0) + ln(1 + longScore) * acceptance / share
                shortTerm[artist] = (shortTerm[artist] ?: 0.0) + ln(1 + shortScore) * acceptance / share
            }
        }
        fun normalized(values: Map<String, Double>): Map<String, Double> {
            val max = values.values.maxOrNull()?.coerceAtLeast(0.001) ?: 1.0
            return values.mapValues { it.value / max }
        }
        val longNorm = normalized(longTerm)
        val shortNorm = normalized(shortTerm)
        val likeCounts = likes.distinctBy { it.id }.flatMap { it.artists }.filter { it.isNotBlank() }
            .groupingBy { it }.eachCount().mapValues { ln(1.0 + it.value) }
        val likeNorm = normalized(likeCounts)
        val listeningConfidence = (bySong.size / 12.0).coerceIn(0.0, 1.0)
        val likeWeight = 0.65 - 0.50 * listeningConfidence
        val affinity = (longNorm.keys + shortNorm.keys + likeNorm.keys).associateWith { artist ->
            ((1 - likeWeight) * (0.60 * (longNorm[artist] ?: 0.0) + 0.40 * (shortNorm[artist] ?: 0.0)) +
                likeWeight * (likeNorm[artist] ?: 0.0)).coerceIn(0.0, 1.0)
        }
        val likedIds = likes.map { it.id }.toSet()
        val tracks = (history.map { it.track } + likes).distinctBy { it.id }.filterNot { it.id in excludedSeedIds }
        val latest = history.firstOrNull {
            it.track.id !in excludedSeedIds && quality(it) >= 0.35 && (penalties[it.track.id] ?: 0.0) < 0.5
        }
        val seedPool = tracks.map { track ->
            val recent = bySong[track.id]?.firstOrNull()
            val strength = strengths[track.id] ?: 0.0
            val recentBoost = recent?.let { quality(it) * decay(now - it.at, 3.0) } ?: 0.0
            val likedBoost = if (track.id in likedIds) 0.20 + 0.30 * (1 - listeningConfidence) else 0.0
            Seed(track, (0.60 * (1 - exp(-strength)) + 0.30 * recentBoost + likedBoost)
                .coerceAtMost(1.0) * (1 - (penalties[track.id] ?: 0.0)))
        }.filter { it.weight > 0.05 }.sortedWith(compareByDescending<Seed> { it.weight }.thenBy { it.track.id })
        val seedCounts = mutableMapOf<String, Int>()
        val seeds = if (lastListenOnly && latest != null) listOf(Seed(latest.track, 1.0)) else {
            seedPool.filter { seed ->
                if (seed.track.artists.any { (seedCounts[it] ?: 0) >= 2 }) false else {
                    seed.track.artists.forEach { seedCounts[it] = (seedCounts[it] ?: 0) + 1 }
                    true
                }
            }.take(6)
        }
        return Profile(
            affinity, strengths, bySong.mapValues { it.value.first().at },
            history.map { it.track.id }.distinct().take(30).toSet(), likedIds,
            penalties, seeds, seeds.isNotEmpty() || likeNorm.isNotEmpty() || strengths.values.any { it >= 0.05 },
        )
    }

    private fun quality(listen: Listen): Double {
        val duration = listen.track.durationSeconds.takeIf { it > 0 }?.times(1000L) ?: 180_000L
        val completion = (listen.playedMs.toDouble() / duration).coerceIn(0.0, 1.0)
        return completion * completion
    }

    /** Scores are bounded; popularity can only boost an already relevant track. */
    fun rank(
        candidates: List<Candidate>,
        profile: Profile,
        now: Long,
        shownIds: Set<String> = emptySet(),
        discovery: Boolean = true,
        limit: Int = 20,
    ): List<Track> {
        if (limit <= 0) return emptyList()
        val merged = candidates.groupBy { it.track.id }.map { (_, duplicates) ->
            Candidate(duplicates.first().track,
                duplicates.flatMap { it.related.entries }.groupBy { it.key }
                    .mapValues { (_, votes) -> votes.maxOf { it.value } },
                duplicates.maxOf { it.trend }.coerceIn(0.0, 1.0))
        }
        val scored = merged.mapNotNull { candidate ->
            val track = candidate.track
            if (discovery && (track.id in profile.likedIds || track.id in profile.recentIds)) return@mapNotNull null
            val skip = profile.skipPenalty[track.id] ?: 0.0
            if (skip >= 0.5) return@mapNotNull null
            val affinity = track.artists.maxOfOrNull { profile.artistAffinity[it] ?: 0.0 } ?: 0.0
            val votes = candidate.related.values.map { it.coerceIn(0.0, 1.0) }.sortedDescending()
            val relatedness = votes.firstOrNull() ?: 0.0
            val consensus = 1 - exp(-votes.drop(1).sum())
            val fit = maxOf(affinity, relatedness)
            if (profile.hasTaste && fit < 0.16) return@mapNotNull null
            val heard = profile.lastHeard[track.id]?.let { 0.24 * decay(now - it, 7.0) } ?: 0.0
            val exposure = if (track.id in shownIds) 0.07 else 0.0
            val novelty = if (track.id !in profile.lastHeard) 0.04 * fit else 0.0
            val score = if (!profile.hasTaste) candidate.trend - skip else {
                0.52 * relatedness + 0.28 * affinity + 0.12 * consensus +
                    0.16 * candidate.trend * fit + novelty - heard - exposure - 0.65 * skip
            }
            track to score
        }.sortedWith(compareByDescending<Pair<Track, Double>> { it.second }.thenBy { it.first.id }).toMutableList()
        val result = mutableListOf<Track>()
        val artistCounts = mutableMapOf<String, Int>()
        val recordings = mutableSetOf<String>()
        while (result.size < limit && scored.isNotEmpty()) {
            val next = scored.filter { (track, _) ->
                track.artists.none { (artistCounts[it] ?: 0) >= 3 } && recordingKey(track) !in recordings
            }.maxByOrNull { (track, score) ->
                val overlap = result.maxOfOrNull { selected ->
                    val union = track.artists + selected.artists
                    val artistSimilarity = if (union.isEmpty()) 0.0 else
                        track.artists.intersect(selected.artists).size.toDouble() / union.size
                    maxOf(artistSimilarity, if (track.albumId != null && track.albumId == selected.albumId) 0.7 else 0.0)
                } ?: 0.0
                score - 0.16 * overlap
            } ?: break
            scored.remove(next)
            result += next.first
            recordings += recordingKey(next.first)
            next.first.artists.forEach { artistCounts[it] = (artistCounts[it] ?: 0) + 1 }
        }
        return result
    }

    private fun recordingKey(track: Track): String {
        val title = artistKey(track.title).replace(Regex("\\s*[\\[(](official (music )?(video|audio)|lyrics?|audio)[\\])]"), "").trim()
        // Unknown-artist tracks cannot safely be deduplicated by title alone.
        return if (track.artists.isEmpty()) track.id else title + "|" + track.artists.sorted().joinToString("|")
    }

    fun isEarlySkip(positionMs: Long, durationMs: Long): Boolean =
        durationMs > 0 && positionMs >= 1_000 && positionMs < minOf(60_000, durationMs * 35 / 100)
}
