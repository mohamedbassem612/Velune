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
import kotlinx.coroutines.sync.withLock
import timber.log.Timber
import kotlinx.coroutines.flow.debounce
import androidx.media3.datasource.cache.Cache
import com.nikhil.yt.di.DownloadCache
import com.nikhil.yt.di.PlayerCache
import com.nikhil.yt.extensions.filterExplicit as filterExplicitSongs
import com.nikhil.yt.utils.isOnlineNow
import com.nikhil.yt.playback.CacheAvailability
import com.nikhil.yt.utils.onlineFlow
import javax.inject.Inject

@HiltViewModel
class HomeViewModel @Inject constructor(
    @ApplicationContext val context: Context,
    val database: MusicDatabase,
    val syncUtils: SyncUtils,
    val forYouEngine: com.nikhil.yt.utils.ForYouSuggestionEngine,
    private val quickPicksEngine: com.nikhil.yt.utils.QuickPicksRecommendationEngine,
    @PlayerCache private val playerCache: Cache,
    @DownloadCache private val downloadCache: Cache,
    private val mp3Library: com.nikhil.yt.playback.Mp3Library,
    private val downloadUtil: com.nikhil.yt.playback.DownloadUtil,
) : ViewModel() {
    // Same seed for the whole offline session, so the Quick picks do not reshuffle on every refresh.
    @Volatile private var offlineSeed = System.nanoTime()
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
    private var likedSnapshot: List<Song>? = null
    private var likedSnapshotTime = 0L
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

    private val quickPicksMutex = kotlinx.coroutines.sync.Mutex()

    private suspend fun loadMoodQuickPicks() = quickPicksMutex.withLock {
        val result = try {
            buildMoodQuickPicks()
        } catch (cancelled: kotlinx.coroutines.CancellationException) {
            throw cancelled
        } catch (e: Exception) {
            reportException(e)
            null
        }
        if (result != null) {
            QuickPicksCache.save(context, result)
            prefetchThumbnails(result.mapNotNull { it.thumbnail })
            moodQuickPicks.value = result
        } else if (moodQuickPicks.value == null) {
            moodQuickPicks.value = emptyList()
        }
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

    /** Multi-source recommendations based on listening quality, recency and compatible charts. */
    private suspend fun buildMoodQuickPicks(): List<SongItem> = quickPicksEngine.recommend(
        hideExplicit = context.dataStore.get(HideExplicitKey, false),
        hideVideo = context.dataStore.get(HideVideoKey, false),
        lastListenOnly = quickPicksEnum.first() == QuickPicks.LAST_LISTEN,
        shownIds = moodQuickPicks.value.orEmpty().map { it.id }.toSet(),
    )

    private suspend fun buildTasteBasedQuickPicks(): List<Song> = quickPicksEngine.localFallback(
        hideExplicit = context.dataStore.get(HideExplicitKey, false),
    )

    // Offline mode: the top 5 most played songs go in the carousel and the Quick picks list is
    // filled with other songs that are downloaded or fully saved in the cache. Nothing else is shown.
    private suspend fun loadOfflineHome() {
        val hideExplicit = context.dataStore.get(HideExplicitKey, false)
        val mp3Ids = mp3Library.ids()
        val ids = (downloadCache.keys + playerCache.keys + mp3Ids).toList()
        val songs = ids.chunked(500).flatMap { database.getSongsByIds(it) }
        val playable = songs.filter { song ->
            val length = song.format?.contentLength
            mp3Library.playableUri(song.song.id) != null ||
                CacheAvailability.isCached(downloadCache, song.song.id, fallbackContentLength = length) ||
                CacheAvailability.isCached(playerCache, song.song.id, fallbackContentLength = length)
        }.filterExplicitSongs(hideExplicit)
        val ranked = playable.sortedByDescending { it.song.totalPlayTime }
        val header = ranked.take(5)
        quickPicks.value = header
        quickPicksFallback.value = ranked.drop(5).shuffled(kotlin.random.Random(offlineSeed)).take(20).ifEmpty { header }
        allLocalItems.value = ranked.take(25)
        allYtItems.value = emptyList()
    }

    // While offline, refresh the list when a download finishes, fails or is removed.
    @OptIn(kotlinx.coroutines.FlowPreview::class)
    private fun watchOfflineAvailability() {
        viewModelScope.launch(Dispatchers.IO) {
            downloadUtil.downloads.debounce(500).collect {
                if (!isOnline.value) {
                    try {
                        loadOfflineHome()
                    } catch (e: kotlinx.coroutines.CancellationException) {
                        throw e
                    } catch (e: Exception) {
                        reportException(e)
                    }
                }
            }
        }
    }

    private fun watchConnectivity() {
        viewModelScope.launch(Dispatchers.IO) {
            context.onlineFlow().collect { online ->
                if (isOnline.value == online) return@collect
                isOnline.value = online
                if (!online) offlineSeed = System.nanoTime()
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

        try {
            supervisorScope {
                val hideExplicit = context.dataStore.get(HideExplicitKey, false)
                val hideVideo = context.dataStore.get(HideVideoKey, false)
                val fromTimeStamp = System.currentTimeMillis() - 86400000 * 7 * 2

                launch { getQuickPicks() }
                launch { loadMoodQuickPicks() }
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
        // Show a recent cache while the new profile and candidates are loading.
        viewModelScope.launch(Dispatchers.IO) {
            QuickPicksCache.load(context)
                ?.filterExplicit(context.dataStore.get(HideExplicitKey, false))
                ?.filterVideo(context.dataStore.get(HideVideoKey, false))?.takeIf { it.isNotEmpty() }?.let { cached ->
                moodQuickPicks.compareAndSet(null, cached)
            }
        }

        viewModelScope.launch(Dispatchers.IO) {
            load()
        }

        watchConnectivity()
        watchOfflineAvailability()

        // After a reinstall the liked songs arrive a few seconds later from the account sync.
        // When the first likes show up, rebuild the Quick picks with them.
        viewModelScope.launch(Dispatchers.IO) {
            var hadLikes: Boolean? = null
            database.likedSongsCount().map { it > 0 }.distinctUntilChanged().collect { hasLikes ->
                if (hadLikes == false && hasLikes) {
                    likedSnapshot = null
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
