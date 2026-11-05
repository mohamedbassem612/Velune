package com.nikhil.yt.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SpotifyTrackMatcherTest {
    private data class Track(val title: String, val artist: String)
    private fun match(title: String, artist: String?, vararg candidates: Track) =
        SpotifyTrackMatcher.bestMatch(title, artist, candidates.toList(), { it.title }, { listOf(it.artist) })

    @Test fun selectsMatchingArtistInsteadOfFirstSearchResult() {
        val correct = Track("Hello", "Adele")
        assertEquals(correct, match("Hello", "Adele", Track("Hello", "Lionel Richie"), correct))
    }

    @Test fun rejectsDifferentRecordingsAndMissingEvidence() {
        listOf("Hello (Live)", "Hello Remix", "Hello Cover", "Hello Karaoke", "Hello Instrumental").forEach {
            assertNull(match("Hello", "Adele", Track(it, "Adele")))
        }
        assertNull(match("Hello", "Adele", Track("Someone Like You", "Adele")))
        assertNull(match("Hello", null, Track("Hello", "Adele")))
        assertNull(match("I Love You", "Artist", Track("I Love You So", "Artist")))
    }

    @Test fun preservesRequestedVersion() {
        val live = Track("Hello - Live", "Adele")
        assertEquals(live, match("Hello (Live)", "Adele", Track("Hello", "Adele"), live))
    }

    @Test fun acceptsCollaborationsWithSeparateArtistCredits() {
        val song = Track("Die With A Smile", "Lady Gaga")
        assertEquals(song, match("Die With A Smile", "Lady Gaga, Bruno Mars", song))
    }

    @Test fun toleratesCatalogAnnotationsWithoutLosingRecordingVersion() {
        val remastered = Track("Hello - 2015 Remaster", "Adele")
        assertEquals(remastered, match("Hello", "Adele", remastered))
        val collaboration = Track("Hello (feat. Guest)", "Adele")
        assertEquals(collaboration, match("Hello", "Adele", collaboration))
        assertNull(match("Hello", "Adele", Track("Hello (Live Remaster)", "Adele")))
    }

    @Test fun comparesUnicodeTitlesAndArtists() {
        val track = Track("حبيبي يا نور العين", "عمرو دياب")
        assertEquals(track, match("حَبِيبي يا نور العَين", "عمرو دياب", track))
        assertEquals(Track("Deja Vu", "Beyonce"), match("Déjà Vu", "Beyoncé", Track("Deja Vu", "Beyonce")))
    }
}
