package com.nikhil.yt.playback

import androidx.media3.common.Player
import java.lang.reflect.Proxy
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.withTimeout
import kotlinx.coroutines.yield
import org.junit.Assert.*
import org.junit.Test

class SleepTimerTest {
    private class Playback {
        var pauses = 0
        val paused = CompletableDeferred<Unit>()
        val player = Proxy.newProxyInstance(Player::class.java.classLoader, arrayOf(Player::class.java)) { _, method, _ ->
            when (method.name) {
                "pause" -> { pauses++; paused.complete(Unit); null }
                "toString" -> "TestPlayer"
                else -> null
            }
        } as Player
    }

    @Test fun remoteTimerChangesAreRoutedWithoutStartingAPhoneCountdown() = runBlocking {
        val playback = Playback()
        val requests = mutableListOf<SleepTimerState>()
        val timer = SleepTimer(this, playback.player, requestRemoteChange = { requests.add(it); true })
        timer.start(15)
        assertEquals(900_000L, requests.single().remainingMs)
        assertFalse(timer.isActive)
        timer.clear()
        assertFalse(requests.last().isActive)
        assertEquals(0, playback.pauses)
    }

    @Test fun transferUsesRemainingTimeInsteadOfRestartingTheOriginalDuration() = runBlocking {
        var elapsed = 1_000L
        val sourcePlayback = Playback()
        val source = SleepTimer(this, sourcePlayback.player, elapsedMillis = { elapsed })
        source.start(10)
        elapsed += 180_000L
        val snapshot = source.snapshot()
        assertEquals(420_000L, snapshot.remainingMs)
        // A mirror displays the timer but must not pause its own player on a song change.
        source.restore(snapshot, execute = false)
        val destination = SleepTimer(this, Playback().player)
        destination.restore(snapshot)
        assertTrue(destination.executesLocally)
        assertEquals(snapshot.id, destination.snapshot().id)
        assertTrue(destination.snapshot().remainingMs in 419_000L..420_000L)
        source.onMediaItemTransition(null, Player.MEDIA_ITEM_TRANSITION_REASON_SEEK)
        assertEquals(0, sourcePlayback.pauses)
        destination.clear()
        source.clear()
    }

    @Test fun endOfSongTimerIgnoresDestinationBufferingAndStopsTheNewRenderer() = runBlocking {
        val playback = Playback()
        val timer = SleepTimer(this, playback.player)
        timer.start(-1)
        timer.suspendExecution()
        timer.onMediaItemTransition(null, Player.MEDIA_ITEM_TRANSITION_REASON_PLAYLIST_CHANGED)
        assertTrue(timer.pauseWhenSongEnd)
        assertEquals(0, playback.pauses)
        timer.restore(timer.snapshot())
        timer.onMediaItemTransition(null, Player.MEDIA_ITEM_TRANSITION_REASON_AUTO)
        assertEquals(1, playback.pauses)
        assertFalse(timer.isActive)
    }

    @Test fun zeroRemainingTimeStopsOnlyTheRendererEvenAfterThePhoneDisconnects() = runBlocking {
        val phonePlayback = Playback()
        val tvPlayback = Playback()
        val phone = SleepTimer(this, phonePlayback.player)
        val tv = SleepTimer(this, tvPlayback.player)
        val transferred = SleepTimerState("timer", 0L)
        phone.restore(transferred, execute = false)
        tv.restore(transferred)
        // Closing the phone's mirror cannot cancel the independently running TV timer.
        phone.clear()
        withTimeout(1_000) { tvPlayback.paused.await() }
        assertEquals(0, phonePlayback.pauses)
        assertEquals(1, tvPlayback.pauses)
        assertFalse(tv.isActive)
    }

    @Test fun mirroredEndOfSongTimerIgnoresLocalPlayerEvents() = runBlocking {
        val playback = Playback()
        val timer = SleepTimer(this, playback.player)
        val remote = SleepTimerState("remote", pauseWhenSongEnd = true)
        timer.restore(remote, execute = false)
        timer.onMediaItemTransition(null, Player.MEDIA_ITEM_TRANSITION_REASON_AUTO)
        timer.onPlaybackStateChanged(Player.STATE_ENDED)
        assertTrue(timer.isActive)
        assertEquals(remote, timer.snapshot())
        assertEquals(0, playback.pauses)
        timer.clear()
    }

    @Test fun changingTimerModeClearsThePreviousMode() = runBlocking {
        val playback = Playback()
        val timer = SleepTimer(this, playback.player)
        timer.start(-1)
        timer.start(5)
        assertFalse(timer.pauseWhenSongEnd)
        assertTrue(timer.triggerTime > 0)
        timer.start(-1)
        assertEquals(-1L, timer.triggerTime)
        assertTrue(timer.pauseWhenSongEnd)
        timer.clear()
        yield()
        assertFalse(timer.isActive)
        assertEquals(0, playback.pauses)
    }
}
