package com.nikhil.yt.models

/** Playlist membership is separate from song identity; reuse stored fields without losing its entry ID. */
internal fun MediaMetadata.withStoredSong(stored: MediaMetadata): MediaMetadata {
    require(id == stored.id)
    return copy(
        title = stored.title.takeIf { it.isNotBlank() } ?: title,
        artists = stored.artists.takeIf { it.isNotEmpty() } ?: artists,
        duration = stored.duration.takeIf { it > 0 } ?: duration,
        thumbnailUrl = stored.thumbnailUrl ?: thumbnailUrl,
        album = stored.album ?: album,
        explicit = stored.explicit || explicit,
    )
}
