package com.nikhil.yt.ui.tv

import com.nikhil.yt.models.MediaMetadata
import org.junit.Assert.*
import org.junit.Test

class TvHomeSelectorTest {
    private fun song(id: String, artist: String = "Artist $id") = MediaMetadata(
        id = id, title = id, duration = 180,
        artists = listOf(MediaMetadata.Artist(null, artist)),
    )

    @Test
    fun overlappingSourcesProduceDistinctShelvesAndStableResults() {
        val songs = (1..60).map { song("$it") }
        val result = TvHomeSelector.select(songs, songs.reversed(), songs, songs)
        assertEquals(24, result.personal.size)
        assertEquals(12, result.radio.size)
        val ids = (result.personal + result.radio).map { it.id }
        assertEquals(ids.size, ids.toSet().size)
        assertEquals(result, TvHomeSelector.select(songs, songs.reversed(), songs, songs))
    }

    @Test
    fun personalPicksMixSourcesInsteadOfOnlyShowingRecentHistory() {
        val liked = (1..6).map { song("like$it") }
        val played = (1..6).map { song("played$it") }
        val recent = (1..6).map { song("recent$it") }
        val discovery = (1..6).map { song("new$it") }
        val result = TvHomeSelector.select(liked, played, recent, discovery)
        for (source in listOf(liked, played, recent, discovery)) {
            assertTrue(result.personal.any { pick -> source.any { it.id == pick.id } })
        }
    }

    @Test
    fun radioDoesNotRepeatArtistsEvenWithDifferentIdsAndCapitalization() {
        val songs = (1..18).map { song("$it", if (it % 2 == 0) " ARTIST  Name " else "artist name") }
        val result = TvHomeSelector.select(songs, emptyList(), emptyList(), emptyList())
        assertEquals(1, result.radio.size)
        assertEquals(17, result.personal.size)
    }

    @Test
    fun tinyLibraryKeepsPersonalPicksWithoutDuplicatingForRadio() {
        val songs = listOf(song("1"), song("2"))
        val result = TvHomeSelector.select(songs, songs, songs, emptyList())
        assertEquals(songs, result.personal)
        assertTrue(result.radio.isEmpty())
        val larger = TvHomeSelector.select(songs + song("3"), emptyList(), emptyList(), emptyList())
        assertEquals(2, larger.personal.size)
        assertEquals(1, larger.radio.size)
    }

    @Test
    fun emptyLibraryHasNoInventedPicks() {
        val result = TvHomeSelector.select(emptyList(), emptyList(), emptyList(), emptyList())
        assertTrue(result.personal.isEmpty())
        assertTrue(result.radio.isEmpty())
    }

    @Test
    fun songsWithoutKnownArtistsAreNotRadioSeeds() {
        val songs = (1..6).map { song("$it").copy(artists = emptyList()) }
        val result = TvHomeSelector.select(songs, emptyList(), emptyList(), emptyList())
        assertTrue(result.radio.isEmpty())
        assertEquals(6, result.personal.size)
    }
}
