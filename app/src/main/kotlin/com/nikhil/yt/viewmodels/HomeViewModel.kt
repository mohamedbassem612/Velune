/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */



package com.nikhil.yt.viewmodels

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nikhil.yt.innertube.YouTube
import com.nikhil.yt.innertube.models.PlaylistItem
import com.nikhil.yt.innertube.models.SongItem
import com.nikhil.yt.innertube.models.WatchEndpoint
import com.nikhil.yt.innertube.models.YTItem
import com.nikhil.yt.innertube.models.filterExplicit
import com.nikhil.yt.innertube.models.filterVideo
import com.nikhil.yt.innertube.pages.ExplorePage
import com.nikhil.yt.innertube.pages.HomePage
import com.nikhil.yt.innertube.utils.completed
import com.nikhil.yt.innertube.utils.parseCookieString
import com.nikhil.yt.constants.HideExplicitKey
import com.nikhil.yt.constants.HideVideoKey
import com.nikhil.yt.constants.InnerTubeCookieKey
import com.nikhil.yt.constants.QuickPicks
import com.nikhil.yt.constants.QuickPicksKey
import com.nikhil.yt.constants.YtmSyncKey
import com.nikhil.yt.db.MusicDatabase
import com.nikhil.yt.db.entities.*
import com.nikhil.yt.extensions.toEnum
import com.nikhil.yt.models.SimilarRecommendation
import com.nikhil.yt.utils.dataStore
import com.nikhil.yt.utils.get
import com.nikhil.yt.utils.SyncUtils
import com.nikhil.yt.utils.reportException
import com.nikhil.yt.ui.utils.resize
import com.nikhil.yt.utils.QuickPicksCache
import com.nikhil.yt.innertube.toHighResThumbnail
import dagger.hilt.android.lifecycle.HiltViewModel
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.awaitAll
import kotlinx.coroutines.flow.*
import kotlinx.coroutines.launch
import kotlinx.coroutines.supervisorScope
import timber.log.Timber
import androidx.media3.datasource.cache.Cache
import com.nikhil.yt.di.DownloadCache
import com.nikhil.yt.di.PlayerCache
import com.nikhil.yt.extensions.filterExplicit as filterExplicitSongs
import com.nikhil.yt.utils.isOnlineNow
import com.nikhil.yt.utils.onlineFlow
import javax.inject.Inject

@HiltViewModel
class HomeViewModel @Inject constructor(
    @ApplicationContext val context: Context,
    val database: MusicDatabase,
    val syncUtils: SyncUtils,
    val forYouEngine: com.nikhil.yt.utils.ForYouSuggestionEngine,
    @PlayerCache private val playerCache: Cache,
    @DownloadCache private val downloadCache: Cache,
    private val mp3Library: com.nikhil.yt.playback.Mp3Library,
) : ViewModel() {
    // False while the phone has no working internet. The Home then shows offline mode.
    val isOnline = MutableStateFlow(context.isOnlineNow())

    val isRefreshing = MutableStateFlow(false)
    val isLoading = MutableStateFlow(false)
    private val isInitialLoadComplete = MutableStateFlow(false)
    val forYouSuggestions = MutableStateFlow<List<com.nikhil.yt.innertube.models.SongItem>?>(null)

    private val quickPicksEnum = context.dataStore.data.map {
        it[QuickPicksKey].toEnum(QuickPicks.QUICK_PICKS)
    }.distinctUntilChanged()

    // Songs shown in the carousel: what was played lately.
    val quickPicks = MutableStateFlow<List<Song>?>(null)
    // Quick picks list: new songs picked from the mood of the songs played lately.
    // null = still loading, empty = nothing found.
    val moodQuickPicks = MutableStateFlow<List<SongItem>?>(null)
    // Used by the Quick picks list when new songs cannot be loaded.
    val quickPicksFallback = MutableStateFlow<List<Song>?>(null)
    // True while the list on screen came from the disk cache. In that case the fresh result
    // is only saved for the next start, so the list does not change under the user's finger.
    private var moodShownFromCache = false
    private var likedSnapshot: List<Song>? = null
    private var likedSnapshotTime = 0L
    private var forceMoodRefresh = false
    @Volatile private var forceHomeRefresh = false
    @Volatile private var lastHomeFetchAt = 0L
    val forgottenFavorites = MutableStateFlow<List<Song>?>(null)
    val keepListening = MutableStateFlow<List<LocalItem>?>(null)
    val similarRecommendations = MutableStateFlow<List<SimilarRecommendation>?>(null)
    val accountPlaylists = MutableStateFlow<List<PlaylistItem>?>(null)
    val homePage = MutableStateFlow<HomePage?>(null)
    val explorePage = MutableStateFlow<ExplorePage?>(null)
    val selectedChip = MutableStateFlow<HomePage.Chip?>(null)
    private val previousHomePage = MutableStateFlow<HomePage?>(null)

    val recentActivity = MutableStateFlow<List<YTItem>?>(null)
    val recentPlaylistsDb = MutableStateFlow<List<Playlist>?>(null)

    val allLocalItems = MutableStateFlow<List<LocalItem>>(emptyList())
    val allYtItems = MutableStateFlow<List<YTItem>>(emptyList())

    // Account display info
    val accountName = MutableStateFlow<String?>(null)
    val accountImageUrl = MutableStateFlow<String?>(null)
    
    // Track last processed cookie to avoid unnecessary updates
    private var lastProcessedCookie: String? = null
    
    // Track if we're currently processing account data
    private var isProcessingAccountData = false
    private var wasLoggedIn = false

    private fun filterHomeChips(chips: List<HomePage.Chip>?): List<HomePage.Chip>? {
        return chips?.filterNot { it.title.contains("podcasts", ignoreCase = true) }
    }

    /** Liked songs, most recently liked first. Kept for a few seconds so one load reads them once. */
    private suspend fun likedSongsRecentFirst(): List<Song> {
        val now = System.currentTimeMillis()
        likedSnapshot?.let { if (now - likedSnapshotTime < 10_000) return it }
        val list = database.likedSongsByRowIdAsc().first().asReversed()
        likedSnapshot = list
        likedSnapshotTime = now
        return list
    }

    private companion object {
        const val HOME_FRESH_MS = 15 * 60 * 1000L
    }

    private suspend fun getQuickPicks() {
        val recent = database.recentEvents(60).first()
            .map { it.song }
            .distinctBy { it.id }
            .take(12)
        // The carousel is filled with liked songs when there are few recent plays.
        val recentIds = recent.map { it.id }.toHashSet()
        val likedFill = likedSongsRecentFirst()
            .filter { it.id !in recentIds }
            .take(12 - recent.size)
        val taste = buildTasteBasedQuickPicks()
        quickPicksFallback.value = taste
        quickPicks.value = (recent + likedFill).ifEmpty { taste }
        prefetchThumbnails(quickPicks.value.orEmpty().mapNotNull { it.song.thumbnailUrl?.toHighResThumbnail() })
    }

    // Loads covers into the Coil disk cache right away, so they also show when offline later.
    private fun prefetchThumbnails(urls: List<String>) {
        try {
            val loader = coil3.SingletonImageLoader.get(context)
            urls.distinct().forEach { url ->
                loader.enqueue(coil3.request.ImageRequest.Builder(context).data(url).build())
            }
        } catch (_: Exception) {
        }
    }

    private suspend fun loadMoodQuickPicks() {
        val result = try {
            kotlinx.coroutines.withTimeoutOrNull(10_000) { buildMoodQuickPicks() }
        } catch (e: Exception) {
            reportException(e)
            null
        }
        if (!result.isNullOrEmpty()) {
            QuickPicksCache.save(context, result)
            prefetchThumbnails(result.mapNotNull { it.thumbnail })
            if (!moodShownFromCache || forceMoodRefresh) {
                moodQuickPicks.value = result
                moodShownFromCache = false
            }
        } else if (moodQuickPicks.value == null) {
            moodQuickPicks.value = emptyList()
        }
        forceMoodRefresh = false
    }

    /**
     * When there is no listening history yet (a new install, or the app data was cleared),
     * fill the carousel and the Quick picks list from the YouTube Music home feed so the
     * Home screen looks the same as it does with history.
     */
    private fun fillStarterPicksIfNeeded() {
        if (!quickPicks.value.isNullOrEmpty()) return
        val songs = homePage.value?.sections.orEmpty()
            .flatMap { it.items }
            .filterIsInstance<SongItem>()
            .distinctBy { it.id }
        if (songs.size < 4) return

        val carousel = songs.take(12)
        quickPicks.value = carousel.map { it.toLocalSong() }
        if (moodQuickPicks.value.isNullOrEmpty()) {
            moodQuickPicks.value = songs.drop(carousel.size).ifEmpty { songs }.take(20)
            moodShownFromCache = false
        }
    }

    private fun SongItem.toLocalSong() = Song(
        song = SongEntity(
            id = id,
            title = title,
            duration = duration ?: -1,
            thumbnailUrl = thumbnail.resize(544, 544),
            albumId = album?.id,
            albumName = album?.name,
            explicit = explicit,
        ),
        artists = artists.map {
            ArtistEntity(id = it.id ?: ArtistEntity.generateArtistId(), name = it.name)
        },
    )

    /**
     * New songs for the current mood. The mood is the vibe of the songs played lately:
     * we ask YouTube Music for songs related to the last few songs played (the latest
     * counts the most), keep the ones that show up for several of them, and drop
     * everything already heard or liked.
     */
    private suspend fun buildMoodQuickPicks(): List<SongItem> {
        val hideExplicit = context.dataStore.get(HideExplicitKey, false)
        val hideVideo = context.dataStore.get(HideVideoKey, false)
        val lastSongOnly = quickPicksEnum.first() == QuickPicks.LAST_LISTEN

        val events = database.recentEvents(400).first()
        val liked = likedSongsRecentFirst()

        // The mood comes from the songs played lately and from liked songs.
        val recentSeeds = events.map { it.song }.distinctBy { it.id }.take(if (lastSongOnly) 1 else 3)
        val likedSeeds = if (lastSongOnly && recentSeeds.isNotEmpty()) {
            emptyList<Song>()
        } else {
            liked.filter { song -> recentSeeds.none { it.id == song.id } }
                .take(40)
                .shuffled()
                .take(if (recentSeeds.isEmpty()) 4 else 2)
        }
        val seeds = recentSeeds + likedSeeds
        if (seeds.isEmpty()) return emptyList()
        // Recent plays count the most (the latest first); liked seeds count a bit less.
        val seedWeights = recentSeeds.indices.map { 1f - it * 0.2f } + likedSeeds.map { 0.6f }

        // What was heard: rank 0 is the song played most recently.
        val heardRank = LinkedHashMap<String, Int>()
        events.forEach { heardRank.putIfAbsent(it.song.id, heardRank.size) }
        val heardCount = heardRank.size.coerceAtLeast(1).toFloat()
        val recentSongs = events.map { it.song }.distinctBy { it.id }.take(RECENT_EXCLUDE_COUNT)
        val recentIds = recentSongs.map { it.id }.toHashSet()
        val likedIds = liked.map { it.id }.toHashSet()
        val shownIds = moodQuickPicks.value.orEmpty().map { it.id }.toHashSet()

        val recentArtistCounts = HashMap<String, Int>()
        recentSongs.forEach { song ->
            song.artists.map { com.nikhil.yt.utils.QuickPicksRanker.normalizeArtist(it.name) }
                .distinct()
                .forEach { recentArtistCounts[it] = (recentArtistCounts[it] ?: 0) + 1 }
        }
        val affinity = buildArtistAffinity(liked)

        val related = kotlinx.coroutines.coroutineScope {
            seeds.mapIndexed { index, seed ->
                async {
                    val endpoint = YouTube.next(WatchEndpoint(videoId = seed.id))
                        .getOrNull()?.relatedEndpoint
                    val page = endpoint?.let { YouTube.related(it).getOrNull() }
                    index to page?.songs.orEmpty()
                }
            }.awaitAll()
        }

        // Mood score: a song gets points from every seed whose related list has it
        // (once per seed), more for recent seeds and for a higher place in the list.
        val moodScores = HashMap<String, Float>()
        val songsById = LinkedHashMap<String, SongItem>()
        for ((seedIndex, songs) in related) {
            val seedWeight = seedWeights[seedIndex]
            val distinctSongs = songs.distinctBy { it.id }
            distinctSongs.forEachIndexed { position, song ->
                // Liked songs and songs from the last plays are not "new".
                if (song.id in likedIds || song.id in recentIds) return@forEachIndexed
                val positionBonus = 1f - position.toFloat() / distinctSongs.size.coerceAtLeast(1)
                moodScores[song.id] = (moodScores[song.id] ?: 0f) + seedWeight * (0.7f + 0.3f * positionBonus)
                songsById.putIfAbsent(song.id, song)
            }
        }
        if (songsById.isEmpty()) return emptyList()
        val maxMood = (moodScores.values.maxOrNull() ?: 1f).coerceAtLeast(0.0001f)

        val random = kotlin.random.Random.Default
        val ranked = songsById.values.toList()
            .filterExplicit(hideExplicit)
            .filterVideo(hideVideo)
            .map { song ->
                // 70% current mood, 30% general taste.
                val moodScore = ((moodScores[song.id] ?: 0f) / maxMood).coerceIn(0f, 1f)
                val tasteScore = song.artists
                    .maxOfOrNull { affinity.of(it.id, it.name) }
                    ?.coerceIn(0f, 1f) ?: 0f
                // Heard before but not lately: allowed, with a smaller penalty the longer ago.
                val heardPenalty = heardRank[song.id]
                    ?.let { MAX_HEARD_PENALTY * (1f - it / heardCount) } ?: 0f
                val repeatedArtist = song.artists.maxOfOrNull {
                    recentArtistCounts[com.nikhil.yt.utils.QuickPicksRanker.normalizeArtist(it.name)] ?: 0
                } ?: 0
                val artistPenalty = 0.05f * minOf(repeatedArtist, 4)

                val score = 0.70f * moodScore + 0.30f * tasteScore - heardPenalty - artistPenalty +
                    random.nextFloat() * 0.03f
                song to score
            }
            .sortedByDescending { it.second }
            .map { it.first }

        // Do not repeat the list that is on screen, unless too few other songs are left.
        val fresh = ranked.filter { it.id !in shownIds }
        val pool = if (fresh.size >= 12) fresh else fresh + ranked.filter { it.id in shownIds }

        return com.nikhil.yt.utils.QuickPicksRanker.selectDiverse(
            sorted = pool,
            limit = 20,
            idOf = { it.id },
            artistKeys = { song ->
                song.artists.map { com.nikhil.yt.utils.QuickPicksRanker.normalizeArtist(it.name) }
            },
        )
    }

    /**
     * Artist taste from the listening history of the last 60 days, followed artists and
     * liked songs, as a score from 0 to 1 by artist id and by artist name.
     */
    private suspend fun buildArtistAffinity(liked: List<Song>): ArtistAffinity {
        val now = System.currentTimeMillis()
        val sixtyDays = 86400000L * 60
        val byId = HashMap<String, Float>()
        val byName = HashMap<String, Float>()

        fun put(id: String, name: String, weight: Float) {
            byId[id] = maxOf(byId[id] ?: 0f, weight)
            val key = com.nikhil.yt.utils.QuickPicksRanker.normalizeArtist(name)
            byName[key] = maxOf(byName[key] ?: 0f, weight)
        }

        val topArtists = database.mostPlayedArtists(now - sixtyDays, limit = 15).first()
        topArtists.forEachIndexed { index, artist ->
            val rankWeight = (topArtists.size - index).toFloat() / topArtists.size
            val followBonus = if (artist.artist.bookmarkedAt != null) 0.2f else 0f
            put(artist.id, artist.artist.name, (rankWeight + followBonus).coerceAtMost(1f))
        }

        // Artists of liked songs count too: the more likes, the stronger the taste.
        val likedPerArtist = HashMap<String, Pair<String, Int>>()
        liked.forEach { song ->
            song.artists.forEach { artist ->
                val count = (likedPerArtist[artist.id]?.second ?: 0) + 1
                likedPerArtist[artist.id] = artist.name to count
            }
        }
        val maxLiked = (likedPerArtist.values.maxOfOrNull { it.second } ?: 1).toFloat()
        likedPerArtist.forEach { (artistId, nameAndCount) ->
            put(artistId, nameAndCount.first, 0.3f + 0.7f * nameAndCount.second / maxLiked)
        }
        return ArtistAffinity(byId, byName)
    }

    /**
     * Quick picks that follow the listener's taste: related songs first, ranked by how much
     * the listener plays and likes their artists, without repeating what was played lately.
     */
    private suspend fun buildTasteBasedQuickPicks(): List<Song> {
        val now = System.currentTimeMillis()
        val sixtyDays = 86400000L * 60

        val liked = likedSongsRecentFirst()
        val related = database.quickPicks().first()
        // Related songs come first (they count as closest to recent listening), then
        // a sample of liked songs, then the most played ones when the library is small.
        var candidates = related + liked.take(60).shuffled().take(40)
        if (candidates.size < 24) {
            candidates = candidates + database.mostPlayedSongs(now - sixtyDays, limit = 30).first()
        }

        val affinity = buildArtistAffinity(liked)

        val recentSongs = database.recentEvents(100).first()
            .map { it.song }
            .distinctBy { it.id }
            .take(RECENT_EXCLUDE_COUNT)
        val recentArtistCounts = HashMap<String, Int>()
        recentSongs.forEach { song ->
            song.artists.map { it.id }.distinct()
                .forEach { recentArtistCounts[it] = (recentArtistCounts[it] ?: 0) + 1 }
        }

        return com.nikhil.yt.utils.QuickPicksRanker.rank(
            candidates = candidates,
            artistAffinity = affinity.byId,
            recentSongIds = recentSongs.map { it.id },
            recentArtistCounts = recentArtistCounts,
            limit = 20,
        )
    }

    // Offline mode: the top 5 most played songs go in the carousel and the Quick picks list is
    // filled with other songs that are downloaded or fully saved in the cache. Nothing else is shown.
    private suspend fun loadOfflineHome() {
        val hideExplicit = context.dataStore.get(HideExplicitKey, false)
        val mp3Ids = mp3Library.ids()
        val ids = (downloadCache.keys + playerCache.keys + mp3Ids).toList()
        val songs = ids.chunked(500).flatMap { database.getSongsByIds(it) }
        val playable = songs.filter { song ->
            val length = song.format?.contentLength ?: 0L
            mp3Library.playableUri(song.song.id) != null ||
                (length > 0 && (downloadCache.isCached(song.song.id, 0, length) || playerCache.isCached(song.song.id, 0, length)))
        }.filterExplicitSongs(hideExplicit)
        val ranked = playable.sortedByDescending { it.song.totalPlayTime }
        val header = ranked.take(5)
        quickPicks.value = header
        quickPicksFallback.value = ranked.drop(5).shuffled().take(20).ifEmpty { header }
        allLocalItems.value = ranked.take(25)
        allYtItems.value = emptyList()
    }

    private fun watchConnectivity() {
        viewModelScope.launch(Dispatchers.IO) {
            context.onlineFlow().collect { online ->
                if (isOnline.value == online) return@collect
                isOnline.value = online
                viewModelScope.launch(Dispatchers.IO) {
                    if (online) load() else loadOfflineHome()
                }
            }
        }
    }

    private suspend fun load() {
        if (isLoading.value) return
        isLoading.value = true

        if (!isOnline.value) {
            try {
                loadOfflineHome()
            } catch (e: Exception) {
                reportException(e)
            } finally {
                isInitialLoadComplete.value = true
                isLoading.value = false
            }
            return
        }

        viewModelScope.launch(Dispatchers.IO) { loadMoodQuickPicks() }

        try {
            supervisorScope {
                val hideExplicit = context.dataStore.get(HideExplicitKey, false)
                val hideVideo = context.dataStore.get(HideVideoKey, false)
                val fromTimeStamp = System.currentTimeMillis() - 86400000 * 7 * 2

                launch { getQuickPicks() }
                launch { forgottenFavorites.value = database.forgottenFavorites().first().shuffled().take(20) }
                launch {
                    val keepListeningSongs = database.mostPlayedSongs(fromTimeStamp, limit = 15, offset = 5)
                        .first().shuffled().take(10)
                    val keepListeningAlbums = database.mostPlayedAlbums(fromTimeStamp, limit = 8, offset = 2)
                        .first().filter { it.album.thumbnailUrl != null }.shuffled().take(5)
                    val keepListeningArtists = database.mostPlayedArtists(fromTimeStamp)
                        .first().filter { it.artist.isYouTubeArtist && it.artist.thumbnailUrl != null }
                        .shuffled().take(5)
                    keepListening.value = (keepListeningSongs + keepListeningAlbums + keepListeningArtists).shuffled()
                }

                launch {
                    val force = forceHomeRefresh
                    forceHomeRefresh = false
                    val now = System.currentTimeMillis()
                    var feedAgeMs: Long? = if (homePage.value != null) now - lastHomeFetchAt else null
                    // Show the saved feed right away, before the network answers.
                    if (!force && homePage.value == null) {
                        YouTube.cachedHome()?.let { (saved, savedAt) ->
                            homePage.value = saved.copy(
                                chips = filterHomeChips(saved.chips),
                                sections = saved.sections.map { section ->
                                    section.copy(items = section.items.filterExplicit(hideExplicit).filterVideo(hideVideo))
                                }
                            )
                            lastHomeFetchAt = savedAt
                            feedAgeMs = now - savedAt
                        }
                    }
                    // A recent feed is reused. Only an old one, or a pull to refresh, asks the network.
                    if (!force && feedAgeMs != null && feedAgeMs!! < HOME_FRESH_MS) return@launch
                    YouTube.home().onSuccess { page ->
                        lastHomeFetchAt = System.currentTimeMillis()
                        homePage.value = page.copy(
                            chips = filterHomeChips(page.chips),
                            sections = page.sections.map { section ->
                                section.copy(items = section.items.filterExplicit(hideExplicit).filterVideo(hideVideo))
                            }
                        )
                    }.onFailure { reportException(it) }
                }

                launch {
                    YouTube.explore().onSuccess { page ->
                        val artists: MutableMap<Int, String> = mutableMapOf()
                        val favouriteArtists: MutableMap<Int, String> = mutableMapOf()
                        database.allArtistsByPlayTime().first().let { list ->
                            var favIndex = 0
                            for ((artistsIndex, artist) in list.withIndex()) {
                                artists[artistsIndex] = artist.id
                                if (artist.artist.bookmarkedAt != null) {
                                    favouriteArtists[favIndex] = artist.id
                                    favIndex++
                                }
                            }
                        }
                        explorePage.value = page.copy(
                            newReleaseAlbums = page.newReleaseAlbums
                                .sortedBy { album ->
                                    val artistIds = album.artists.orEmpty().mapNotNull { it.id }
                                    val firstArtistKey = artistIds.firstNotNullOfOrNull { artistId ->
                                        if (artistId in favouriteArtists.values) {
                                            favouriteArtists.entries.firstOrNull { it.value == artistId }?.key
                                        } else {
                                            artists.entries.firstOrNull { it.value == artistId }?.key
                                        }
                                    } ?: Int.MAX_VALUE
                                    firstArtistKey
                                }.filterExplicit(hideExplicit)
                        )
                    }.onFailure { reportException(it) }
                }
            }

            fillStarterPicksIfNeeded()

            allLocalItems.value = (quickPicks.value.orEmpty() + forgottenFavorites.value.orEmpty() + keepListening.value.orEmpty())
                .filter { it is Song || it is Album }

            viewModelScope.launch(Dispatchers.IO) {
                // Extra suggestions are not needed to show the home feed, so they wait a moment.
                // This leaves the network free for the main feed and for a song the user taps first.
                kotlinx.coroutines.delay(1500)
                launch {
                    try {
                        val hideExplicit = context.dataStore.get(HideExplicitKey, false)
                        val hideVideo = context.dataStore.get(HideVideoKey, false)
                        forYouSuggestions.value = forYouEngine.getSuggestions(hideExplicit, hideVideo)
                    } catch (_: Exception) {}
                }
                loadSimilarRecommendations()
            }

            allYtItems.value = similarRecommendations.value?.flatMap { it.items }.orEmpty() +
                    homePage.value?.sections?.flatMap { it.items }.orEmpty()
                    
            isInitialLoadComplete.value = true
        } catch (e: Exception) {
            reportException(e)
        } finally {
            isLoading.value = false
            if (!isOnline.value) loadOfflineHome()
        }
    }

    private suspend fun loadSimilarRecommendations() {
        val hideExplicit = context.dataStore.get(HideExplicitKey, false)
        val hideVideo = context.dataStore.get(HideVideoKey, false)
        val fromTimeStamp = System.currentTimeMillis() - 86400000 * 7 * 2
        
        val artistSeeds = database.mostPlayedArtists(fromTimeStamp, limit = 10).first()
            .filter { it.artist.isYouTubeArtist }
            .shuffled().take(3)
        val songSeeds = database.mostPlayedSongs(fromTimeStamp, limit = 10).first()
            .filter { it.album != null }
            .shuffled().take(2)

        // All requests run at the same time instead of one after another.
        val (artistRecommendations, songRecommendations) = kotlinx.coroutines.coroutineScope {
            val artistJobs = artistSeeds.map { artist ->
                async<SimilarRecommendation?> {
                    val items = mutableListOf<YTItem>()
                    YouTube.artist(artist.id).onSuccess { page ->
                        items += page.sections.getOrNull(page.sections.size - 2)?.items.orEmpty()
                        items += page.sections.lastOrNull()?.items.orEmpty()
                    }
                    SimilarRecommendation(
                        title = artist,
                        items = items.filterExplicit(hideExplicit).filterVideo(hideVideo).shuffled().ifEmpty { return@async null }
                    )
                }
            }
            val songJobs = songSeeds.map { song ->
                async<SimilarRecommendation?> {
                    val endpoint = YouTube.next(WatchEndpoint(videoId = song.id)).getOrNull()?.relatedEndpoint
                        ?: return@async null
                    val page = YouTube.related(endpoint).getOrNull() ?: return@async null
                    SimilarRecommendation(
                        title = song,
                        items = (page.songs.shuffled().take(8) +
                                page.albums.shuffled().take(4) +
                                page.artists.shuffled().take(4) +
                                page.playlists.shuffled().take(4))
                            .filterExplicit(hideExplicit).filterVideo(hideVideo)
                            .shuffled()
                            .ifEmpty { return@async null }
                    )
                }
            }
            artistJobs.awaitAll().filterNotNull() to songJobs.awaitAll().filterNotNull()
        }

        similarRecommendations.value = (artistRecommendations + songRecommendations).shuffled()
        
        allYtItems.value = similarRecommendations.value?.flatMap { it.items }.orEmpty() +
                homePage.value?.sections?.flatMap { it.items }.orEmpty()
    }

    private val _isLoadingMore = MutableStateFlow(false)
    fun loadMoreYouTubeItems(continuation: String?) {
        if (continuation == null || _isLoadingMore.value) return
        val hideExplicit = context.dataStore.get(HideExplicitKey, false)
        val hideVideo = context.dataStore.get(HideVideoKey, false)

        viewModelScope.launch(Dispatchers.IO) {
            _isLoadingMore.value = true
            val nextSections = YouTube.home(continuation).getOrNull() ?: run {
                _isLoadingMore.value = false
                return@launch
            }

            homePage.value = nextSections.copy(
                chips = homePage.value?.chips,
                sections = (homePage.value?.sections.orEmpty() + nextSections.sections).map { section ->
                    section.copy(items = section.items.filterExplicit(hideExplicit).filterVideo(hideVideo))
                }
            )
            _isLoadingMore.value = false
        }
    }

    fun toggleChip(chip: HomePage.Chip?) {
        if (chip == null || (chip == selectedChip.value && previousHomePage.value != null)) {
            homePage.value = previousHomePage.value
            previousHomePage.value = null
            selectedChip.value = null
            return
        }

        val originalHomePage = previousHomePage.value ?: homePage.value
        if (selectedChip.value == null) {
            previousHomePage.value = homePage.value
        }

        viewModelScope.launch(Dispatchers.IO) {
            val hideExplicit = context.dataStore.get(HideExplicitKey, false)
            val hideVideo = context.dataStore.get(HideVideoKey, false)
            val nextSections = YouTube.home(params = chip.endpoint?.params).getOrNull()
            if (nextSections == null) {
                if (selectedChip.value == null) {
                    previousHomePage.value = null
                }
                return@launch
            }

            homePage.value = nextSections.copy(
                chips = nextSections.chips ?: homePage.value?.chips ?: originalHomePage?.chips,
                sections = nextSections.sections.map { section ->
                    section.copy(items = section.items.filterExplicit(hideExplicit).filterVideo(hideVideo))
                }
            )
            selectedChip.value = chip
        }
    }

    fun refresh() {
        if (isRefreshing.value) return
        forceMoodRefresh = true
        forceHomeRefresh = true
        viewModelScope.launch(Dispatchers.IO) {
            isRefreshing.value = true
            load()
            isRefreshing.value = false
        }
    }

    fun refreshAccountData() {
        viewModelScope.launch(Dispatchers.IO) {
            if (isProcessingAccountData) return@launch
            
            isProcessingAccountData = true
            try {
                val cookie = context.dataStore.get(InnerTubeCookieKey, "")
                if (cookie.isNotEmpty()) {
                    YouTube.cookie = cookie
                    
                    YouTube.accountInfo().onSuccess { info ->
                        accountName.value = info.name
                        accountImageUrl.value = info.thumbnailUrl
                    }.onFailure {
                        timber.log.Timber.w(it, "Failed to fetch account info")
                    }

                    launch {
                        YouTube.library("FEmusic_liked_playlists").completed().onSuccess {
                            val lists = it.items.filterIsInstance<PlaylistItem>().filterNot { it.id == "SE" }
                            accountPlaylists.value = lists
                        }.onFailure {
                            timber.log.Timber.w(it, "Failed to fetch playlists")
                        }
                    }
                } else {
                    accountName.value = "Guest"
                    accountImageUrl.value = null
                    accountPlaylists.value = null
                }
            } finally {
                isProcessingAccountData = false
            }
        }
    }

    init {
        // Show the last Quick picks right away; the fresh ones are saved for the next start.
        viewModelScope.launch(Dispatchers.IO) {
            QuickPicksCache.load(context)?.takeIf { it.isNotEmpty() }?.let { cached ->
                if (moodQuickPicks.value == null) {
                    moodShownFromCache = true
                    moodQuickPicks.value = cached
                }
            }
        }

        viewModelScope.launch(Dispatchers.IO) {
            load()
        }

        watchConnectivity()
        viewModelScope.launch(Dispatchers.IO) {
            while (true) {
                kotlinx.coroutines.delay(5000)
                if (!isOnline.value) {
                    try { loadOfflineHome() }
                    catch (e: kotlinx.coroutines.CancellationException) { throw e }
                    catch (e: Exception) { reportException(e) }
                }
            }
        }

        // After a reinstall the liked songs arrive a few seconds later from the account sync.
        // When the first likes show up, rebuild the Quick picks with them.
        viewModelScope.launch(Dispatchers.IO) {
            var hadLikes: Boolean? = null
            database.likedSongsCount().map { it > 0 }.distinctUntilChanged().collect { hasLikes ->
                if (hadLikes == false && hasLikes) {
                    likedSnapshot = null
                    forceMoodRefresh = true
                    try {
                        getQuickPicks()
                        loadMoodQuickPicks()
                    } catch (e: Exception) {
                        reportException(e)
                    }
                }
                hadLikes = hasLikes
            }
        }

        viewModelScope.launch(Dispatchers.IO) {
            kotlinx.coroutines.delay(3000)
            
            syncUtils.cleanupDuplicatePlaylists()
        }
        
        viewModelScope.launch(Dispatchers.IO) {
            context.dataStore.data
                .map { it[InnerTubeCookieKey] }
                .distinctUntilChanged()
                .collect { cookie ->
                    if (isProcessingAccountData) return@collect
                    
                    lastProcessedCookie = cookie
                    isProcessingAccountData = true
                    
                    try {
                        val isLoggedIn = cookie?.let { "SAPISID" in parseCookieString(it) } ?: false
                        val loginTransition = isLoggedIn && !wasLoggedIn
                        wasLoggedIn = isLoggedIn
                        
                        if (isLoggedIn && cookie != null && cookie.isNotEmpty()) {
                            try {
                                YouTube.cookie = cookie
                            } catch (e: Exception) {
                                timber.log.Timber.e(e, "Failed to set YouTube cookie")
                                return@collect
                            }

                            if (loginTransition) {
                                launch {
                                    try {
                                        if (context.dataStore.get(YtmSyncKey, true)) {
                                            syncUtils.performFullSync()
                                        }
                                    } catch (e: Exception) {
                                        Timber.e(e, "Error during login-triggered sync")
                                        reportException(e)
                                    }
                                }
                            }
                            
                            kotlinx.coroutines.delay(100)
                            
                            try {
                                YouTube.accountInfo().onSuccess { info ->
                                    accountName.value = info.name
                                    accountImageUrl.value = info.thumbnailUrl
                                }.onFailure { e ->
                                    timber.log.Timber.w(e, "Failed to fetch account info")
                                }
                            } catch (e: Exception) {
                                timber.log.Timber.e(e, "Exception fetching account info")
                            }

                            launch {
                                try {
                                    YouTube.library("FEmusic_liked_playlists").completed().onSuccess {
                                        val lists = it.items.filterIsInstance<PlaylistItem>().filterNot { it.id == "SE" }
                                        accountPlaylists.value = lists
                                    }.onFailure { e ->
                                        timber.log.Timber.w(e, "Failed to fetch account playlists")
                                    }
                                } catch (e: Exception) {
                                    timber.log.Timber.e(e, "Exception fetching account playlists")
                                }
                            }
                        } else {
                            accountName.value = "Guest"
                            accountImageUrl.value = null
                            accountPlaylists.value = null
                        }
                    } catch (e: Exception) {
                        timber.log.Timber.e(e, "Error processing cookie change")
                        accountName.value = "Guest"
                        accountImageUrl.value = null
                        accountPlaylists.value = null
                    } finally {
                        isProcessingAccountData = false
                    }
                }
        }
    }
}

/** Songs played in the last [RECENT_EXCLUDE_COUNT] plays are not suggested as new. */
private const val RECENT_EXCLUDE_COUNT = 30

/** Most points taken from a new song that was heard before (older than the recent plays). */
private const val MAX_HEARD_PENALTY = 0.35f

private class ArtistAffinity(
    val byId: Map<String, Float>,
    val byName: Map<String, Float>,
) {
    fun of(id: String?, name: String): Float =
        maxOf(
            id?.let { byId[it] } ?: 0f,
            byName[com.nikhil.yt.utils.QuickPicksRanker.normalizeArtist(name)] ?: 0f,
        )
}
