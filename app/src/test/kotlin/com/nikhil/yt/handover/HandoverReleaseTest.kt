package com.nikhil.yt.handover

import com.nikhil.yt.connect.ConnectTransfer
import com.nikhil.yt.models.MediaMetadata
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class HandoverReleaseTest {
    private val state = HandoverPlayback(
        queue = listOf(MediaMetadata("track", "Track", emptyList(), 180)), index = 0,
        positionMs = 10000, durationMs = 180000, playing = true,
        sessionId = "session", rendererId = "phone", epoch = 4,
    )

    @Test fun phoneReleaseIsAcceptedAndTvCommitsTheFinalPosition() {
        val owner = HandoverSessionAuthority(state)
        val transfer = ConnectTransfer("phone", "tv", state.epoch)
        transfer.ready(); transfer.releasing()
        // Playback advances while the TV buffers. The acknowledgement uses the final source position.
        val frame = JSONObject(state.releaseAcknowledgement("transfer", state.copy(positionMs = 12750)).toString())
        assertTrue(owner.state.acceptsEnvelope(frame))
        assertEquals("released", frame.getString("type"))
        assertEquals("transfer", frame.getString("transferId"))
        val final = frame.getJSONObject("state").sessionState()
        transfer.released()
        transfer.commit(owner.state.rendererId, owner.state.epoch)
        assertTrue(owner.update("phone", state.epoch, final))
        owner.transfer("tv")
        assertEquals("tv", owner.state.rendererId)
        assertEquals(12750L, owner.state.positionMs)
        assertTrue(owner.state.playing)
    }

    @Test fun missingOrStaleReleaseEnvelopeCannotCommit() {
        val missing = JSONObject().put("type", "released").put("state", state.toSessionJson())
        assertFalse(state.acceptsEnvelope(missing))
        val frame = state.releaseAcknowledgement("transfer", state)
        assertFalse(state.copy(epoch = 5).acceptsEnvelope(frame))
        assertFalse(state.copy(sessionId = "new-session").acceptsEnvelope(frame))
    }
}
