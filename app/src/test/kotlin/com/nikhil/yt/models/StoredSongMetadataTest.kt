package com.nikhil.yt.models

import org.junit.Assert.*
import org.junit.Test

class StoredSongMetadataTest {
    @Test fun newPlaylistUsesStoredSongWithoutLosingItsPlaylistEntry() {
        val incoming = MediaMetadata("same-song", "Remote title", emptyList(), -1, "remote-cover", setVideoId = "playlist-entry-2")
        val stored = MediaMetadata("same-song", "Saved title", listOf(MediaMetadata.Artist("artist", "Saved artist")), 180, "saved-cover")
        val merged = incoming.withStoredSong(stored)
        assertEquals("Saved title", merged.title)
        assertEquals(180, merged.duration)
        assertEquals("saved-cover", merged.thumbnailUrl)
        assertEquals("playlist-entry-2", merged.setVideoId)
        assertEquals(stored.artists, merged.artists)
    }

    @Test fun incompleteStoredFieldsDoNotEraseAvailableMetadata() {
        val incoming = MediaMetadata("song", "Title", listOf(MediaMetadata.Artist(null, "Artist")), 200, "cover")
        val stored = MediaMetadata("song", "", emptyList(), -1)
        assertEquals(incoming, incoming.withStoredSong(stored))
    }
}
