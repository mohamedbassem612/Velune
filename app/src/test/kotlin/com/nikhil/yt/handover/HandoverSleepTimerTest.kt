package com.nikhil.yt.handover

import com.nikhil.yt.playback.SleepTimerState
import org.junit.Assert.*
import org.junit.Test

class HandoverSleepTimerTest {
    private fun playback(timer: SleepTimerState) = HandoverPlayback(
        emptyList(), -1, 0, 0, true, rendererId = "phone", sessionId = "session", sleepTimer = timer,
    )

    @Test fun timedAndEndOfSongTimersSurviveProtocolRoundTripsAndBothTransferDirections() {
        for (timer in listOf(SleepTimerState("timed", 420_000), SleepTimerState("end", pauseWhenSongEnd = true))) {
            val session = HandoverSessionAuthority(playback(timer).toSessionJson().sessionState())
            assertEquals(timer, session.transfer("tv").sleepTimer)
            assertEquals(timer, session.transfer("phone").toSessionJson().sessionState().sleepTimer)
        }
    }

    @Test fun partialHeartbeatsCarryTimerCancellationWithoutReplacingTheQueue() {
        val base = playback(SleepTimerState("timer", 60_000))
        val cleared = base.copy(sleepTimer = SleepTimerState()).toSessionJson(includeQueue = false).sessionState(base)
        assertFalse(cleared.sleepTimer.isActive)
        assertEquals(base.queue, cleared.queue)
    }

    @Test fun timerDurationDoesNotDependOnPhoneAndTvWallClocksMatching() {
        val timer = SleepTimerState("timer", 60_000)
        val json = playback(timer).toSessionJson()
        assertEquals(60_000L, json.sessionState().sleepTimer.remainingMs)
        assertEquals(45_000L, timer.elapsed(15_000).remainingMs)
        assertEquals(0L, timer.elapsed(120_000).remainingMs)
    }

    @Test fun olderPeersWithoutTimerFieldsRemainCompatible() {
        val json = playback(SleepTimerState()).toSessionJson()
        json.remove("sleepTimer")
        assertFalse(json.sessionState().sleepTimer.isActive)
    }

    @Test fun closingPhoneDoesNotClearTvTimer() {
        val timer = SleepTimerState("timer", 30_000)
        val owner = HandoverSessionAuthority(playback(timer).copy(rendererId = "tv"))
        assertFalse(owner.deviceDisconnected("phone"))
        assertEquals(timer, owner.state.sleepTimer)
    }
}
