package com.nikhil.yt.utils

import java.text.Normalizer
import java.util.Locale

/** Require both title and artist evidence before replacing a Spotify track. */
internal object SpotifyTrackMatcher {
    private fun normalize(value: String): String = Normalizer.normalize(value, Normalizer.Form.NFKD)
        .lowercase(Locale.ROOT)
        .replace(Regex("\\p{M}+|ـ"), "")
        .replace(Regex("[^\\p{L}\\p{N}]+"), " ")
        .trim()

    private fun normalizedTitle(value: String): String = normalize(
        value.replace(
            Regex(
                """[\[(](?:(?:feat\.?|ft\.?)\s+[^\])]+|official (?:audio|video)|(?:\d{4}\s+)?remaster(?:ed)?(?:\s+\d{4})?)[\])]""",
                RegexOption.IGNORE_CASE,
            ),
            "",
        ).replace(Regex("\\s+-\\s+(?:\\d{4}\\s+)?remaster(?:ed)?(?:\\s+\\d{4})?$", RegexOption.IGNORE_CASE), ""),
    )

    private fun similarity(first: String, second: String): Double {
        val a = normalize(first).split(' ').filter { it.isNotBlank() }.toSet()
        val b = normalize(second).split(' ').filter { it.isNotBlank() }.toSet()
        if (a.isEmpty() || b.isEmpty()) return 0.0
        return 2.0 * a.intersect(b).size / (a.size + b.size)
    }

    fun <T> bestMatch(
        title: String,
        artist: String?,
        candidates: List<T>,
        titleOf: (T) -> String,
        artists: (T) -> List<String>,
    ): T? {
        if (artist.isNullOrBlank()) return null
        val source = normalizedTitle(title)
        if (source.isBlank()) return null
        val sourceArtists = artist.split(Regex("\\s*(?:,|&|;| feat\\.? | ft\\.? )\\s*", RegexOption.IGNORE_CASE))
            .filter { it.isNotBlank() }
        return candidates.mapNotNull { candidate ->
            if (source != normalizedTitle(titleOf(candidate))) return@mapNotNull null
            val candidateArtists = artists(candidate)
            val artistScore = maxOf(
                similarity(artist, candidateArtists.joinToString(" ")),
                sourceArtists.maxOfOrNull { sourceArtist ->
                    candidateArtists.maxOfOrNull { similarity(sourceArtist, it) } ?: 0.0
                } ?: 0.0,
            )
            if (artistScore < 0.7) null else candidate to artistScore
        }.maxByOrNull { it.second }?.first
    }
}
