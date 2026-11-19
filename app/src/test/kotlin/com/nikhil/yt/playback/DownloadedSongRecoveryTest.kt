package com.nikhil.yt.playback

import org.junit.Assert.*
import org.junit.Test

class DownloadedSongRecoveryTest {
    @Test fun freshInstallationRebuildsAllMissingRowsFromLocalFiles() {
        assertEquals(setOf("one", "two"), DownloadedSongRecovery.missingIds(setOf("one", "two"), emptySet()))
        assertEquals(setOf("two"), DownloadedSongRecovery.missingIds(setOf("one", "two"), setOf("one")))
        assertTrue(DownloadedSongRecovery.missingIds(setOf("one"), setOf("one")).isEmpty())
    }

    @Test fun tagsRestoreArabicMetadataWithoutAnAccountOrNetworkResponse() {
        val recovered = DownloadedSongRecovery.recover("songId", " نور العين ", " عمرو دياب ", " الألبوم ", "240500", "ignored.mp3")
        assertEquals("نور العين", recovered.metadata.title)
        assertEquals("عمرو دياب", recovered.metadata.artists.single().name)
        assertEquals(240, recovered.metadata.duration)
        assertEquals("الألبوم", recovered.albumName)
        assertFalse(recovered.metadata.liked)
        assertNull(recovered.metadata.inLibrary)
        // A recovered album title must not create a fake online album browse ID.
        assertNull(recovered.metadata.album)
    }

    @Test fun untaggedOrDamagedFilesUseTheFilenameAndPreserveTheVideoId() {
        val recovered = DownloadedSongRecovery.recover("abc123", null, " ", null, "invalid", "My Song-abc123.mp3")
        assertEquals("abc123", recovered.metadata.id)
        assertEquals("My Song", recovered.metadata.title)
        assertTrue(recovered.metadata.artists.isEmpty())
        assertEquals(-1, recovered.metadata.duration)
    }

    @Test fun missingNameAndInvalidDurationStillYieldAVisibleSong() {
        val recovered = DownloadedSongRecovery.recover("abc123", " ", null, " ", "-1", null)
        assertEquals("abc123", recovered.metadata.title)
        assertEquals(-1, recovered.metadata.duration)
        assertNull(recovered.albumName)
    }
}
