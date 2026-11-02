/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.utils

import com.nikhil.yt.db.entities.Song
import kotlin.random.Random

/**
 * Orders quick picks candidates by how well they fit the listener's taste.
 *
 * Signals, all taken from the local database so the home screen stays fast:
 * - how related the song is to what was played lately (candidate order, best first)
 * - how much the listener plays and follows the song's artists
 * - liked songs get a small bonus
 * - songs played lately are pushed down, the more recent the more (graded)
 * - artists that were played a lot lately are pushed down a little (graded)
 * - songs that were played to death are pushed down a little
 *
 * A small random part breaks ties. Artist limits count every artist of a song
 * (featured artists too) and are only relaxed step by step when the list would be short.
 */
object QuickPicksRanker {
    const val MAX_PER_ARTIST = 2
    private const val MAX_RELAXED_PER_ARTIST = 4
    private const val WORN_OUT_PLAY_TIME_MS = 2 * 60 * 60 * 1000L
    private const val RECENT_SONG_PENALTY = 0.60f
    private const val RECENT_ARTIST_PENALTY_STEP = 0.05f
    private const val RECENT_ARTIST_PENALTY_MAX_COUNT = 4

    /** Same key for the same artist name, whatever the letter case or spaces around it. */
    fun normalizeArtist(name: String): String = name.trim().lowercase()

    /**
     * @param recentSongIds songs played lately, most recent first
     * @param recentArtistCounts how many of the recent songs each artist id has
     */
    fun rank(
        candidates: List<Song>,
        artistAffinity: Map<String, Float>,
        recentSongIds: List<String>,
        recentArtistCounts: Map<String, Int> = emptyMap(),
        limit: Int,
        random: Random = Random.Default,
    ): List<Song> {
        val unique = candidates.distinctBy { it.id }
        if (unique.isEmpty()) return emptyList()
        val total = unique.size.toFloat()

        val recentRank = HashMap<String, Int>()
        recentSongIds.forEach { recentRank.putIfAbsent(it, recentRank.size) }
        val recentCount = recentRank.size.coerceAtLeast(1).toFloat()

        val scored = unique.mapIndexed { index, song ->
            val relatedness = 1f - index / total
            val affinity = song.artists.maxOfOrNull { artistAffinity[it.id] ?: 0f } ?: 0f

            var score = 0.45f * relatedness + 0.45f * affinity
            if (song.song.liked) score += 0.10f
            recentRank[song.id]?.let { score -= RECENT_SONG_PENALTY * (1f - it / recentCount) }
            val repeatedArtist = song.artists.maxOfOrNull { recentArtistCounts[it.id] ?: 0 } ?: 0
            score -= RECENT_ARTIST_PENALTY_STEP * minOf(repeatedArtist, RECENT_ARTIST_PENALTY_MAX_COUNT)
            if (song.song.totalPlayTime > WORN_OUT_PLAY_TIME_MS) score -= 0.10f
            score += random.nextFloat() * 0.05f

            song to score
        }.sortedByDescending { it.second }

        return selectDiverse(
            sorted = scored.map { it.first },
            limit = limit,
            idOf = { it.id },
            artistKeys = { song -> song.artists.map { it.id } },
        )
    }

    /**
     * Takes items in the given order with at most [MAX_PER_ARTIST] per artist. Every artist of
     * an item counts. If that leaves the list short, the limit goes up one step at a time
     * (3, then 4) and never further, so the list may end up shorter than [limit].
     */
    fun <T> selectDiverse(
        sorted: List<T>,
        limit: Int,
        idOf: (T) -> String,
        artistKeys: (T) -> List<String>,
    ): List<T> {
        val result = ArrayList<T>(limit)
        val chosen = HashSet<String>()
        val counts = HashMap<String, Int>()

        for (allowed in MAX_PER_ARTIST..MAX_RELAXED_PER_ARTIST) {
            for (item in sorted) {
                if (result.size >= limit) return result
                val id = idOf(item)
                if (id in chosen) continue

                val keys = artistKeys(item).distinct().ifEmpty { listOf(id) }
                if (keys.any { (counts[it] ?: 0) >= allowed }) continue

                result += item
                chosen += id
                keys.forEach { counts[it] = (counts[it] ?: 0) + 1 }
            }
        }
        return result
    }
}
