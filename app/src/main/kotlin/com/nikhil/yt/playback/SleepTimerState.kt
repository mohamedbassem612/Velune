package com.nikhil.yt.playback

/** A relative duration travels between devices without depending on their wall clocks matching. */
data class SleepTimerState(
    val id: String = "",
    val remainingMs: Long = -1L,
    val pauseWhenSongEnd: Boolean = false,
) {
    val isActive: Boolean get() = id.isNotBlank() && (remainingMs >= 0 || pauseWhenSongEnd)

    fun elapsed(milliseconds: Long): SleepTimerState =
        if (remainingMs >= 0) copy(remainingMs = (remainingMs - milliseconds.coerceAtLeast(0)).coerceAtLeast(0)) else this
}
