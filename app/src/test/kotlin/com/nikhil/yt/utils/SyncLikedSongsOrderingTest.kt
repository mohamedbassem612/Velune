package com.nikhil.yt.utils

import org.junit.Assert.assertEquals
import org.junit.Test
import java.time.LocalDateTime

class SyncLikedSongsOrderingTest {
    @Test
    fun likedSongTimestamp_decrementsBySecondsPerIndex() {
        val base = LocalDateTime.of(2026, 1, 1, 0, 0, 0)
        assertEquals(base, likedSongTimestamp(base, 0))
        assertEquals(base.minusSeconds(1), likedSongTimestamp(base, 1))
        assertEquals(base.minusSeconds(123), likedSongTimestamp(base, 123))
    }
    @Test fun refreshKeepsExistingLikesBehindANewerTvLike() {
        val oldLike = LocalDateTime.of(2026, 11, 1, 12, 0)
        val tvLike = oldLike.plusDays(1)
        val refresh = tvLike.plusSeconds(2)
        assertEquals(oldLike, syncedLikedDate(true, oldLike, refresh))
        assertEquals(listOf(tvLike, oldLike), listOf(syncedLikedDate(true, oldLike, refresh), tvLike).sortedDescending())
    }

    @Test fun newAndRelikedSongsReceiveTheImportedDate() {
        val imported = LocalDateTime.of(2026, 11, 14, 12, 0)
        assertEquals(imported, syncedLikedDate(false, imported.minusDays(1), imported))
        assertEquals(imported, syncedLikedDate(true, null, imported))
    }
}
