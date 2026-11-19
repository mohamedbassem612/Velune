package com.nikhil.yt.playback

import com.nikhil.yt.models.MediaMetadata

/** Metadata reconstructed from the local file, independent of account sync or the network. */
internal object DownloadedSongRecovery {
    data class Recovered(val metadata: MediaMetadata, val albumName: String?)

    fun recover(
        id: String,
        title: String?,
        artist: String?,
        album: String?,
        durationMs: String?,
        displayName: String?,
    ): Recovered {
        require(id.isNotBlank())
        val fileTitle = displayName?.substringBeforeLast('.')?.removeSuffix("-$id")?.trim()
        val songTitle = title?.trim()?.takeIf { it.isNotEmpty() }
            ?: fileTitle?.takeIf { it.isNotEmpty() } ?: id
        return Recovered(
            MediaMetadata(
                id = id, title = songTitle,
                artists = artist?.trim()?.takeIf { it.isNotEmpty() }
                    ?.let { listOf(MediaMetadata.Artist(id = null, name = it)) }.orEmpty(),
                duration = durationMs?.toLongOrNull()?.takeIf { it > 0 }
                    ?.div(1000)?.coerceAtMost(Int.MAX_VALUE.toLong())?.toInt() ?: -1,
                thumbnailUrl = "https://i.ytimg.com/vi/$id/hqdefault.jpg",
            ),
            album?.trim()?.takeIf { it.isNotEmpty() },
        )
    }

    fun missingIds(downloadedIds: Set<String>, storedIds: Set<String>): Set<String> = downloadedIds - storedIds
}
