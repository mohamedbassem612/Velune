package com.nikhil.yt.utils

import com.nikhil.yt.utils.QuickPicksRecommender.Candidate
import com.nikhil.yt.utils.QuickPicksRecommender.Listen
import com.nikhil.yt.utils.QuickPicksRecommender.Skip
import com.nikhil.yt.utils.QuickPicksRecommender.Track
import org.junit.Assert.*
import org.junit.Test

class QuickPicksRecommenderTest {
    private val now = 1_800_000_000_000L
    private val day = 86_400_000L
    private fun track(id: String, artist: String = id, title: String = id) =
        Track(id, title, setOf(artist), durationSeconds = 200)
    private fun listen(track: Track, age: Long = 0, played: Long = 200_000) = Listen(track, now - age, played)
    private fun profile(listens: List<Listen> = emptyList(), likes: List<Track> = emptyList(), skips: Map<String, Skip> = emptyMap()) =
        QuickPicksRecommender.profile(listens, likes, skips, now)

    @Test fun completedListeningOutweighsLargeUnplayedLikeCollection() {
        val history = (1..20).map { listen(track("played$it", "chosen")) }
        val likes = (1..300).map { track("liked$it", "imported") }
        val profile = profile(history, likes)
        assertTrue(profile.artistAffinity.getValue("chosen") > profile.artistAffinity.getValue("imported"))
        assertTrue(profile.seeds.any { "chosen" in it.track.artists })
    }

    @Test fun partialListeningIsWeakerThanFinishingSongs() {
        val profile = profile(listOf(listen(track("full"), played = 200_000), listen(track("partial"), played = 30_000)))
        assertTrue(profile.songStrength.getValue("full") > profile.songStrength.getValue("partial") * 5)
    }

    @Test fun recentTasteCanOvertakeOldTaste() {
        val profile = profile(listOf(listen(track("old"), age = 75 * day), listen(track("new"))))
        assertTrue(profile.artistAffinity.getValue("new") > profile.artistAffinity.getValue("old"))
    }

    @Test fun loopingAndLongVideosCannotProduceUnboundedAffinity() {
        val song = track("loop")
        val once = profile(listOf(listen(song, played = 900_000_000)))
        val loop = profile(List(1000) { listen(song) })
        assertTrue(loop.songStrength.getValue("loop") <= once.songStrength.getValue("loop") * 2.01)
        assertTrue(loop.artistAffinity.values.all { it in 0.0..1.0 })
    }

    @Test fun seedsAreDeterministicAndIncludeMultipleArtists() {
        val songs = (1..15).map { track("a$it", "same") } + track("b", "different")
        val profile = profile(songs.map { listen(it) })
        assertTrue(profile.seeds.count { "same" in it.track.artists } <= 2)
        assertTrue(profile.seeds.any { "different" in it.track.artists })
        assertEquals(profile.seeds, profile(songs.reversed().map { listen(it) }).seeds)
    }

    @Test fun lastListenModeUsesOnlyLatestQualifiedSong() {
        val old = track("old")
        val latest = track("latest")
        val profile = QuickPicksRecommender.profile(listOf(listen(old, day), listen(latest)),
            listOf(track("liked")), emptyMap(), now, lastListenOnly = true)
        assertEquals(listOf("latest"), profile.seeds.map { it.track.id })
    }

    @Test fun localSeedsDoNotConsumeNetworkSeedSlotsButStillTeachTaste() {
        val local = track("local", "favorite")
        val remote = track("remote", "favorite")
        val profile = QuickPicksRecommender.profile(List(10) { listen(local) } + listen(remote),
            emptyList(), emptyMap(), now, excludedSeedIds = setOf("local"))
        assertEquals(listOf("remote"), profile.seeds.map { it.track.id })
        assertTrue(profile.artistAffinity.getValue("favorite") > 0)
    }

    @Test fun momentaryListeningDoesNotLockColdStartToAnUnreliableProfile() {
        val profile = profile(listOf(listen(track("accidental"), played = 1000)))
        assertFalse(profile.hasTaste)
        assertEquals("trend", QuickPicksRecommender.rank(
            listOf(Candidate(track("trend"), trend = 1.0)), profile, now).single().id)
    }

    @Test fun unrelatedNumberOneTrendingSongCannotOverrideTaste() {
        val profile = profile(listOf(listen(track("seed", "favorite"))))
        val candidates = listOf(Candidate(track("viral", "unrelated"), trend = 1.0),
            Candidate(track("fits", "favorite"), trend = 0.1))
        assertEquals(listOf("fits"), QuickPicksRecommender.rank(candidates, profile, now).map { it.id })
    }

    @Test fun chartsBoostRelevantSongsAndColdStartWorks() {
        val profile = profile(listOf(listen(track("seed", "favorite"))))
        val candidates = listOf(Candidate(track("plain", "favorite")), Candidate(track("trend", "favorite"), trend = 1.0))
        assertEquals("trend", QuickPicksRecommender.rank(candidates, profile, now).first().id)
        assertEquals("trend", QuickPicksRecommender.rank(candidates, profile(), now).first().id)
    }

    @Test fun independentNeighboursImproveConsensusWithoutDuplicateVotes() {
        val profile = profile(likes = listOf(track("seed")))
        val one = Candidate(track("single"), mapOf("s1" to 0.7))
        val multiple = Candidate(track("consensus"), mapOf("s1" to 0.7, "s2" to 0.6))
        val duplicates = List(20) { one }
        val ranked = QuickPicksRecommender.rank(duplicates + multiple, profile, now)
        assertEquals(listOf("consensus", "single"), ranked.map { it.id })
    }

    @Test fun strongGraphEvidenceDiscoversUnfamiliarArtists() {
        val profile = profile(likes = listOf(track("seed", "known")))
        val fresh = Candidate(track("fresh", "unknown"), mapOf("seed" to 0.8))
        assertEquals("fresh", QuickPicksRecommender.rank(listOf(fresh), profile, now).single().id)
    }

    @Test fun repeatedRecentSkipsExcludeSongButNotItsEntireArtist() {
        val disliked = track("disliked", "artist")
        val profile = profile(likes = listOf(track("seed", "artist")), skips = mapOf(disliked.id to Skip(4, now)))
        val ranked = QuickPicksRecommender.rank(listOf(Candidate(disliked, trend = 1.0), Candidate(track("other", "artist"))), profile, now)
        assertEquals(listOf("other"), ranked.map { it.id })
    }

    @Test fun oldSkipsDecayAndSuccessfulListeningCanRecover() {
        val song = track("song")
        val oldSkip = profile(skips = mapOf(song.id to Skip(4, now - 90 * day)))
        val recovered = profile(List(10) { listen(song) }, skips = mapOf(song.id to Skip(2, now)))
        assertTrue(oldSkip.skipPenalty.getValue(song.id) < 0.02)
        assertTrue(recovered.skipPenalty.getValue(song.id) < 0.2)
    }

    @Test fun discoveryExcludesLikesAndRecentPlaysButLocalFallbackCanUseThem() {
        val heard = track("heard", "a")
        val liked = track("liked", "a")
        val profile = profile(listOf(listen(heard)), listOf(liked))
        val candidates = listOf(heard, liked, track("new", "a")).map { Candidate(it) }
        assertEquals(listOf("new"), QuickPicksRecommender.rank(candidates, profile, now).map { it.id })
        assertEquals(3, QuickPicksRecommender.rank(candidates, profile, now, discovery = false).size)
    }

    @Test fun diversityCountsFeaturedArtistsAndRemovesAlternateUploads() {
        val profile = profile()
        val candidates = (1..20).map { Candidate(track("id$it", "same").copy(artists = setOf("same", "featured$it")), trend = 1.0) } +
            Candidate(track("original", "other", "Song"), trend = 1.0) +
            Candidate(track("upload", "other", "Song (Official Video)"), trend = 1.0)
        val ranked = QuickPicksRecommender.rank(candidates, profile, now)
        assertEquals(3, ranked.count { "same" in it.artists })
        assertEquals(1, ranked.count { "other" in it.artists })
    }

    @Test fun shownSongsGetSoftPenaltyWithoutDiscardingBestMatches() {
        val profile = profile(likes = listOf(track("seed", "artist")))
        val candidates = listOf(Candidate(track("a", "artist")), Candidate(track("b", "artist")))
        assertEquals("b", QuickPicksRecommender.rank(candidates, profile, now, setOf("a")).first().id)
        assertEquals(2, QuickPicksRecommender.rank(candidates, profile, now, setOf("a", "b")).size)
    }

    @Test fun artistKeysAreUnicodeAndLocaleStable() {
        assertEquals("عمرو دياب", QuickPicksRecommender.artistKey("  عمرو   دياب  "))
        assertEquals("artist", QuickPicksRecommender.artistKey("ＡＲＴＩＳＴ"))
    }

    @Test fun emptyInputsAndNonPositiveLimitsAreSafe() {
        assertTrue(QuickPicksRecommender.rank(emptyList(), profile(), now).isEmpty())
        assertTrue(QuickPicksRecommender.rank(listOf(Candidate(track("a"))), profile(), now, limit = -1).isEmpty())
        assertTrue(profile().seeds.isEmpty())
    }

    @Test fun skipFeedbackRequiresMeaningfulEarlyPlayback() {
        assertTrue(QuickPicksRecommender.isEarlySkip(10_000, 200_000))
        assertFalse(QuickPicksRecommender.isEarlySkip(0, 200_000))
        assertFalse(QuickPicksRecommender.isEarlySkip(180_000, 200_000))
        assertFalse(QuickPicksRecommender.isEarlySkip(10_000, -1))
        assertFalse(QuickPicksRecommender.isEarlySkip(20_000, 30_000))
    }
}
