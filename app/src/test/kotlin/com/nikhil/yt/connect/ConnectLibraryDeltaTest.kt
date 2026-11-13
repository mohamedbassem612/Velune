package com.nikhil.yt.connect

import com.nikhil.yt.localsync.*
import org.junit.Assert.*
import org.junit.Test

class ConnectLibraryDeltaTest {
    @Test fun deltaRetainsUnlikesAndPlaylistTombstonesAndOrdering() {
        val a = SyncTrack("a", "A"); val b = SyncTrack("b", "B")
        val base = LibrarySnapshot(device = "phone", counter = 1,
            likes = listOf(SyncLike(a, true, SyncVersion(1, "phone"))),
            playlists = listOf(SyncPlaylist("p", "P", songs = listOf(a, b), version = SyncVersion(1, "phone"))))
        val current = base.copy(counter = 2,
            likes = listOf(SyncLike(a, false, SyncVersion(2, "phone"))),
            playlists = listOf(base.playlists.single().copy(removed = true, version = SyncVersion(2, "phone"))))
        val delta = libraryDelta(current, base)
        val merged = LibraryMerge.merge(base, delta)
        assertFalse(merged.likes.single().liked)
        assertTrue(merged.playlists.single().removed)
        assertEquals(listOf(a, b), merged.playlists.single().songs)
        assertEquals(merged, LibraryMerge.merge(merged, delta))
    }
    @Test fun noChangesProduceEmptyDelta() {
        val a = LibrarySnapshot(device = "tv")
        val delta = libraryDelta(a, a)
        assertTrue(delta.likes.isEmpty() && delta.playlists.isEmpty() && delta.history.isEmpty())
    }

    @Test fun historyClearTombstoneSurvivesStaleReconnectAndRepeatedExchange() {
        val track = SyncTrack("a", "A")
        val event = SyncHistory(track, "2026-11-11T12:00", 5000)
        val initial = LibraryMerge.observe(LibrarySnapshot(device = "phone"),
            LibrarySnapshot(device = "phone", history = listOf(event)))
        val cleared = LibraryMerge.observe(initial, LibrarySnapshot(device = "phone"))
        assertTrue(cleared.history.single().removed)
        val delta = libraryDelta(cleared, initial)
        val merged = LibraryMerge.merge(initial, delta)
        assertTrue(merged.history.single().removed)
        assertEquals(merged.history, LibraryMerge.merge(merged, initial).history)
        assertEquals(merged.history, LibraryMerge.merge(merged, delta).history)
    }
}
