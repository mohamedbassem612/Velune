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

    @Test fun dislikeOnlyExcludesTheSelectedRecording() {
        val seed = c("seed", "Intro Acoustic", "A")
        val disliked = c("disliked", "Session Acoustic", "A")
        val anotherRecording = c("live", "Session Acoustic (Live)", "A")
        val sameArtist = c("artist", "Morning Acoustic", "A")
        val sameStyle = c("style", "Night Unplugged", "B")
        val pool = listOf(disliked, anotherRecording, sameArtist, sameStyle)
        val blocks = AutomixBlocklist(historyIds = setOf(disliked.id), dislikedIds = setOf(disliked.id))
        val expected = AutomixFilter.refine(pool.drop(1), blocks, seed = seed)

        assertEquals(setOf("live", "artist", "style"), expected.toSet())
        assertEquals(expected, AutomixFilter.refine(pool, blocks, seed = seed))
        assertTrue("disliked" in AutomixFilter.refine(pool, AutomixBlocklist(), seed = seed))
    }

    @Test fun renewedLikeOverridesDislikeAndRaisesTheRecordingScore() {
        val seed = c("seed", "Intro", "A")
        val neutral = c("neutral", "Morning", "B")
        val likedAgain = c("returned", "Night", "C")
        val pool = listOf(neutral, likedAgain)
        val disliked = AutomixBlocklist(dislikedIds = setOf(likedAgain.id))

        assertEquals(listOf(neutral.id), AutomixFilter.refine(pool, disliked, seed = seed))
        assertEquals(
            listOf(likedAgain.id, neutral.id),
            AutomixFilter.refine(pool, disliked.copy(likedIds = setOf(likedAgain.id)), seed = seed),
        )
    }

    @Test fun recitationSeedRanksReciterAndRejectsSongsAndLectures() {
        val seed = c("seed", "سُـورَةُ الْكَهْفِ", "الحصري")
        val pool = listOf(
            c("song", "ليلة", "الحصري"),
            c("lecture", "تفسير سورة الكهف", "واعظ"),
            c("other", "Surah Maryam", "Another reciter"),
            c("same", "البقرة", "الحصري"),
            seed.copy(id = "copy"),
        )
        assertEquals(listOf("same", "other"), AutomixFilter.refine(pool, AutomixBlocklist(), seed = seed))
    }

    @Test fun recitationSeedFallsBackWhenProviderHasNoRecognizableRecitations() {
        val seed = c("seed", "سورة الكهف", "الحصري")
        val pool = listOf(c("one", "Morning", "Artist A"), c("two", "Night", "Artist B"))

        assertEquals(listOf("one", "two"), AutomixFilter.refine(pool, AutomixBlocklist(), seed = seed))
    }

    @Test fun ambiguousChapterNeedsReciterEvidence() {
        val pool = listOf(c("song", "حبيبي", "مطرب"), c("recitation", "سورة الفجر", "قارئ"))
        val title = c("seed", "مريم", "محمد صديق المنشاوي")
        assertEquals(listOf("recitation"), AutomixFilter.refine(pool, AutomixBlocklist(), seed = title))
        assertEquals(listOf("song", "recitation"), AutomixFilter.refine(pool, AutomixBlocklist(), seed = title.copy(artist = "مطرب")))
    }

    @Test fun albumCanIdentifyRecitationAndFallsBackWhenPoolHasNoRecitationMarker() {
        val seed = c("seed", "001", null).copy(album = "The Holy Qur’an")
        val song = c("song", "تملي معاك", "عمرو دياب")
        assertEquals(listOf("song"), AutomixFilter.refine(listOf(song), AutomixBlocklist(), seed = seed))
        val chapter = c("next", "002", null).copy(album = "القرآن الكريم")
        assertEquals(listOf("next"), AutomixFilter.refine(listOf(song, chapter), AutomixBlocklist(), seed = seed))
    }

    @Test fun musicUsesAlbumArtistAndStyleWithProviderOrderForTies() {
        val seed = c("s", "Morning (Acoustic)", "A").copy(album = "Sessions")
        val pool = listOf(
            c("x", "First", null), c("y", "Second", null), c("style", "Night (Unplugged)", "B"),
            c("artist", "Another", "A"), c("album", "Last", "A").copy(album = "Sessions"),
        )
        assertEquals(listOf("album", "artist", "style", "x", "y"), AutomixFilter.refine(pool, AutomixBlocklist(), seed = seed))
    }
}
