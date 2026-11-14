package com.nikhil.yt.ui.tv

import com.nikhil.yt.models.MediaMetadata
import java.util.Locale

/** A small, deterministic selection policy, independent of Compose and playback. */
internal object TvHomeSelector {
    data class Shelves(
        val personal: List<MediaMetadata>,
        val radio: List<MediaMetadata>,
    )

    fun select(
        liked: List<MediaMetadata>,
        mostPlayed: List<MediaMetadata>,
        recent: List<MediaMetadata>,
        discovery: List<MediaMetadata>,
    ): Shelves {
        val candidates = interleave(liked, mostPlayed, discovery, recent)
        // Reserve at most a third of a small library for radio so personal picks stay useful.
        val radioLimit = minOf(12, candidates.size / 3)
        val radio = mutableListOf<MediaMetadata>()
        val radioArtists = mutableSetOf<String>()
        for (song in interleave(mostPlayed, recent, liked, discovery)) {
            if (radio.size >= radioLimit) break
            val artists = artistKeys(song)
            // Unknown artists do not provide a useful artist-based radio seed.
            if (artists.isEmpty() || artists.any { it in radioArtists }) continue
            radio += song
            radioArtists += artists
        }

        val radioIds = radio.mapTo(mutableSetOf()) { it.id }
        val available = candidates.filterNot { it.id in radioIds }
        val personal = mutableListOf<MediaMetadata>()
        val artistCounts = mutableMapOf<String, Int>()
        for (song in available) {
            if (personal.size >= 24) break
            val artists = artistKeys(song)
            if (artists.any { (artistCounts[it] ?: 0) >= 2 }) continue
            personal += song
            artists.forEach { artistCounts[it] = (artistCounts[it] ?: 0) + 1 }
        }
        // Prefer diversity, but allow a small/single-artist library to fill the shelf.
        val selectedIds = personal.mapTo(mutableSetOf()) { it.id }
        personal += available.filterNot { it.id in selectedIds }.take(24 - personal.size)
        return Shelves(personal, radio)
    }

    private fun artistKeys(song: MediaMetadata): Set<String> = song.artists
        .map { it.name.trim().lowercase(Locale.ROOT).replace(Regex("\\s+"), " ") }
        .filter { it.isNotEmpty() }
        .toSet()

    private fun interleave(vararg sources: List<MediaMetadata>): List<MediaMetadata> = buildList {
        val seen = mutableSetOf<String>()
        repeat(sources.maxOfOrNull { it.size } ?: 0) { index ->
            sources.forEach { source ->
                source.getOrNull(index)?.let { if (seen.add(it.id)) add(it) }
            }
        }
    }
}
