package com.nikhil.yt.handover

import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

class HandoverVolumeTest {
    private fun state(volume: HandoverVolume) = HandoverPlayback(emptyList(), -1, 0, 0, false,
        sessionId = "session", rendererId = "tv", deviceVolume = volume)

    @Test fun realTvRangeAndMuteSurviveFullAndIncrementalFrames() {
        val original = state(HandoverVolume(27, 0, 100, true))
        val full = JSONObject(original.toSessionJson().toString()).sessionState()
        assertEquals(original.deviceVolume, full.deviceVolume)
        val next = original.copy(deviceVolume = HandoverVolume(28, 0, 100))
        assertEquals(next.deviceVolume, JSONObject(next.toSessionJson(false).toString()).sessionState(full).deviceVolume)
    }
    @Test fun absoluteVolumeIsBoundedByTheDestinationRange() {
        val volume = HandoverVolume(12, 3, 50)
        assertEquals(3, volume.clamp(-100))
        assertEquals(50, volume.clamp(1000))
        assertEquals(22, volume.clamp(22))
    }
    @Test fun fixedAndUnknownOutputsCannotAdvertiseAdjustableVolume() {
        assertFalse(HandoverVolume().adjustable)
        assertFalse(HandoverVolume(10, 0, 100, fixed = true).adjustable)
        assertTrue(HandoverVolume(10, 0, 100).adjustable)
    }
    @Test fun legacyFrameDoesNotEraseKnownDeviceVolume() {
        val base = state(HandoverVolume(40, 0, 100))
        val frame = base.toSessionJson(false).apply { remove("deviceVolume") }
        assertEquals(base.deviceVolume, frame.sessionState(base).deviceVolume)
    }
    @Test fun invalidRemoteRangeAndLevelAreNormalized() {
        val frame = state(HandoverVolume()).toSessionJson().put("deviceVolume",
            JSONObject().put("min", -5).put("max", -2).put("level", 900))
        assertEquals(HandoverVolume(), frame.sessionState().deviceVolume)
    }
    @Test fun volumeUpdatesDoNotReplaceTheQueueOrOwnership() {
        val owner = HandoverSessionAuthority(state(HandoverVolume(20, 0, 100)))
        val before = owner.state
        assertTrue(owner.update("tv", before.epoch, before.copy(deviceVolume = HandoverVolume(21, 0, 100))))
        assertEquals(before.queueRevision, owner.state.queueRevision)
        assertEquals(before.rendererId, owner.state.rendererId)
        assertEquals(21, owner.state.deviceVolume.level)
    }
}
