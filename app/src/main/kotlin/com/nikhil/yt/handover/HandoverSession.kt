package com.nikhil.yt.handover

import com.nikhil.yt.models.MediaMetadata

/** One queue and one renderer. The TV coordinates ownership while either device can render. */
data class HandoverPlayback(
    val queue: List<MediaMetadata>,
    val index: Int,
    val positionMs: Long,
    val durationMs: Long,
    val playing: Boolean,
    val queueTitle: String? = null,
    val repeatMode: Int = 0,
    val shuffle: Boolean = false,
    val order: List<Int> = queue.indices.toList(),
    val speed: Float = 1f,
    val sessionId: String = "",
    val rendererId: String = "",
    val epoch: Long = 0,
    val revision: Long = 0,
    val queueRevision: Long = 0,
    val playbackState: Int = 3,
) {
    val song: MediaMetadata? get() = queue.getOrNull(index)
}

/** Epochs reject late state from a device which has already handed playback over. */
internal class HandoverSessionAuthority(initial: HandoverPlayback) {
    var state: HandoverPlayback = initial
        private set

    fun update(rendererId: String, epoch: Long, snapshot: HandoverPlayback): Boolean {
        if (rendererId != state.rendererId || epoch != state.epoch ||
            (snapshot.sessionId.isNotBlank() && snapshot.sessionId != state.sessionId)) return false
        val queueChanged = state.queue != snapshot.queue || state.order != snapshot.order
        state = snapshot.copy(
            sessionId = state.sessionId, rendererId = state.rendererId, epoch = state.epoch,
            revision = state.revision + 1,
            queueRevision = state.queueRevision + if (queueChanged) 1 else 0,
        )
        return true
    }

    fun transfer(rendererId: String): HandoverPlayback {
        state = state.copy(rendererId = rendererId, epoch = state.epoch + 1, revision = state.revision + 1)
        return state
    }
}
