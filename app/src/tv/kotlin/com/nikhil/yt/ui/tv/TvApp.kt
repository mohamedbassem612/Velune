package com.nikhil.yt.ui.tv

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.grid.GridCells
import androidx.compose.foundation.lazy.grid.GridItemSpan
import androidx.compose.foundation.lazy.grid.LazyVerticalGrid
import androidx.compose.foundation.lazy.grid.items
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.text.KeyboardActions
import androidx.compose.foundation.text.KeyboardOptions
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalSoftwareKeyboardController
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.input.ImeAction
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.hilt.lifecycle.viewmodel.compose.hiltViewModel
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.compose.LifecycleEventEffect
import androidx.media3.common.C
import coil3.compose.AsyncImage
import com.nikhil.yt.R
import com.nikhil.yt.innertube.toHighResThumbnail
import com.nikhil.yt.extensions.toMediaItem
import com.nikhil.yt.extensions.togglePlayPause
import com.nikhil.yt.models.MediaMetadata
import com.nikhil.yt.models.toMediaMetadata
import com.nikhil.yt.playback.PlayerConnection
import com.nikhil.yt.playback.queues.ListQueue
import com.nikhil.yt.playback.queues.YouTubeQueue
import com.nikhil.yt.viewmodels.HomeViewModel
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive

@Composable
fun TvApp(
    connection: PlayerConnection?,
    home: HomeViewModel = hiltViewModel(),
    search: TvSearchViewModel = hiltViewModel(),
) {
    var fullPlayer by remember { mutableStateOf(false) }
    var page by remember { mutableStateOf(TvPage.Home) }
    var showPhoneConnection by rememberSaveable { mutableStateOf(false) }
    var query by rememberSaveable { mutableStateOf("") }
    var librarySelection by rememberSaveable { mutableStateOf<String?>(null) }
    var focusedSong by remember { mutableStateOf<MediaMetadata?>(null) }
    var railFocused by remember { mutableStateOf(true) }
    val navigation = remember { TvPage.entries.associateWith { FocusRequester() } }
    val contentFocus = remember { FocusRequester() }
    val keyboard = LocalSoftwareKeyboardController.current
    val searchState by search.state.collectAsState()
    val quickPicks by home.quickPicks.collectAsState()
    val suggestions by home.moodQuickPicks.collectAsState()
    val forYou by home.forYouSuggestions.collectAsState()
    val fallback by home.quickPicksFallback.collectAsState()
    val forgotten by home.forgottenFavorites.collectAsState()
    val homeLoading by home.isLoading.collectAsState()
    val likedSongs by remember(home.database) { home.database.likedSongsByCreateDateAsc() }.collectAsState(initial = emptyList())
    val playlists by remember(home.database) { home.database.playlistsByCreateDateAsc() }.collectAsState(initial = emptyList())
    val history by remember(home.database) { home.database.recentEvents(100) }.collectAsState(initial = emptyList())
    val playlistSongs by remember(home.database, librarySelection) { home.database.playlistSongs(librarySelection.orEmpty()) }.collectAsState(initial = emptyList())
    val activeTrack = connection?.mediaMetadata?.collectAsState()?.value

    val recent = history.map { it.song.toMediaMetadata() }.distinctBy { it.id }
    val personal = quickPicks.orEmpty().map { it.toMediaMetadata() }
        .ifEmpty { recent + likedSongs.asReversed().map { it.toMediaMetadata() } }.distinctBy { it.id }.take(24)
    val personalIds = personal.mapTo(mutableSetOf()) { it.id }
    val recommendations = (forYou.orEmpty().map { it.toMediaMetadata() } + suggestions.orEmpty().map { it.toMediaMetadata() } + fallback.orEmpty().map { it.toMediaMetadata() })
        .distinctBy { it.id }.filterNot { it.id in personalIds }.take(24)
    val rediscover = forgotten.orEmpty().map { it.toMediaMetadata() }.distinctBy { it.id }.take(18)
    val hero = focusedSong ?: activeTrack ?: personal.firstOrNull() ?: recommendations.firstOrNull()
    val librarySongs = when (librarySelection) {
        null -> likedSongs.asReversed().map { it.toMediaMetadata() }
        "#history" -> recent
        else -> playlistSongs.map { it.song.toMediaMetadata() }.distinctBy { it.id }
    }
    val currentRail = navigation.getValue(page)
    fun play(songs: List<MediaMetadata>, selected: MediaMetadata) {
        val index = songs.indexOfFirst { it.id == selected.id }.coerceAtLeast(0)
        connection?.let {
            it.playQueue(ListQueue(items = songs.map { it.toMediaItem() }, startIndex = index))
            fullPlayer = true
        }
    }
    fun submitSearch() {
        if (query.isBlank()) return
        keyboard?.hide()
        search.search(query)
    }

    LaunchedEffect(fullPlayer) { if (!fullPlayer) currentRail.requestFocus() }
    LifecycleEventEffect(Lifecycle.Event.ON_START) {
        page = TvPage.Home
        fullPlayer = false
        showPhoneConnection = false
    }
    BackHandler(enabled = !fullPlayer && (!railFocused || page != TvPage.Home || showPhoneConnection)) {
        keyboard?.hide()
        when {
            page == TvPage.Settings && showPhoneConnection -> { showPhoneConnection = false; currentRail.requestFocus() }
            !railFocused -> currentRail.requestFocus()
            else -> { page = TvPage.Home; navigation.getValue(TvPage.Home).requestFocus() }
        }
    }

    TvAmoledTheme {
        Surface(Modifier.fillMaxSize(), color = TvBackground, contentColor = Color.White) {
            Box {
                // The artwork stays behind the shelves and fades into the fixed left rail.
                if (page == TvPage.Home && hero?.thumbnailUrl != null) {
                    AsyncImage(
                        hero.thumbnailUrl?.toHighResThumbnail(), null,
                        Modifier.align(Alignment.TopEnd).fillMaxWidth(.82f).fillMaxHeight(.86f),
                        contentScale = ContentScale.Crop, alpha = .73f,
                    )
                    Box(Modifier.fillMaxSize().background(Brush.horizontalGradient(
                        colorStops = arrayOf(0f to TvBackground, .28f to TvBackground.copy(alpha = .97f), .62f to TvBackground.copy(alpha = .64f), 1f to TvBackground.copy(alpha = .1f)),
                    )))
                    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(
                        colorStops = arrayOf(0f to TvBackground.copy(alpha = .08f), .3f to TvBackground.copy(alpha = .24f), .72f to TvBackground.copy(alpha = .94f), 1f to TvBackground),
                    )))
                }
                if (fullPlayer && connection != null) {
                    TvFullPlayer(connection, onClose = { fullPlayer = false }, onNavigate = {
                        page = it; fullPlayer = false
                    })
                } else Row(Modifier.fillMaxSize()) {
                    TvNavigationRail(
                        page, navigation,
                        contentFocus,
                        onSelect = { keyboard?.hide(); page = it },
                        onRailFocus = { railFocused = it },
                    )
                    Column(Modifier.weight(1f).fillMaxHeight()) {
                        Box(Modifier.weight(1f).fillMaxWidth()) {
                            when (page) {
                                TvPage.Home -> TvHomeScreen(
                                    personal, recommendations, rediscover, recent, hero, homeLoading,
                                    connection != null, contentFocus, currentRail,
                                    onSearch = { page = TvPage.Search },
                                    onFocusTrack = { focusedSong = it },
                                    onPlay = ::play,
                                    onRadio = { connection?.playQueue(YouTubeQueue.radio(it)); if (connection != null) fullPlayer = true },
                                )
                                TvPage.Search -> Column(Modifier.fillMaxSize().padding(top = 28.dp)) {
                                    Text(stringResource(R.string.search), style = MaterialTheme.typography.headlineMedium, modifier = Modifier.padding(horizontal = 28.dp))
                                    Row(Modifier.padding(horizontal = 28.dp, vertical = 18.dp), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
                                        OutlinedTextField(
                                            value = query, onValueChange = { query = it },
                                            modifier = Modifier.weight(1f).focusRequester(contentFocus).focusProperties { left = currentRail },
                                            label = { Text(stringResource(R.string.tv_search_hint)) }, singleLine = true,
                                            keyboardOptions = KeyboardOptions(imeAction = ImeAction.Search),
                                            keyboardActions = KeyboardActions(onSearch = { submitSearch() }),
                                        )
                                        TvButton(stringResource(R.string.search), { submitSearch() }, enabled = query.isNotBlank())
                                    }
                                    TvSongGrid(
                                        songs = searchState.songs, ready = connection != null, loading = searchState.loading,
                                        message = stringResource(when {
                                            searchState.failed -> R.string.tv_search_failed
                                            searchState.submitted -> R.string.no_results_found
                                            else -> R.string.tv_search_hint
                                        }),
                                        railFocus = currentRail, onFocusTrack = { focusedSong = it }, onPlay = { play(searchState.songs, it) },
                                    )
                                }
                                TvPage.Library -> Column(Modifier.fillMaxSize().padding(top = 28.dp)) {
                                    Text(stringResource(R.string.filter_library), style = MaterialTheme.typography.headlineMedium, modifier = Modifier.padding(horizontal = 28.dp))
                                    LazyRow(
                                        contentPadding = PaddingValues(horizontal = 28.dp, vertical = 18.dp),
                                        horizontalArrangement = Arrangement.spacedBy(10.dp),
                                    ) {
                                        item { TvButton(stringResource(R.string.liked_songs), { librarySelection = null }, Modifier.focusRequester(contentFocus).focusProperties { left = currentRail }, selected = librarySelection == null) }
                                        item { TvButton(stringResource(R.string.history), { librarySelection = "#history" }, selected = librarySelection == "#history") }
                                        items(playlists, key = { it.id }) { list -> TvButton(list.title, { librarySelection = list.id }, selected = librarySelection == list.id) }
                                    }
                                    TvSongGrid(
                                        songs = librarySongs, ready = connection != null,
                                        message = stringResource(if (librarySelection == null) R.string.tv_likes_empty else R.string.no_results_found),
                                        railFocus = currentRail, onFocusTrack = { focusedSong = it }, onPlay = { play(librarySongs, it) },
                                    )
                                }
                                TvPage.Settings -> if (showPhoneConnection) {
                                    TvPhoneConnection(
                                        connection, contentFocus, currentRail,
                                        onBack = { showPhoneConnection = false; currentRail.requestFocus() },
                                        modifier = Modifier.fillMaxSize().padding(28.dp),
                                    )
                                } else {
                                    TvSettingsScreen(
                                        contentFocus = contentFocus, railFocus = currentRail,
                                        onPhoneConnection = { showPhoneConnection = true },
                                        onRefresh = { home.refresh(); page = TvPage.Home; navigation.getValue(TvPage.Home).requestFocus() },
                                    )
                                }
                            }
                        }
                        TvPlayer(connection, onExpand = { fullPlayer = true })
                    }
                }
            }
        }
    }
}

@Composable
private fun TvSongGrid(
    songs: List<MediaMetadata>, ready: Boolean, message: String,
    railFocus: FocusRequester, onFocusTrack: (MediaMetadata) -> Unit,
    onPlay: (MediaMetadata) -> Unit, loading: Boolean = false,
) {
    LazyVerticalGrid(
        columns = GridCells.Adaptive(142.dp),
        modifier = Modifier.fillMaxSize(), contentPadding = PaddingValues(start = 28.dp, end = 28.dp, top = 4.dp, bottom = 24.dp),
        horizontalArrangement = Arrangement.spacedBy(14.dp), verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        if (loading || songs.isEmpty()) {
            item(span = { GridItemSpan(maxLineSpan) }) {
                Row(Modifier.padding(vertical = 20.dp), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
                    if (loading) CircularProgressIndicator(Modifier.size(24.dp), color = TvAccent)
                    Text(if (loading) stringResource(R.string.loading) else message, color = Color.White.copy(alpha = .6f))
                }
            }
        }
        items(songs, key = { it.id }) { song ->
            TvTrackCard(song, ready, onFocused = { onFocusTrack(song) }, modifier = Modifier.focusProperties { if (song.id == songs.firstOrNull()?.id) left = railFocus }) { onPlay(song) }
        }
    }
}

@Composable
private fun TvPlayer(connection: PlayerConnection?, onExpand: () -> Unit) {
    if (connection == null) {
        Text(stringResource(R.string.tv_connection_wait), Modifier.padding(24.dp), color = Color.White.copy(alpha = .5f))
        return
    }
    val track by connection.mediaMetadata.collectAsState()
    val playing by connection.isPlaying.collectAsState()
    val previous by connection.canSkipPrevious.collectAsState()
    val next by connection.canSkipNext.collectAsState()
    val error by connection.error.collectAsState()
    val song by remember(connection) { connection.currentSong }.collectAsState(initial = null)
    var positionMs by remember(connection, track?.id) { mutableLongStateOf(0L) }
    var durationMs by remember(connection, track?.id) { mutableLongStateOf(0L) }
    LaunchedEffect(connection, track?.id) {
        while (isActive) {
            positionMs = connection.player.currentPosition.coerceAtLeast(0L)
            durationMs = connection.player.duration.takeIf { it != C.TIME_UNSET && it > 0L } ?: 0L
            delay(250L)
        }
    }
    Surface(color = TvBackground.copy(alpha = .97f)) {
        Column(Modifier.fillMaxWidth().padding(horizontal = 28.dp, vertical = 10.dp)) {
            LinearProgressIndicator(
                progress = { if (track != null && durationMs > 0L) (positionMs.toFloat() / durationMs).coerceIn(0f, 1f) else 0f },
                modifier = Modifier.fillMaxWidth().height(3.dp),
                color = TvAccent,
                trackColor = Color.White.copy(alpha = .12f),
                gapSize = 0.dp,
                drawStopIndicator = {},
            )
            if (error != null) Text(stringResource(R.string.tv_playback_failed), color = MaterialTheme.colorScheme.error, fontSize = 12.sp)
            Row(Modifier.padding(top = 8.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(12.dp)) {
                Box(Modifier.size(42.dp).clip(RoundedCornerShape(7.dp)).background(TvSurface)) {
                    Icon(painterResource(R.drawable.music_note), null, Modifier.align(Alignment.Center).size(22.dp), tint = Color.White.copy(alpha = .2f))
                    AsyncImage(track?.thumbnailUrl, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
                }
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(3.dp)) {
                    Text(track?.title ?: stringResource(R.string.tv_no_track), maxLines = 1, overflow = TextOverflow.Ellipsis, color = Color.White, fontSize = 14.sp)
                    Text(track?.artists?.joinToString { it.name }.orEmpty(), maxLines = 1, overflow = TextOverflow.Ellipsis, color = Color.White.copy(alpha = .5f), fontSize = 11.sp)
                }
                TvPlayerAction(R.drawable.expand_less, stringResource(R.string.tv_open_player), track != null, onClick = onExpand)
                TvPlayerAction(R.drawable.skip_previous, stringResource(R.string.tv_previous), track != null && previous) { connection.seekToPrevious() }
                TvPlayerAction(if (playing) R.drawable.pause else R.drawable.play, stringResource(if (playing) R.string.tv_pause else R.string.play), track != null, prominent = true) { connection.player.togglePlayPause() }
                TvPlayerAction(R.drawable.skip_next, stringResource(R.string.tv_next), track != null && next) { connection.seekToNext() }
                TvPlayerAction(if (song?.song?.liked == true) R.drawable.favorite else R.drawable.favorite_border, stringResource(if (song?.song?.liked == true) R.string.tv_unlike else R.string.tv_like), track != null) { connection.toggleLike() }
            }
        }
    }
}

@Composable
internal fun TvPlayerAction(icon: Int, label: String, enabled: Boolean, prominent: Boolean = false, modifier: Modifier = Modifier, onClick: () -> Unit) {
    var focused by remember { mutableStateOf(false) }
    Surface(
        onClick = onClick, enabled = enabled,
        modifier = modifier.size(42.dp).onFocusChanged { focused = it.isFocused },
        color = if (prominent || focused) Color.White else Color.Transparent,
        shape = RoundedCornerShape(50), border = BorderStroke(2.dp, if (focused) TvAccent else Color.Transparent),
    ) {
        Box(contentAlignment = Alignment.Center) {
            Icon(painterResource(icon), label, Modifier.size(23.dp), tint = if (!enabled) Color(0xFF505050) else if (prominent || focused) Color.Black else Color.White)
        }
    }
}

@Composable
internal fun TvButton(label: String, onClick: () -> Unit, modifier: Modifier = Modifier, selected: Boolean = false, enabled: Boolean = true) {
    var focused by remember { mutableStateOf(false) }
    Button(
        onClick, enabled = enabled,
        modifier = modifier.onFocusChanged { focused = it.isFocused }.heightIn(min = 40.dp),
        border = BorderStroke(2.dp, if (focused) TvAccent else Color.Transparent),
        colors = ButtonDefaults.buttonColors(
            containerColor = if (selected || focused) Color.White else TvSurface,
            contentColor = if (selected || focused) Color.Black else Color.White,
        ),
    ) { Text(label, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis) }
}
