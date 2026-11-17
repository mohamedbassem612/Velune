package com.nikhil.yt.utils

import com.nikhil.yt.db.MusicDatabase
import com.nikhil.yt.db.entities.Song
import com.nikhil.yt.innertube.YouTube
import com.nikhil.yt.innertube.models.SongItem
import com.nikhil.yt.innertube.models.WatchEndpoint
import com.nikhil.yt.innertube.models.filterExplicit
import com.nikhil.yt.innertube.models.filterVideo
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.supervisorScope
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withTimeoutOrNull
import java.time.ZoneId
import javax.inject.Inject

/** Bounded candidate retrieval; a failed chart/seed never discards other sources. */
class QuickPicksRecommendationEngine @Inject constructor(private val database: MusicDatabase) {
    private suspend fun profile(lastListenOnly: Boolean = false): QuickPicksRecommender.Profile {
        val zone = ZoneId.systemDefault()
        val events = database.recentEvents(2000).first()
        val likes = database.likedSongsByRowIdAsc().first().asReversed()
        return QuickPicksRecommender.profile(
            listens = events.map {
                QuickPicksRecommender.Listen(it.song.track(), it.event.timestamp.atZone(zone).toInstant().toEpochMilli(), it.event.playTime)
            },
            likes = likes.map { it.track() },
            skips = database.getAllSkips().first().associate { it.songId to QuickPicksRecommender.Skip(it.skipCount, it.lastSkippedAt) },
            now = System.currentTimeMillis(),
            lastListenOnly = lastListenOnly,
            excludedSeedIds = (events.map { it.song } + likes).filter { it.song.isLocal }.map { it.id }.toSet(),
        )
    }

    suspend fun recommend(
        hideExplicit: Boolean,
        hideVideo: Boolean,
        lastListenOnly: Boolean,
        shownIds: Set<String>,
    ): List<SongItem> = supervisorScope {
        val profile = profile(lastListenOnly)
        val permits = Semaphore(3)
        val charts = async {
            fetchWithin(4_000) {
                YouTube.getChartsPage().getOrNull()?.sections.orEmpty()
                    .flatMap { section -> section.items.filterIsInstance<SongItem>().mapIndexed { index, song ->
                        song to (1.0 / (1.0 + (song.chartPosition?.minus(1)?.coerceAtLeast(0) ?: index) / 15.0))
                    } }
            }.orEmpty()
        }
        val neighbours = profile.seeds.map { seed ->
            async {
                permits.withPermit {
                    val songs = fetchWithin(3_500) {
                        val endpoint = YouTube.next(WatchEndpoint(videoId = seed.track.id)).getOrNull()?.relatedEndpoint
                        endpoint?.let { YouTube.related(it).getOrNull()?.songs }.orEmpty()
                    }.orEmpty().distinctBy { it.id }.take(60)
                    seed to songs
                }
            }
        }.awaitAll()
        val candidates = mutableListOf<QuickPicksRecommender.Candidate>()
        val songsById = linkedMapOf<String, SongItem>()
        fun add(song: SongItem, candidate: QuickPicksRecommender.Candidate) {
            if (listOf(song).filterExplicit(hideExplicit).filterVideo(hideVideo).isEmpty()) return
            songsById.putIfAbsent(song.id, song)
            candidates += candidate
        }
        neighbours.forEach { (seed, songs) ->
            songs.forEachIndexed { index, song ->
                add(song, QuickPicksRecommender.Candidate(song.track(),
                    related = mapOf(seed.track.id to seed.weight * (0.65 + 0.35 / (1 + index / 10.0)))))
            }
        }
        charts.await().forEach { (song, trend) ->
            add(song, QuickPicksRecommender.Candidate(song.track(), trend = trend))
        }
        QuickPicksRecommender.rank(candidates, profile, System.currentTimeMillis(), shownIds)
            .mapNotNull { songsById[it.id] }
    }

    suspend fun localFallback(hideExplicit: Boolean): List<Song> {
        val profile = profile()
        val songs = (database.quickPicks().first() + database.mostPlayedSongs(
            System.currentTimeMillis() - 90 * 86_400_000L, limit = 100,
        ).first() + database.likedSongsByRowIdAsc().first().takeLast(100))
            .distinctBy { it.id }.filter { !hideExplicit || !it.song.explicit }
        val byId = songs.associateBy { it.id }
        return QuickPicksRecommender.rank(songs.map { QuickPicksRecommender.Candidate(it.track()) },
            profile, System.currentTimeMillis(), discovery = false).mapNotNull { byId[it.id] }
    }

    private suspend fun <T> fetchWithin(timeoutMs: Long, block: suspend () -> T): T? =
        withTimeoutOrNull(timeoutMs) {
            try {
                block().also { currentCoroutineContext().ensureActive() }
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                null
            }
        }

    private fun Song.track() = QuickPicksRecommender.Track(id, title,
        artists.map { QuickPicksRecommender.artistKey(it.name) }.filter { it.isNotBlank() }.toSet(), song.duration, song.albumId)

    private fun SongItem.track() = QuickPicksRecommender.Track(id, title,
        artists.map { QuickPicksRecommender.artistKey(it.name) }.filter { it.isNotBlank() }.toSet(), duration ?: 0, album?.id)
}
