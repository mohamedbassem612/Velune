package com.nikhil.yt.ui.player

import androidx.activity.compose.BackHandler
import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.draw.drawWithContent
import androidx.compose.ui.graphics.drawscope.clipRect
import androidx.compose.ui.geometry.Rect
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.layout.LayoutCoordinates
import androidx.compose.ui.layout.onGloballyPositioned
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.*
import androidx.media3.common.Player
import androidx.navigation.NavController
import com.nikhil.yt.LocalPlayerConnection
import com.nikhil.yt.R
import com.nikhil.yt.constants.MiniPlayerHeight
import com.nikhil.yt.extensions.togglePlayPause
import com.nikhil.yt.extensions.toggleRepeatMode
import com.nikhil.yt.models.MediaMetadata
import com.nikhil.yt.playback.PlayerConnection
import com.nikhil.yt.ui.component.*
import com.nikhil.yt.ui.menu.PlayerMenu
import com.nikhil.yt.ui.utils.ShowMediaInfo
import com.nikhil.yt.utils.makeTimeString
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive

/** All playback actions use the existing service connection; opening a surface never loads a queue. */
@Composable
fun BottomSheetPlayer(
    state: BottomSheetState,
    navController: NavController,
    modifier: Modifier = Modifier,
    pureBlack: Boolean,
    miniPlayerHeight: Dp = MiniPlayerHeight,
) {
    val useMusicPlayer by com.nikhil.yt.utils.rememberPreference(com.nikhil.yt.constants.UseMusicPlayerKey, true)
    if (!useMusicPlayer) {
        LegacyBottomSheetPlayer(state, navController, modifier, pureBlack)
        return
    }
    val connection = LocalPlayerConnection.current ?: return
    val metadata by connection.mediaMetadata.collectAsState()
    val isPlaying by connection.isPlaying.collectAsState()
    val playbackState by connection.playbackState.collectAsState()
    val canNext by connection.canSkipNext.collectAsState()
    val canPrevious by connection.canSkipPrevious.collectAsState()
    val automix by connection.service.automixItems.collectAsState()
    LaunchedEffect(canNext, automix, metadata?.id) {
        if (connection.service.handoverController.selected.value == null && !canNext && automix.isNotEmpty()) {
            connection.service.addToQueueAutomix(automix[0], 0)
        }
    }
    val currentSong by connection.currentSong.collectAsState(initial = null)
    val menu = LocalMenuState.current
    val pages = LocalBottomSheetPageState.current
    val bottomInset = WindowInsets.systemBars.asPaddingValues().calculateBottomPadding()
    val queueState = rememberBottomSheetState(
        dismissedBound = 0.dp,
        collapsedBound = 0.dp,
        expandedBound = state.expandedBound,
        initialAnchor = COLLAPSED_ANCHOR,
    )
    var lyricsVisible by rememberSaveable { mutableStateOf(false) }
    val color = MaterialTheme.colorScheme.onSurface
    val surface = MaterialTheme.colorScheme.surface
    var sheetCoordinates by remember { mutableStateOf<LayoutCoordinates?>(null) }
    var coverBounds by remember { mutableStateOf<Rect?>(null) }
    var titleBounds by remember { mutableStateOf<Rect?>(null) }
    val showMenu: () -> Unit = {
        menu.show {
            PlayerMenu(metadata, navController, state,
                onShowDetailsDialog = { metadata?.id?.let { id -> pages.show { ShowMediaInfo(id) } } },
                onDismiss = menu::dismiss)
        }
    }
    BackHandler(!state.isCollapsed && !state.isDismissed) {
        when {
            lyricsVisible -> lyricsVisible = false
            !queueState.isCollapsed -> queueState.collapseSoft()
            else -> state.collapseSoft()
        }
    }
    BottomSheet(
        state = state,
        modifier = modifier,
        onSheetPositioned = { sheetCoordinates = it },
        backgroundColor = surface,
        onDismiss = { connection.service.stopAndClearPlayback() },
        collapsedContent = {
            MusicMiniPlayer(
                isPlaying = isPlaying,
                loading = playbackState == Player.STATE_BUFFERING,
                canNext = canNext,
                onPlayPause = connection::togglePlayPause,
                onNext = connection::seekToNext,
                onExpand = state::expandSoft,
                connection = connection,
                height = miniPlayerHeight,
            )
        },
        overlayContent = {
            val progress = state.progress.coerceIn(0f, 1f)
            val density = LocalDensity.current
            BoxWithConstraints(Modifier.fillMaxSize()) {
                val rtl = LocalLayoutDirection.current == LayoutDirection.Rtl
                val widthPx = with(density) { maxWidth.toPx() }
                fun px(value: Dp) = with(density) { value.toPx() }
                val miniCoverX = if (rtl) widthPx - px(72.dp) else px(24.dp)
                val coverY = px((miniPlayerHeight - 48.dp) / 2)
                val miniCover = Rect(miniCoverX, coverY, miniCoverX + px(48.dp), coverY + px(48.dp))
                val miniTitleX = if (rtl) px(120.dp) else px(84.dp)
                val miniTitleY = with(density) { (miniPlayerHeight.toPx() - 44.sp.toPx()) / 2 }
                val miniTitle = Rect(miniTitleX, miniTitleY, miniTitleX + (widthPx - px(204.dp)).coerceAtLeast(1f), miniTitleY + px(48.dp))
                val targetCover = coverBounds ?: miniCover
                val targetTitle = titleBounds ?: miniTitle
                val cover = androidx.compose.ui.geometry.lerp(miniCover, targetCover, progress)
                val title = androidx.compose.ui.geometry.lerp(miniTitle, targetTitle, progress)
                val viewportTop = px(WindowInsets.safeDrawing.asPaddingValues().calculateTopPadding() + 56.dp)
                val viewportBottom = px(maxHeight - queueState.collapsedBound)
                if (!lyricsVisible) {
                    Box(Modifier.fillMaxSize().drawWithContent {
                        clipRect(top = viewportTop * progress,
                            bottom = size.height + (viewportBottom - size.height) * progress) { this@drawWithContent.drawContent() }
                    }) {
                    MusicArtwork(metadata?.thumbnailUrl, songId = metadata?.id, modifier =
                        Modifier.absoluteOffset(with(density) { cover.left.toDp() }, with(density) { cover.top.toDp() })
                            .size(with(density) { cover.width.toDp() }, with(density) { cover.height.toDp() })
                            .miniPlayerSwipe(progress == 0f, connection::seekToPrevious, connection::seekToNext)
                            .then(if (progress == 0f) Modifier.clickable(onClick = state::expandSoft) else Modifier))
                    MusicTrackInfo(metadata?.title.orEmpty(), metadata?.artists?.joinToString { it.name }.orEmpty(),
                        expandedFraction = progress,
                        modifier = Modifier.absoluteOffset(with(density) { title.left.toDp() }, with(density) { title.top.toDp() })
                            .width(with(density) { title.width.toDp() })
                            .miniPlayerSwipe(progress == 0f, connection::seekToPrevious, connection::seekToNext)
                            .then(if (progress == 0f) Modifier.clickable(onClick = state::expandSoft) else Modifier))
                }
                }
                if (progress > 0f) {
                    if (queueState.progress > 0f) Box(Modifier.fillMaxSize()
                        .background(MaterialTheme.colorScheme.scrim.copy(alpha = 0.4f * queueState.progress.coerceIn(0f, 1f)))
                        .clickable(onClick = queueState::collapseSoft))
                    Queue(
                        state = queueState, playerBottomSheetState = state, navController = navController,
                        backgroundColor = MaterialTheme.colorScheme.surfaceContainer,
                        onBackgroundColor = color, TextBackgroundColor = color,
                        textButtonColor = color, iconButtonColor = surface,
                        onShowLyrics = { lyricsVisible = true }, pureBlack = pureBlack,
                    )
                }
            }
        },
    ) {
        metadata?.let { track ->
            MusicFullPlayer(
                metadata = track, connection = connection,
                isPlaying = isPlaying, loading = playbackState == Player.STATE_BUFFERING,
                canPrevious = canPrevious, canNext = canNext,
                liked = currentSong?.song?.liked == true,
                bottomPadding = bottomInset,
                onCollapse = state::collapseSoft, onMenu = showMenu,
                onQueue = queueState::expandSoft, onLyrics = { lyricsVisible = true },
                onArtworkPositioned = { coordinates ->
                    sheetCoordinates?.takeIf { it.isAttached && coordinates.isAttached }?.let {
                        coverBounds = it.localBoundingBoxOf(coordinates, clipBounds = false)
                    }
                },
                onTitlePositioned = { coordinates ->
                    sheetCoordinates?.takeIf { it.isAttached && coordinates.isAttached }?.let {
                        titleBounds = it.localBoundingBoxOf(coordinates, clipBounds = false)
                    }
                },
            )
        }
    }
    if (lyricsVisible) {
        // Keep lyrics and its option sheets in the same window, with an opaque full-screen surface.
        Surface(
            modifier = Modifier.fillMaxSize(),
            color = surface.copy(alpha = 1f),
            contentColor = MaterialTheme.colorScheme.onSurface,
        ) {
            metadata?.let { LyricsScreen(it, { lyricsVisible = false }, navController) }
        }
    }
}

@Composable
private fun MusicMiniPlayer(
    isPlaying: Boolean, loading: Boolean, canNext: Boolean,
    onPlayPause: () -> Unit, onNext: () -> Unit, onExpand: () -> Unit,
    connection: PlayerConnection,
    height: Dp,
) {
    Box(Modifier.padding(horizontal = 16.dp).fillMaxWidth().height(height)
        .clip(RoundedCornerShape(32.dp)).background(MaterialTheme.colorScheme.surfaceContainer)
        .miniPlayerSwipe(true, connection::seekToPrevious, onNext)
        .clickable(onClick = onExpand)) {
        Row(Modifier.fillMaxSize().padding(horizontal = 8.dp), verticalAlignment = Alignment.CenterVertically) {
            Spacer(Modifier.weight(1f))
            IconButton(onClick = onPlayPause, modifier = Modifier.size(48.dp)) {
                if (loading) CircularProgressIndicator(Modifier.size(22.dp), strokeWidth = 2.dp)
                else Icon(painterResource(if (isPlaying) R.drawable.pause else R.drawable.play),
                    stringResource(if (isPlaying) R.string.music_pause else R.string.play),
                    tint = MaterialTheme.colorScheme.primary, modifier = Modifier.size(28.dp))
            }
            IconButton(onClick = onNext, enabled = canNext, modifier = Modifier.size(48.dp)) {
                Icon(painterResource(R.drawable.skip_next), stringResource(R.string.music_next))
            }
        }
        MiniProgress(connection, Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(horizontal = 24.dp))
    }
}

@Composable
fun MusicTrackInfo(title: String, artist: String, expandedFraction: Float, modifier: Modifier = Modifier) {
    Column(modifier, verticalArrangement = Arrangement.Center) {
        AnimatedContent(
            targetState = title,
            transitionSpec = { fadeIn() togetherWith fadeOut() },
            label = "MusicTrackTitle",
        ) { animatedTitle ->
            Text(animatedTitle, fontSize = lerp(16.sp, 26.sp, expandedFraction),
                lineHeight = lerp(24.sp, 36.sp, expandedFraction), fontWeight = FontWeight.SemiBold,
                color = MaterialTheme.colorScheme.onSurface,
                maxLines = if (expandedFraction > 0.8f) 2 else 1, overflow = TextOverflow.Ellipsis)
        }
        Text(artist.ifBlank { stringResource(R.string.music_unknown_artist) },
            fontSize = lerp(12.sp, 15.sp, expandedFraction), lineHeight = lerp(20.sp, 24.sp, expandedFraction),
            color = MaterialTheme.colorScheme.onSurfaceVariant, maxLines = 1, overflow = TextOverflow.Ellipsis)
    }
}

@Composable
private fun MusicFullPlayer(
    metadata: MediaMetadata, connection: PlayerConnection,
    isPlaying: Boolean, loading: Boolean, canPrevious: Boolean, canNext: Boolean, liked: Boolean,
    bottomPadding: Dp, onCollapse: () -> Unit, onMenu: () -> Unit,
    onQueue: () -> Unit, onLyrics: () -> Unit,
    onArtworkPositioned: (LayoutCoordinates) -> Unit, onTitlePositioned: (LayoutCoordinates) -> Unit,
) {
    val queueTitle by connection.queueTitle.collectAsState()
    BoxWithConstraints(Modifier.fillMaxSize().windowInsetsPadding(WindowInsets.safeDrawing.only(
        WindowInsetsSides.Horizontal + WindowInsetsSides.Top)).padding(bottom = bottomPadding)) {
        val wide = maxWidth > maxHeight
        val coverSize = minOf(if (wide) maxWidth * 0.4f else maxWidth - 48.dp, maxHeight * if (wide) 0.68f else 0.46f)
        Column(Modifier.fillMaxSize()) {
            Row(Modifier.fillMaxWidth().height(64.dp).padding(horizontal = 12.dp),
                verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = onCollapse) {
                    Icon(painterResource(R.drawable.expand_more), stringResource(R.string.music_collapse))
                }
                Column(Modifier.weight(1f), horizontalAlignment = Alignment.CenterHorizontally) {
                    Text(stringResource(R.string.music_now_playing), style = MaterialTheme.typography.titleMedium)
                    val source = queueTitle?.takeIf { it.isNotBlank() } ?: metadata.album?.title
                    source?.let {
                        Text(it, style = MaterialTheme.typography.bodyMedium,
                            color = MaterialTheme.colorScheme.onSurfaceVariant,
                            maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
                Spacer(Modifier.size(48.dp))
            }
            val art: @Composable () -> Unit = {
                Spacer(Modifier.size(coverSize).onGloballyPositioned(onArtworkPositioned))
            }
            val controls: @Composable () -> Unit = {
                Column(Modifier.fillMaxWidth().padding(horizontal = 24.dp)) {
                    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
                        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
                            // The shared title follows this exact slot and cannot overlap its actions.
                            MusicTrackInfo(metadata.title, metadata.artists.joinToString { it.name }, 1f,
                                Modifier.weight(1f).graphicsLayer { alpha = 0f }.clearAndSetSemantics { }
                                    .onGloballyPositioned(onTitlePositioned))
                            PlayerTrackActions(connection, liked, MaterialTheme.colorScheme.onSurface)
                        }
                    }
                    Spacer(Modifier.height(16.dp))
                    MusicProgress(connection, metadata.id)
                    PlayerTransportControls(connection, isPlaying, loading, canPrevious, canNext)
                    val error by connection.error.collectAsState()
                    error?.let { PlaybackError(it, retry = { connection.player.prepare() }) }
                }
            }
            if (wide) {
                Row(Modifier.weight(1f).verticalScroll(rememberScrollState()), verticalAlignment = Alignment.CenterVertically) {
                    Box(Modifier.weight(1f), contentAlignment = Alignment.Center) { art() }
                    Box(Modifier.weight(1f)) { controls() }
                }
            } else {
                Column(Modifier.weight(1f).fillMaxWidth().verticalScroll(rememberScrollState()),
                    horizontalAlignment = Alignment.CenterHorizontally) {
                    Spacer(Modifier.height(8.dp))
                    art()
                    Spacer(Modifier.height(24.dp))
                    controls()
                }
            }
            Box(Modifier.fillMaxWidth().padding(horizontal = 24.dp)) {
                PlayerFooterActions(connection, MaterialTheme.colorScheme.onSurface, onQueue, onLyrics, onMenu)
            }
        }
    }
}

/** The clock is observed only by progress components, never by library rows or shared metadata. */
@Composable
private fun rememberPlaybackClock(connection: PlayerConnection): State<Pair<Long, Long>> {
    val playback by connection.playbackState.collectAsState()
    val metadata by connection.mediaMetadata.collectAsState()
    return produceState(connection.player.currentPosition to connection.player.duration, connection, playback, metadata?.id) {
        while (isActive) {
            value = connection.player.currentPosition.coerceAtLeast(0) to connection.player.duration.coerceAtLeast(0)
            delay(250)
        }
    }
}

@Composable
private fun MiniProgress(connection: PlayerConnection, modifier: Modifier) {
    val clock by rememberPlaybackClock(connection)
    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
        LinearProgressIndicator(progress = { (clock.first.toFloat() / clock.second.coerceAtLeast(1)).coerceIn(0f, 1f) },
            modifier = modifier.height(2.dp), color = MaterialTheme.colorScheme.primary,
            trackColor = MaterialTheme.colorScheme.outlineVariant, drawStopIndicator = {})
    }
}

@Composable
private fun MusicProgress(connection: PlayerConnection, trackId: String) {
    val clock by rememberPlaybackClock(connection)
    var seeking by remember(trackId) { mutableStateOf<Float?>(null) }
    val duration = clock.second
    val position = seeking?.toLong() ?: clock.first
    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
        Column {
            Slider(value = position.toFloat().coerceIn(0f, duration.coerceAtLeast(1).toFloat()),
                onValueChange = { seeking = it }, enabled = duration > 0,
                onValueChangeFinished = { seeking?.let { connection.seekTo(it.toLong()) }; seeking = null },
                valueRange = 0f..duration.coerceAtLeast(1).toFloat(), modifier = Modifier.fillMaxWidth())
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(makeTimeString(position), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(makeTimeString(duration), style = MaterialTheme.typography.labelMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
        }
    }
}
