package com.nikhil.yt.handover

import com.nikhil.yt.models.MediaMetadata
import org.junit.Assert.*
import org.junit.Test

class HandoverDisconnectionTest {
    private fun state(renderer: String, playing: Boolean = true) = HandoverPlayback(
        listOf(MediaMetadata("song", "Song", emptyList(), 180)), 0, 42500, 180000,
        playing, sessionId = "session", rendererId = renderer, epoch = 7,
    )

    @Test fun closingPhoneDoesNotPauseAnIndependentlyPlayingTv() {
        val owner = HandoverSessionAuthority(state("tv"))
        val before = owner.state
        assertFalse(owner.deviceDisconnected("phone"))
        assertEquals(before, owner.state)
    }

    @Test fun phonePauseThenClosureKeepsTvPausedWithItsQueueAndOwnership() {
        val owner = HandoverSessionAuthority(state("tv", false))
        val before = owner.state
        assertFalse(owner.deviceDisconnected("phone"))
        assertEquals(before, owner.state)
        assertTrue(owner.update("tv", 7, owner.state.copy(playing = true)))
        assertEquals(42500L, owner.state.positionMs)
        assertTrue(owner.state.playing)
    }

    @Test fun missingPhoneRendererDoesNotStartTvUntilExplicitLocalChoice() {
        val owner = HandoverSessionAuthority(state("phone"))
        assertTrue(owner.deviceDisconnected("phone"))
        assertFalse(owner.state.playing)
        assertEquals("phone", owner.state.rendererId)
        val claimed = owner.claimOfflinePlayback("tv")
        assertEquals("tv", claimed.rendererId)
        assertEquals(42500L, claimed.positionMs)
        assertFalse(claimed.playing)
        assertTrue(owner.update("tv", claimed.epoch, claimed.copy(playing = true)))
        assertTrue(owner.state.playing)
        assertFalse(owner.update("phone", 7, state("phone")))
    }

    @Test fun tvLossFreezesPhoneMirrorWithoutInventingPlaybackProgress() {
        val checkpoint = state("tv")
        val offline = checkpoint.offlineMirror()
        assertFalse(offline.playing)
        assertEquals("tv", offline.rendererId)
        assertEquals(checkpoint.positionMs, offline.positionMs)
        assertEquals(checkpoint.queue, offline.queue)
        // Keep the separate checkpoint's play/pause choice for an explicit local resume.
        assertTrue(checkpoint.playing)
        assertFalse(state("tv", false).offlineMirror().playing)
    }

    @Test fun reconnectUsesCurrentTvStateWithoutImportingOldPhoneQueue() {
        val owner = HandoverSessionAuthority(state("tv"))
        owner.deviceDisconnected("phone")
        owner.update("tv", 7, owner.state.copy(positionMs = 60000))
        assertFalse(owner.update("phone", 7, state("phone").copy(positionMs = 0)))
        assertEquals(60000L, owner.state.positionMs)
        assertEquals("tv", owner.state.rendererId)
    }

    @Test fun restartedTvCanClaimMissingPhoneCheckpointButDoesNotAutoplay() {
        val owner = HandoverSessionAuthority(recoverHandoverSession(state("phone")))
        assertFalse(owner.state.playing)
        val claimed = owner.claimOfflinePlayback("tv")
        assertEquals(42500L, claimed.positionMs)
        assertFalse(claimed.playing)
        assertTrue(claimed.epoch > 7)
    }
}
