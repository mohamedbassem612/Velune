package com.nikhil.yt.localsync

import org.junit.Assert.*
import org.junit.Test
import com.nikhil.yt.together.TogetherJoinInfo
import com.nikhil.yt.together.TogetherLink

class LibraryMergeTest {
    private val song = SyncTrack("song", "Song")
    private val like = SyncLike(song, true, SyncVersion(1, "a"))

    @Test fun pairingLinkKeepsExplicitLibraryOptIn() {
        val info = TogetherJoinInfo("192.168.1.3", 42117, "session", "key", syncLibrary = true)
        assertEquals(info, TogetherLink.decode(info.toDeepLink()))
    }

    @Test fun oldListeningLinksKeepLibrarySyncDisabled() {
        val info = TogetherLink.decode("velune://together?host=192.168.1.3&port=42117&sid=session&key=key")
        assertNotNull(info)
        assertFalse(info!!.syncLibrary)
    }

    @Test fun repeatedExchangeDoesNotDuplicateHistoryOrLikes() {
        val event = SyncHistory(song, "2026-11-09T12:00", 20_000)
        val a = LibrarySnapshot(device = "a", counter = 1, likes = listOf(like), history = listOf(event))
        val b = LibrarySnapshot(device = "b")
        val merged = LibraryMerge.merge(b, a)
        assertEquals(merged, LibraryMerge.merge(merged, a))
        assertEquals(1, merged.history.size)
        assertEquals(1, merged.likes.size)
    }

    @Test fun unlikeSurvivesAStalePeerAndReconnect() {
        val old = LibrarySnapshot(device = "a", counter = 1, likes = listOf(like))
        val changed = LibraryMerge.observe(old, LibrarySnapshot(device = "a"))
        assertFalse(changed.likes.single().liked)
        assertEquals(2, changed.counter)
        assertFalse(LibraryMerge.merge(changed, old).likes.single().liked)
    }

    @Test fun simultaneousChangesChooseTheSameWinnerOnBothPeers() {
        val a = LibrarySnapshot(device = "a", counter = 3, likes = listOf(like.copy(version = SyncVersion(3, "a"))))
        val b = LibrarySnapshot(device = "b", counter = 3, likes = listOf(like.copy(liked = false, version = SyncVersion(3, "b"))))
        assertEquals(LibraryMerge.merge(a, b).likes, LibraryMerge.merge(b, a).likes)
        assertFalse(LibraryMerge.merge(a, b).likes.single().liked)
    }

    @Test fun removedPlaylistDoesNotReappearFromAnOfflinePeer() {
        val list = SyncPlaylist("list", "My list", songs = listOf(song), version = SyncVersion(5, "b"))
        val old = LibrarySnapshot(device = "a", counter = 5, playlists = listOf(list))
        val removed = LibraryMerge.observe(old, LibrarySnapshot(device = "a"))
        assertTrue(LibraryMerge.merge(removed, old).playlists.single().removed)
        assertEquals(removed, LibraryMerge.observe(removed, LibrarySnapshot(device = "a")))
    }

    @Test fun anExplicitRelikeGetsANewerVersion() {
        val old = LibrarySnapshot(device = "a", counter = 9, likes = listOf(like.copy(liked = false, version = SyncVersion(9, "b"))))
        val current = LibrarySnapshot(device = "a", likes = listOf(SyncLike(song, true)))
        val observed = LibraryMerge.observe(old, current)
        assertTrue(observed.likes.single().liked)
        assertEquals(SyncVersion(10, "a"), observed.likes.single().version)
    }
}
