package com.nikhil.yt.handover

import androidx.media3.common.FlagSet
import androidx.media3.common.ForwardingPlayer
import androidx.media3.common.MediaItem
import androidx.media3.common.PlaybackParameters
import androidx.media3.common.Player
import androidx.media3.exoplayer.ExoPlayer
import androidx.media3.exoplayer.source.ShuffleOrder
import com.nikhil.yt.extensions.metadata
import com.nikhil.yt.playback.queues.Queue
import org.json.JSONArray
import org.json.JSONObject
import java.util.concurrent.CopyOnWriteArraySet

/** The UI and Android MediaSession use this Player for both local and remote rendering. */
class HandoverPlayer(private val local: ExoPlayer, private val session: HandoverController) : ForwardingPlayer(local) {
    private val listeners = CopyOnWriteArraySet<Player.Listener>()
    private val remote get() = session.selected.value != null

    override fun addListener(listener: Player.Listener) { listeners.add(listener); super.addListener(listener) }
    override fun removeListener(listener: Player.Listener) { listeners.remove(listener); super.removeListener(listener) }
    override fun getCurrentPosition(): Long = if (remote) session.currentPosition() else local.currentPosition
    override fun getContentPosition(): Long = currentPosition
    override fun getDuration(): Long = if (remote) session.playback.value?.durationMs ?: 0L else local.duration
    override fun getContentDuration(): Long = duration
    override fun getBufferedPosition(): Long = if (remote) currentPosition else local.bufferedPosition
    override fun getPlayWhenReady(): Boolean = if (remote) session.playback.value?.playing == true else local.playWhenReady
    override fun isPlaying(): Boolean = if (remote) playWhenReady && playbackState == Player.STATE_READY else local.isPlaying
    override fun getPlaybackState(): Int = if (remote) session.playback.value?.playbackState ?: Player.STATE_IDLE else local.playbackState
    override fun getShuffleModeEnabled(): Boolean = if (remote) session.playback.value?.shuffle == true else local.shuffleModeEnabled
    override fun getRepeatMode(): Int = if (remote) session.playback.value?.repeatMode ?: 0 else local.repeatMode
    override fun getPlaybackParameters(): PlaybackParameters = if (remote) PlaybackParameters(session.playback.value?.speed ?: 1f) else local.playbackParameters
    override fun prepare() { if (session.controlsFrozen) return; if (!remote) local.prepare() }
    override fun play() { if (session.controlsFrozen) return; if (remote) session.command("resume") else local.play() }
    override fun pause() { if (remote) session.command("pause") else local.pause() }
    override fun setPlayWhenReady(value: Boolean) { if (value && session.controlsFrozen) return; if (remote) session.command(if (value) "resume" else "pause") else local.playWhenReady = value }
    override fun stop() { if (remote) session.command("stop") else local.stop() }
    override fun clearMediaItems() { if (remote) session.command("clear") else local.clearMediaItems() }
    override fun seekTo(positionMs: Long) { if (remote) session.command("seek", positionMs) else local.seekTo(positionMs) }
    override fun seekTo(mediaItemIndex: Int, positionMs: Long) {
        if (remote) session.request(JSONObject().put("name", "seek").put("index", mediaItemIndex).put("position", positionMs.coerceAtLeast(0)))
        else local.seekTo(mediaItemIndex, positionMs)
    }
    override fun seekToDefaultPosition() = seekTo(0L)
    override fun seekToDefaultPosition(mediaItemIndex: Int) = seekTo(mediaItemIndex, 0L)
    override fun seekToNext() { if (remote) session.command("next") else local.seekToNext() }
    override fun seekToNextMediaItem() { if (remote) session.command("next") else local.seekToNextMediaItem() }
    override fun seekToPrevious() { if (remote) session.command("previous") else local.seekToPrevious() }
    override fun seekToPreviousMediaItem() { if (remote) session.command("previous") else local.seekToPreviousMediaItem() }
    override fun seekBack() = seekTo((currentPosition - seekBackIncrement).coerceAtLeast(0))
    override fun seekForward() = seekTo((currentPosition + seekForwardIncrement).coerceAtMost(duration.takeIf { it > 0 } ?: Long.MAX_VALUE))
    override fun setShuffleModeEnabled(enabled: Boolean) {
        if (remote) session.request(JSONObject().put("name", "shuffle").put("enabled", enabled)) else local.shuffleModeEnabled = enabled
    }
    override fun setRepeatMode(mode: Int) {
        if (remote) session.request(JSONObject().put("name", "repeat").put("mode", mode)) else local.repeatMode = mode
    }
    override fun setPlaybackParameters(parameters: PlaybackParameters) {
        if (remote) session.request(JSONObject().put("name", "speed").put("speed", parameters.speed)) else local.playbackParameters = parameters
    }
    override fun setPlaybackSpeed(speed: Float) {
        if (remote) session.request(JSONObject().put("name", "speed").put("speed", speed)) else local.setPlaybackSpeed(speed)
    }
    override fun setMediaItem(item: MediaItem) = setMediaItems(listOf(item), 0, 0L)
    override fun setMediaItem(item: MediaItem, startPositionMs: Long) = setMediaItems(listOf(item), 0, startPositionMs)
    override fun setMediaItem(item: MediaItem, resetPosition: Boolean) = setMediaItems(listOf(item), 0, if (resetPosition) 0L else currentPosition)
    override fun setMediaItems(items: List<MediaItem>) = setMediaItems(items, 0, 0L)
    override fun setMediaItems(items: List<MediaItem>, resetPosition: Boolean) = setMediaItems(items, 0, if (resetPosition) 0L else currentPosition)
    override fun setMediaItems(items: List<MediaItem>, startIndex: Int, startPositionMs: Long) {
        if (remote) session.sendQueue(Queue.Status(null, items, startIndex, startPositionMs), playWhenReady)
        else local.setMediaItems(items, startIndex, startPositionMs)
    }
    override fun addMediaItem(item: MediaItem) = addMediaItems(listOf(item))
    override fun addMediaItem(index: Int, item: MediaItem) = addMediaItems(index, listOf(item))
    override fun addMediaItems(items: List<MediaItem>) {
        if (remote) session.addTracks(items, next = false) else local.addMediaItems(items)
    }
    override fun addMediaItems(index: Int, items: List<MediaItem>) {
        if (remote) session.request(JSONObject().put("name", "insert").put("index", index)
            .put("queue", JSONArray(items.mapNotNull { it.metadata }.map { it.toHandoverJson() })))
        else local.addMediaItems(index, items)
    }
    override fun moveMediaItem(fromIndex: Int, toIndex: Int) {
        if (remote) session.request(JSONObject().put("name", "move").put("from", fromIndex).put("to", toIndex))
        else local.moveMediaItem(fromIndex, toIndex)
    }
    override fun moveMediaItems(fromIndex: Int, toIndex: Int, newIndex: Int) {
        if (remote) session.request(JSONObject().put("name", "moveRange").put("from", fromIndex).put("to", toIndex).put("index", newIndex))
        else local.moveMediaItems(fromIndex, toIndex, newIndex)
    }
    override fun replaceMediaItem(index: Int, item: MediaItem) = replaceMediaItems(index, index + 1, listOf(item))
    override fun replaceMediaItems(fromIndex: Int, toIndex: Int, items: List<MediaItem>) {
        if (remote) session.request(JSONObject().put("name", "replaceItems").put("from", fromIndex).put("to", toIndex)
            .put("queue", JSONArray(items.mapNotNull { it.metadata }.map { it.toHandoverJson() })))
        else local.replaceMediaItems(fromIndex, toIndex, items)
    }
    override fun removeMediaItem(index: Int) = removeMediaItems(index, index + 1)
    override fun removeMediaItems(fromIndex: Int, toIndex: Int) {
        if (remote) session.request(JSONObject().put("name", "remove").put("from", fromIndex).put("to", toIndex))
        else local.removeMediaItems(fromIndex, toIndex)
    }
    fun setShuffleOrder(order: ShuffleOrder) {
        if (!remote) { local.setShuffleOrder(order); return }
        val indices = mutableListOf<Int>(); var index = order.firstIndex
        while (index >= 0 && indices.size < order.length) { indices.add(index); index = order.getNextIndex(index) }
        session.request(JSONObject().put("name", "order").put("order", JSONArray(indices)))
    }

    fun onSessionStateChanged(previous: HandoverPlayback?, state: HandoverPlayback) {
        if (!remote) return
        if (previous != null && previous.playing == state.playing && previous.playbackState == state.playbackState && previous.rendererId == state.rendererId) return
        val events = Player.Events(FlagSet.Builder().add(Player.EVENT_PLAY_WHEN_READY_CHANGED)
            .add(Player.EVENT_IS_PLAYING_CHANGED).add(Player.EVENT_PLAYBACK_STATE_CHANGED).build())
        listeners.forEach {
            it.onPlayWhenReadyChanged(state.playing, Player.PLAY_WHEN_READY_CHANGE_REASON_USER_REQUEST)
            it.onIsPlayingChanged(isPlaying)
            it.onPlaybackStateChanged(state.playbackState)
            it.onEvents(this, events)
        }
    }
}
