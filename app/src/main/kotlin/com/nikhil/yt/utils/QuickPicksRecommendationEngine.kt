package com.nikhil.yt.utils

import com.nikhil.yt.db.MusicDatabase
import com.nikhil.yt.db.entities.Song
import com.nikhil.yt.models.MediaMetadata
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
import javax.inject.Singleton
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock

/** Bounded candidate retrieval; a failed chart/seed never discards other sources. */
@Singleton
class QuickPicksRecommendationEngine @Inject constructor(private val database: MusicDatabase) {
    private val chartsMutex = Mutex()
    private data class ChartsSnapshot(val account: String, val savedAt: Long, val songs: List<Pair<SongItem, Double>>)
    private var chartsSnapshot: ChartsSnapshot? = null

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
        radioSeed: MediaMetadata? = null,
        radioCandidates: List<SongItem> = emptyList(),
        excludedIds: Set<String> = emptySet(),
    ): List<SongItem> = supervisorScope {
        val profile = profile(lastListenOnly).let { taste ->
            if (radioSeed == null) taste else RadioQueuePolicy.context(taste, radioSeed.track())
        }
        val permits = Semaphore(3)
        val charts = async {
            fetchWithin(4_000) { chartSongs() }.orEmpty()
        }
        val retrievalSeeds = profile.seeds.filterNot {
            it.track.id == radioSeed?.id && radioCandidates.isNotEmpty()
        }
        val neighbours = retrievalSeeds.map { seed ->
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
            if (song.id in excludedIds || song.id == radioSeed?.id) return
            if (listOf(song).filterExplicit(hideExplicit).filterVideo(hideVideo).isEmpty()) return
            songsById.putIfAbsent(song.id, song)
            candidates += candidate
        }
        // The current song's YouTube radio is useful evidence, but our policy chooses the order.
        radioSeed?.let { seed ->
            radioCandidates.distinctBy { it.id }.forEachIndexed { index, song ->
                add(song, QuickPicksRecommender.Candidate(song.track(), related = mapOf(
                    seed.id to (0.85 / (1 + index / 40.0)),
                )))
            }
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
        QuickPicksRecommender.rank(candidates, profile, System.currentTimeMillis(), shownIds,
            discovery = radioSeed == null)
            .mapNotNull { songsById[it.id] }
    }

    private suspend fun chartSongs(): List<Pair<SongItem, Double>> = chartsMutex.withLock {
        val now = System.currentTimeMillis()
        val account = YouTube.cookie.orEmpty() + YouTube.locale.toString()
        chartsSnapshot?.takeIf { it.account == account && now - it.savedAt in 0..(15 * 60 * 1000L) }
            ?.let { return@withLock it.songs }
        val songs = YouTube.getChartsPage().getOrNull()?.sections.orEmpty().flatMap { section ->
            section.items.filterIsInstance<SongItem>().mapIndexed { index, song ->
                song to (1.0 / (1.0 + (song.chartPosition?.minus(1)?.coerceAtLeast(0) ?: index) / 15.0))
            }
        }
        if (songs.isNotEmpty()) chartsSnapshot = ChartsSnapshot(account, now, songs)
        songs
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

    private fun MediaMetadata.track() = QuickPicksRecommender.Track(id, title,
        artists.map { QuickPicksRecommender.artistKey(it.name) }.filter { it.isNotBlank() }.toSet(), duration, album?.id)
}
