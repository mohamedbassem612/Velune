package com.nikhil.yt.handover

import com.nikhil.yt.models.MediaMetadata
import org.junit.Assert.*
import org.junit.Test

class HandoverSessionTest {
    private val songs = listOf("a", "b", "c").map { MediaMetadata(it, it, emptyList(), 180) }
    private fun initial() = HandoverPlayback(songs, 1, 42000, 180000, true,
        "My queue", repeatMode = 2, shuffle = true, order = listOf(2, 0, 1),
        sessionId = "session", rendererId = "phone", epoch = 1)

    @Test fun transferBothWays_keepsTheEntireQueueAndPlaybackSettings() {
        val session = HandoverSessionAuthority(initial())
        val tv = session.transfer("tv")
        val phone = session.transfer("phone")
        assertEquals(songs, phone.queue)
        assertEquals(listOf(2, 0, 1), phone.order)
        assertEquals(1, phone.index)
        assertEquals(42000L, phone.positionMs)
        assertEquals(2, phone.repeatMode)
        assertTrue(phone.shuffle)
        assertTrue(phone.playing)
        assertEquals(tv.sessionId, phone.sessionId)
        assertEquals(3L, phone.epoch)
    }

    @Test fun lateStateFromPreviousRenderer_cannotOverwriteTheNewOwner() {
        val session = HandoverSessionAuthority(initial())
        session.transfer("tv")
        assertFalse(session.update("phone", 1, initial().copy(queue = songs.reversed(), positionMs = 0)))
        assertFalse(session.update("tv", 1, initial().copy(positionMs = 0)))
        assertFalse(session.update("tv", 2, session.state.copy(sessionId = "old-session", positionMs = 0)))
        assertEquals(42000L, session.state.positionMs)
        assertEquals("tv", session.state.rendererId)
    }

    @Test fun positionHeartbeats_doNotChangeQueueRevision_butQueueEditsDo() {
        val session = HandoverSessionAuthority(initial())
        assertTrue(session.update("phone", 1, initial().copy(positionMs = 43000)))
        assertEquals(0L, session.state.queueRevision)
        assertTrue(session.update("phone", 1, session.state.copy(queue = songs + songs.first(), order = listOf(2, 0, 1, 3))))
        assertEquals(1L, session.state.queueRevision)
        assertEquals(4, session.state.queue.size)
        assertEquals("a", session.state.queue.last().id)
    }

    @Test fun updatedQueueFromTv_survivesTransferBackToPhone() {
        val session = HandoverSessionAuthority(initial())
        session.transfer("tv")
        val edited = session.state.copy(queue = listOf(songs[2], songs[0]), order = listOf(0, 1), index = 0, positionMs = 90000)
        assertTrue(session.update("tv", 2, edited))
        session.transfer("phone")
        assertEquals(listOf("c", "a"), session.state.queue.map { it.id })
        assertEquals("c", session.state.song?.id)
        assertEquals(90000L, session.state.positionMs)
    }
}
