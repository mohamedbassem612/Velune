package com.nikhil.yt.utils

import org.junit.Assert.*
import org.junit.Test

class RadioQueuePolicyTest {
    private val now = 1_800_000_000_000L
    private fun track(id: String, artist: String = id) = QuickPicksRecommender.Track(id, id, setOf(artist), 200)
    private fun taste() = QuickPicksRecommender.profile(
        (1..15).map { QuickPicksRecommender.Listen(track("old$it", "usual"), now, 200_000) },
        listOf(track("liked", "usual")), emptyMap(), now,
    )

    @Test fun songRadioIsDetectedWithoutReorderingOrdinaryPlaylistsOrAlbums() {
        assertTrue(RadioQueuePolicy.isRadio("song", null))
        assertTrue(RadioQueuePolicy.isRadio("song", "RDAMVMsong"))
        assertTrue(RadioQueuePolicy.isRadio(null, "RDCLAKradio"))
        assertFalse(RadioQueuePolicy.isRadio("song", "PLuser-list"))
        assertFalse(RadioQueuePolicy.isRadio(null, "OLAKalbum"))
        assertFalse(RadioQueuePolicy.isRadio(null, null))
    }

    @Test fun selectedSongLeadsTheSessionEvenOutsideUsualTaste() {
        val current = track("current", "new-mood")
        val profile = RadioQueuePolicy.context(taste(), current)
        assertEquals(current, profile.seeds.first().track)
        assertEquals(1.0, profile.seeds.first().weight, 0.0)
        assertTrue(profile.artistAffinity.getValue("new-mood") >= 0.75)
        assertTrue(profile.artistAffinity.getValue("usual") > 0)
        assertTrue(profile.seeds.size <= 3)
        assertTrue(profile.seeds.drop(1).all { it.weight <= 0.5 })
    }

    @Test fun favoriteSongsAreAllowedInRadioAndRejectedSkipsStillMatter() {
        val taste = taste()
        val profile = RadioQueuePolicy.context(taste, track("current"))
            .copy(skipPenalty = mapOf("rejected" to 0.8))
        val candidates = listOf(
            QuickPicksRecommender.Candidate(track("liked", "usual"), mapOf("current" to 0.8)),
            QuickPicksRecommender.Candidate(track("rejected", "usual"), mapOf("current" to 1.0)),
        )
        assertEquals(listOf("liked"), QuickPicksRecommender.rank(candidates, profile, now, discovery = false).map { it.id })
        assertTrue(QuickPicksRecommender.rank(candidates, profile, now, discovery = true).isEmpty())
    }

    @Test fun currentTrackAndQueuedSongsAreExcludedWithoutDroppingTheFirstFreshSong() {
        val items = listOf("current", "first-new", "queued", "second-new", "first-new")
        assertEquals(listOf("first-new", "second-new"),
            RadioQueuePolicy.unseen(items, setOf("current", "queued"), { it }))
    }

    @Test fun oldResponsesCannotChangeAReplacementQueueOrADifferentCurrentSong() {
        assertTrue(RadioQueuePolicy.isCurrent(1, 1, "seed", "seed"))
        assertFalse(RadioQueuePolicy.isCurrent(1, 2, "seed", "seed"))
        assertFalse(RadioQueuePolicy.isCurrent(1, 1, "seed", "different"))
        assertFalse(RadioQueuePolicy.isCurrent(1, 1, "seed", null))
    }

    @Test fun radioCanStartWithoutAnyAccountOrListeningHistory() {
        val empty = QuickPicksRecommender.profile(emptyList(), emptyList(), emptyMap(), now)
        val profile = RadioQueuePolicy.context(empty, track("seed", "artist"))
        val candidate = QuickPicksRecommender.Candidate(track("next", "artist"), mapOf("seed" to 0.8))
        assertEquals("next", QuickPicksRecommender.rank(listOf(candidate), profile, now, discovery = false).single().id)
    }
}
