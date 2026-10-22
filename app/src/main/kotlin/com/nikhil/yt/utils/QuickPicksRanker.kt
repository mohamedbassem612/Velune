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
 * - songs played a moment ago are pushed down so the list is not a repeat
 * - songs that were played to death are pushed down a little
 *
 * A small random part keeps the list different on each refresh, and no more
 * than two songs from the same artist can appear.
 */
object QuickPicksRanker {
    private const val MAX_PER_ARTIST = 2
    private const val WORN_OUT_PLAY_TIME_MS = 2 * 60 * 60 * 1000L

    fun rank(
        candidates: List<Song>,
        artistAffinity: Map<String, Float>,
        recentSongIds: Set<String>,
        limit: Int,
        random: Random = Random.Default,
    ): List<Song> {
        val unique = candidates.distinctBy { it.id }
        if (unique.isEmpty()) return emptyList()
        val total = unique.size.toFloat()

        val scored = unique.mapIndexed { index, song ->
            val relatedness = 1f - index / total
            val affinity = song.artists.maxOfOrNull { artistAffinity[it.id] ?: 0f } ?: 0f

            var score = 0.45f * relatedness + 0.45f * affinity
            if (song.song.liked) score += 0.10f
            if (song.id in recentSongIds) score -= 0.60f
            if (song.song.totalPlayTime > WORN_OUT_PLAY_TIME_MS) score -= 0.10f
            score += random.nextFloat() * 0.15f

            song to score
        }.sortedByDescending { it.second }

        val perArtist = HashMap<String, Int>()
        val result = ArrayList<Song>(limit)
        for ((song, _) in scored) {
            val key = song.artists.firstOrNull()?.id ?: song.id
            val used = perArtist[key] ?: 0
            if (used >= MAX_PER_ARTIST) continue
            perArtist[key] = used + 1
            result += song
            if (result.size >= limit) break
        }

        // If the artist limit left the list short, fill it with what remains.
        if (result.size < limit) {
            val chosen = result.map { it.id }.toHashSet()
            for ((song, _) in scored) {
                if (song.id in chosen) continue
                result += song
                if (result.size >= limit) break
            }
        }
        return result
    }
}
