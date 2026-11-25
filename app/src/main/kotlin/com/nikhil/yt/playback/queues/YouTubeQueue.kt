/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */



package com.nikhil.yt.playback.queues

import androidx.media3.common.MediaItem
import com.nikhil.yt.innertube.YouTube
import com.nikhil.yt.innertube.models.WatchEndpoint
import com.nikhil.yt.extensions.rankAutomix
import com.nikhil.yt.utils.AutomixBlocklist
import com.nikhil.yt.extensions.toMediaItem
import com.nikhil.yt.models.MediaMetadata
import kotlinx.coroutines.Dispatchers.IO
import kotlinx.coroutines.withContext

class YouTubeQueue(
    private var endpoint: WatchEndpoint,
    override val preloadItem: MediaMetadata? = null,
) : Queue {
    private val seedId = endpoint.videoId?.takeIf { endpoint.playlistId.isNullOrBlank() }
    val isTrackRadio get() = seedId != null
    private var continuation: String? = null

    override suspend fun getInitialStatus(): Queue.Status = getInitialStatus { _ -> AutomixBlocklist() }

    internal suspend fun getInitialStatus(blocklist: suspend (List<MediaItem>) -> AutomixBlocklist): Queue.Status {
        val nextResult =
            withContext(IO) {
                YouTube.next(endpoint, continuation).getOrThrow()
            }
        endpoint = nextResult.endpoint
        continuation = nextResult.continuation
        val items = nextResult.items.map { it.toMediaItem() }
        if (isTrackRadio) {
            val seed = items.find { it.mediaId == seedId } ?: preloadItem?.toMediaItem()
            if (seed != null) {
                return Queue.Status(
                    title = nextResult.title,
                    items = listOf(seed) + items.rankAutomix(seed, blocklist(items)),
                    mediaItemIndex = 0,
                )
            }
        }
        return Queue.Status(
            title = nextResult.title,
            items = items,
            mediaItemIndex = nextResult.currentIndex ?: 0,
        )
    }

    override fun hasNextPage(): Boolean = continuation != null

    override suspend fun nextPage(): List<MediaItem> {
        val nextResult =
            withContext(IO) {
                YouTube.next(endpoint, continuation).getOrThrow()
            }
        endpoint = nextResult.endpoint
        continuation = nextResult.continuation
        return nextResult.items.map { it.toMediaItem() }
    }

    companion object {
        fun radio(song: MediaMetadata) = YouTubeQueue(WatchEndpoint(song.id), song)
    }
}
