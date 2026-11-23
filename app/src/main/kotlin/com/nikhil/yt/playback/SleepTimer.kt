/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */



package com.nikhil.yt.playback

import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.time.Duration.Companion.minutes
import java.util.UUID

class SleepTimer(
    private val scope: CoroutineScope,
    val player: Player,
    private val requestRemoteChange: (SleepTimerState) -> Boolean = { false },
    private val nowMillis: () -> Long = System::currentTimeMillis,
    private val elapsedMillis: () -> Long = { System.nanoTime() / 1_000_000L },
) : Player.Listener {
    private var sleepTimerJob: Job? = null
    private var timerId = ""
    private var elapsedDeadline = -1L
    var executesLocally: Boolean = true
        private set
    var triggerTime by mutableStateOf(-1L)
        private set
    var pauseWhenSongEnd by mutableStateOf(false)
        private set
    val isActive: Boolean
        get() = triggerTime != -1L || pauseWhenSongEnd

    fun start(minute: Int) {
        require(minute == -1 || minute > 0)
        val state = SleepTimerState(UUID.randomUUID().toString(),
            remainingMs = if (minute == -1) -1L else minute.minutes.inWholeMilliseconds,
            pauseWhenSongEnd = minute == -1)
        if (!requestRemoteChange(state)) restore(state)
    }

    fun clear() {
        if (!requestRemoteChange(SleepTimerState())) restore(SleepTimerState())
    }

    fun snapshot(): SleepTimerState = SleepTimerState(timerId,
        remainingMs = if (elapsedDeadline >= 0) (elapsedDeadline - elapsedMillis()).coerceAtLeast(0) else -1L,
        pauseWhenSongEnd = pauseWhenSongEnd)

    /** Mirrors are display-only. Only the audio renderer is allowed to run the timer. */
    fun restore(state: SleepTimerState, execute: Boolean = true) {
        if (execute && executesLocally && state.isActive && state.id == timerId &&
            state.pauseWhenSongEnd == pauseWhenSongEnd) return
        sleepTimerJob?.cancel()
        sleepTimerJob = null
        executesLocally = execute
        timerId = if (state.isActive) state.id else ""
        pauseWhenSongEnd = state.isActive && state.pauseWhenSongEnd
        val duration = state.remainingMs.takeIf { state.isActive && !pauseWhenSongEnd && it >= 0 }
        triggerTime = duration?.let { nowMillis() + it } ?: -1L
        elapsedDeadline = duration?.let { elapsedMillis() + it } ?: -1L
        if (execute && duration != null) {
            val expectedId = timerId
            sleepTimerJob = scope.launch {
                delay(duration)
                if (timerId == expectedId && executesLocally) finish()
            }
        }
    }

    /** Buffering a transfer must not consume an end-of-song timer or pause the destination. */
    fun suspendExecution() {
        sleepTimerJob?.cancel()
        sleepTimerJob = null
        executesLocally = false
    }

    private fun finish() {
        sleepTimerJob = null
        restore(SleepTimerState())
        player.pause()
    }

    override fun onMediaItemTransition(
        mediaItem: MediaItem?,
        reason: Int,
    ) {
        if (executesLocally && pauseWhenSongEnd) finish()
    }

    override fun onPlaybackStateChanged(
        @Player.State playbackState: Int,
    ) {
        if (executesLocally && playbackState == Player.STATE_ENDED && pauseWhenSongEnd) finish()
    }
}
