package com.nikhil.yt.handover

import androidx.media3.common.MediaItem
import com.nikhil.yt.models.MediaMetadata
import com.nikhil.yt.playback.queues.Queue
import com.nikhil.yt.playback.queues.YouTubeQueue

/** Keeps the source position when the normal radio queue finishes loading. */
internal class HandoverQueue(
    song: MediaMetadata,
    private val startPositionMs: Long,
) : Queue {
    private val source = YouTubeQueue.radio(song)
    override val preloadItem = song
    override suspend fun getInitialStatus(): Queue.Status =
        source.getInitialStatus().copy(position = startPositionMs.coerceAtLeast(0))
    override fun hasNextPage(): Boolean = source.hasNextPage()
    override suspend fun nextPage(): List<MediaItem> = source.nextPage()
}
