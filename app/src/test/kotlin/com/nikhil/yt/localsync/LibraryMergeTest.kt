package com.nikhil.yt.localsync

import org.junit.Assert.*
import org.junit.Test
import com.nikhil.yt.together.TogetherJoinInfo
import com.nikhil.yt.together.TogetherLink

class LibraryMergeTest {
    private val song = SyncTrack("song", "Song")
    private val like = SyncLike(song, true, SyncVersion(1, "a"))

    @Test fun changingPhoneReplacesLikesPlaylistsHistoryAndOldDeletionClocks() {
        val old = LibrarySnapshot(device = "tv", counter = 100,
            likes = listOf(like.copy(liked = false, version = SyncVersion(100, "old-phone"))),
            playlists = listOf(SyncPlaylist("old-list", "Old", version = SyncVersion(99, "old-phone"))),
            history = listOf(SyncHistory(song, "2026-11-09T12:00", 100)))
        val fresh = LibrarySnapshot(device = "new-phone", counter = 1, likes = listOf(like))
        val result = LibraryMerge.forConnect(old, fresh, replacing = true)
        assertEquals("tv", result.device)
        assertEquals(fresh.likes, result.likes)
        assertEquals(1L, result.counter)
        assertTrue(result.playlists.isEmpty())
        assertTrue(result.history.isEmpty())
    }

    @Test fun switchingToAnEmptyPhoneClearsThePreviousLibrary() {
        val old = LibrarySnapshot(device = "tv", counter = 1, likes = listOf(like))
        assertEquals(LibrarySnapshot(device = "tv"),
            LibraryMerge.forConnect(old, LibrarySnapshot(device = "empty-phone"), replacing = true))
    }

    @Test fun reconnectingSamePhoneRetainsTvChanges() {
        val local = LibrarySnapshot(device = "tv", counter = 2,
            likes = listOf(like.copy(liked = false, version = SyncVersion(2, "tv"))))
        val remote = LibrarySnapshot(device = "phone", counter = 1, likes = listOf(like))
        assertFalse(LibraryMerge.forConnect(local, remote, replacing = false).likes.single().liked)
    }

    @Test fun returningToPreviousPhoneRestoresOnlyThatPhonesLibrary() {
        val a = LibrarySnapshot(device = "phone-a", counter = 1, likes = listOf(like))
        val b = LibrarySnapshot(device = "phone-b", counter = 2,
            likes = listOf(like.copy(track = SyncTrack("other", "Other"), version = SyncVersion(2, "phone-b"))))
        val tv = LibrarySnapshot(device = "tv")
        val withA = LibraryMerge.forConnect(tv, a, replacing = true)
        val withB = LibraryMerge.forConnect(withA, b, replacing = true)
        val backToA = LibraryMerge.forConnect(withB, a, replacing = true)
        assertEquals(listOf("song"), backToA.likes.map { it.track.id })
    }

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
