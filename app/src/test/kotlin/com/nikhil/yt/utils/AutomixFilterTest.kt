package com.nikhil.yt.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

class AutomixFilterTest {
    private fun c(id: String, title: String, artist: String?) = AutomixCandidate(id, title, artist)

    @Test fun otherVersionsOfQueuedSongAreDropped() {
        val queueKeys = setOf(AutomixFilter.songKey("Blinding Lights", "The Weeknd"))
        val result =
            AutomixFilter.refine(
                listOf(
                    c("a", "Blinding Lights (Official Video)", "The Weeknd"),
                    c("b", "Blinding Lights - Live", "The Weeknd"),
                    c("c", "Save Your Tears", "The Weeknd"),
                ),
                AutomixBlocklist(queueKeys = queueKeys),
            )
        assertEquals(listOf("c"), result)
    }

    @Test fun duplicatesInsidePoolKeepFirstOnly() {
        val result =
            AutomixFilter.refine(
                listOf(c("a", "Song", "X"), c("b", "Song (Lyrics)", "X"), c("a", "Song", "X")),
                AutomixBlocklist(),
            )
        assertEquals(listOf("a"), result)
    }

    @Test fun recentlyPlayedAreDroppedWhenEnoughFreshRemain() {
        val fresh = (1..9).map { c("f$it", "Fresh $it", "Artist $it") }
        val result =
            AutomixFilter.refine(
                listOf(c("old", "Old", "O")) + fresh,
                AutomixBlocklist(historyIds = setOf("old")),
            )
        assertTrue("old" !in result)
        assertEquals(9, result.size)
    }

    @Test fun recentlyPlayedComeBackLastWhenTooFewFresh() {
        val result =
            AutomixFilter.refine(
                listOf(c("old", "Old", "O"), c("n", "New", "N")),
                AutomixBlocklist(historyIds = setOf("old")),
            )
        assertEquals(listOf("n", "old"), result)
    }

    @Test fun sameArtistNeverMoreThanTwoInARowWhenOthersExist() {
        val input =
            (1..6).map { c("a$it", "A song $it", "Same") } + (1..6).map { c("b$it", "B song $it", "Other$it") }
        val ids = AutomixFilter.refine(input, AutomixBlocklist())
        assertEquals(input.size, ids.size)
        val artistOf = input.associate { it.id to it.artist }
        for (i in 2 until ids.size) {
            val three = listOf(ids[i - 2], ids[i - 1], ids[i]).map { artistOf[it] }
            assertTrue(three.toSet().size > 1 || three[0] != "Same")
        }
    }

    @Test fun nothingIsDroppedBySpreading() {
        val input = (1..10).map { c("a$it", "Song $it", "Only") }
        assertEquals(input.map { it.id }, AutomixFilter.refine(input, AutomixBlocklist()))
    }

    @Test fun arabicTitlesStayDistinct() {
        val result =
            AutomixFilter.refine(
                listOf(c("1", "قصة حب", "عمرو دياب"), c("2", "تملي معاك", "عمرو دياب")),
                AutomixBlocklist(),
            )
        assertEquals(listOf("1", "2"), result)
    }
}
