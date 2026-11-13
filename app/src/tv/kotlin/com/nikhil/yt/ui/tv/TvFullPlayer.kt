package com.nikhil.yt.ui.tv

import android.os.SystemClock
import android.view.View
import androidx.activity.compose.BackHandler
import androidx.compose.animation.*
import androidx.compose.animation.core.*
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.focusable
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
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
import androidx.compose.ui.graphics.StrokeCap
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.key.*
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.viewinterop.AndroidView
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import coil3.compose.AsyncImage
import coil3.imageLoader
import coil3.request.ImageRequest
import coil3.request.SuccessResult
import com.nikhil.yt.LocalPlayerConnection
import com.nikhil.yt.R
import com.nikhil.yt.db.entities.LyricsEntity
import com.nikhil.yt.extensions.metadata
import com.nikhil.yt.extensions.toggleRepeatMode
import com.nikhil.yt.playback.PlayerConnection
import com.nikhil.yt.constants.UseLyricsV2Key
import com.nikhil.yt.ui.component.Lyrics
import com.nikhil.yt.ui.component.LyricsV2
import com.nikhil.yt.ui.component.LocalMenuState
import com.nikhil.yt.ui.component.MenuState
import com.nikhil.yt.utils.rememberPreference
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
internal fun TvFullPlayer(connection: PlayerConnection, onClose: () -> Unit, onNavigate: (TvPage) -> Unit) {
    val track by connection.mediaMetadata.collectAsState()
    val playing by connection.isPlaying.collectAsState()
    val previous by connection.canSkipPrevious.collectAsState()
    val next by connection.canSkipNext.collectAsState()
    val shuffle by connection.shuffleModeEnabled.collectAsState()
    val repeat by connection.repeatMode.collectAsState()
    val queueWindows by connection.queueWindows.collectAsState()
    val currentItemIndex by connection.currentMediaItemIndex.collectAsState()
    val upNextItem = remember(connection, queueWindows, currentItemIndex, repeat, shuffle) {
        val nextIndex = connection.player.nextMediaItemIndex
        if (nextIndex in 0 until connection.player.mediaItemCount) connection.player.getMediaItemAt(nextIndex) else null
    }
    val song by connection.currentSong.collectAsState(initial = null)
    val error by connection.error.collectAsState()
    val playbackState by connection.playbackState.collectAsState()
    var panel by rememberSaveable { mutableStateOf<String?>(null) }
    val playFocus = remember { FocusRequester() }
    val likeFocus = remember { FocusRequester() }
    val shuffleFocus = remember { FocusRequester() }
    val idleFocus = remember { FocusRequester() }
    val upNextFocus = remember { FocusRequester() }
    var upNextFocused by remember { mutableStateOf(false) }
    val scope = rememberCoroutineScope()
    var screenVisible by remember { mutableStateOf(false) }
    var closing by remember { mutableStateOf(false) }
    val screenProgress by animateFloatAsState(if (screenVisible) 1f else 0f, tween(360, easing = FastOutSlowInEasing), label = "player entrance")
    LaunchedEffect(Unit) { screenVisible = true }
    fun closePlayer(target: TvPage? = null) {
        if (closing) return
        closing = true
        screenVisible = false
        scope.launch {
            delay(360)
            if (target == null) onClose() else onNavigate(target)
        }
    }
    var controlsVisible by remember { mutableStateOf(true) }
    var lastInteraction by remember { mutableLongStateOf(SystemClock.uptimeMillis()) }
    var wakeKey by remember { mutableStateOf<Key?>(null) }
    var position by remember(track?.id) { mutableLongStateOf(0L) }
    var duration by remember(track?.id) { mutableLongStateOf(0L) }
    var upNextDismissed by rememberSaveable(track?.id, currentItemIndex) { mutableStateOf(false) }
    val showUpNext = !controlsVisible && panel == null && upNextItem != null && !upNextDismissed &&
        duration > 0L && position >= (duration - 20_000L).coerceAtLeast(0L) && position < duration
    val bottomPadding by animateDpAsState(
        when { controlsVisible -> 32.dp; showUpNext -> 80.dp; else -> 72.dp },
        tween(450), label = "player inset",
    )
    val sidePadding by animateDpAsState(if (showUpNext) 60.dp else 52.dp, tween(450), label = "player side inset")
    val context = LocalContext.current
    val artist = track?.artists?.firstOrNull()
    var photos by remember(artist?.name) { mutableStateOf(emptyList<String>()) }
    var background by remember(artist?.name) { mutableStateOf<String?>(null) }
    LaunchedEffect(artist) {
        if (artist != null) TvArtistPhotos.load(artist) { photos = it }
    }
    LaunchedEffect(photos) {
        if (photos.isNotEmpty()) {
            val shuffled = photos.shuffled()
            var index = 0
            var failures = 0
            while (isActive) {
                val candidate = shuffled[index++ % shuffled.size]
                val request = ImageRequest.Builder(context).data(candidate).size(1920, 1080).build()
                // Keep the last decoded photo until the next one is ready; broken URLs never blank it.
                if (context.imageLoader.execute(request) is SuccessResult) {
                    background = candidate
                    failures = 0
                    delay(30_000)
                } else {
                    failures++
                    delay(if (failures % shuffled.size == 0) 15_000 else 500)
                }
            }
        }
    }
    LaunchedEffect(panel, controlsVisible, showUpNext, if (showUpNext) upNextItem?.mediaId else null) {
        if (panel == null) {
            when {
                controlsVisible -> playFocus.requestFocus()
                showUpNext -> upNextFocus.requestFocus()
                else -> idleFocus.requestFocus()
            }
        }
    }
    LaunchedEffect(lastInteraction, panel) {
        if (panel == null) {
            delay(12_000)
            controlsVisible = false
        }
    }
    LaunchedEffect(connection, track?.id) {
        while (isActive) {
            position = connection.player.currentPosition.coerceAtLeast(0)
            duration = connection.player.duration.takeIf { it != C.TIME_UNSET && it > 0 } ?: 0
            delay(250)
        }
    }
    fun handleBack() {
        if (closing) return
        when {
            panel != null -> panel = null
            showUpNext -> upNextDismissed = true
            controlsVisible -> controlsVisible = false
            else -> closePlayer()
        }
    }
    BackHandler { handleBack() }
    Box(Modifier.fillMaxSize().graphicsLayer {
        alpha = screenProgress
        translationY = (1f - screenProgress) * 72.dp.toPx()
    }.background(Color.Black).onPreviewKeyEvent { event ->
        if (closing) return@onPreviewKeyEvent true
        // Handle the complete remote Back press before a focused child can consume it.
        // The dispatcher remains a fallback for system Back actions without a key event.
        if (event.key == Key.Back || event.key == Key.Escape) {
            if (event.type == KeyEventType.KeyUp) handleBack()
            return@onPreviewKeyEvent true
        }
        if (event.key == wakeKey) {
            if (event.type == KeyEventType.KeyUp) wakeKey = null
            return@onPreviewKeyEvent true
        }
        if (event.type == KeyEventType.KeyDown) {
            lastInteraction = SystemClock.uptimeMillis()
            val activateUpNext = showUpNext && upNextFocused && event.key in listOf(Key.DirectionCenter, Key.Enter, Key.NumPadEnter, Key.Spacebar)
            if (!controlsVisible && !activateUpNext) {
                controlsVisible = true
                wakeKey = event.key
                return@onPreviewKeyEvent true
            }
        }
        false
    }.focusRequester(idleFocus).focusable()) {
        // This view owns the flag only while this screen is mounted, including paused playback.
        AndroidView(factory = { View(it).apply { keepScreenOn = true } }, modifier = Modifier.size(1.dp))
        // Prefer artist photography, but keep the player from going black when
        // every artist-photo source fails by falling back to the album artwork.
        TvArtistBackdrop(
            background,
            track?.thumbnailUrl ?: connection.player.currentMediaItem?.mediaMetadata?.artworkUri?.toString(),
        )
        AnimatedVisibility(controlsVisible && panel == null, modifier = Modifier.align(Alignment.TopStart),
            enter = fadeIn(tween(450)) + slideInVertically(tween(450)) { -it / 5 },
            exit = fadeOut(tween(350)) + slideOutVertically(tween(450)) { -it / 5 }) {
        Column(Modifier.padding(28.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            TvPage.entries.filter { it != TvPage.Settings }.forEach { target ->
                TvPanelButton(target.icon, stringResource(target.label)) { closePlayer(target) }
            }
        }
        }
        if (panel == null) Column(Modifier.align(Alignment.BottomCenter).fillMaxWidth().padding(start = sidePadding, end = sidePadding, bottom = bottomPadding)) {
            AnimatedContent(showUpNext, modifier = Modifier.fillMaxWidth(), contentAlignment = Alignment.BottomStart,
                transitionSpec = {
                    (fadeIn(tween(400)) + slideInVertically(tween(450)) { it / 5 }) togetherWith
                        (fadeOut(tween(250)) + slideOutVertically(tween(350)) { -it / 5 })
                }, label = "up next replaces current track") { upcoming ->
            // Keep the animation full-width, but give its children loose minimum constraints.
            Box(Modifier.fillMaxWidth(), contentAlignment = Alignment.BottomStart) {
            if (upcoming) {
                upNextItem?.let { item ->
                    TvUpNextCard(item, Modifier.focusRequester(upNextFocus),
                        onFocusChanged = { upNextFocused = it },
                        onClick = { connection.seekToNext() })
                }
            } else {
            Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.Bottom, horizontalArrangement = Arrangement.spacedBy(18.dp)) {
                Box(Modifier.size(108.dp).clip(RoundedCornerShape(8.dp)).background(Color.White.copy(alpha = .08f))) {
                    Icon(painterResource(R.drawable.music_note), null, Modifier.align(Alignment.Center).size(36.dp))
                    AsyncImage(track?.thumbnailUrl, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
                }
                Column(Modifier.weight(1f).padding(bottom = 6.dp)) {
                    Text(track?.title.orEmpty(), fontSize = 26.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Text(track?.album?.title ?: track?.artists?.joinToString { it.name }.orEmpty(), color = Color.White.copy(alpha = .65f), fontSize = 15.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                }
                AnimatedVisibility(controlsVisible, enter = fadeIn(tween(400)), exit = fadeOut(tween(300))) {
                TvPlayerAction(if (song?.song?.liked == true) R.drawable.favorite else R.drawable.favorite_border,
                    stringResource(if (song?.song?.liked == true) R.string.tv_unlike else R.string.tv_like), track != null,
                    modifier = Modifier.focusRequester(likeFocus)) { connection.toggleLike() }
                }
            }
            }
            }
            }
            AnimatedVisibility(controlsVisible,
                enter = fadeIn(tween(400)) + expandVertically(tween(450), expandFrom = Alignment.Bottom) + slideInVertically(tween(450)) { it / 3 },
                exit = fadeOut(tween(300)) + shrinkVertically(tween(450), shrinkTowards = Alignment.Bottom) + slideOutVertically(tween(450)) { it / 3 }) {
            Column {
                Spacer(Modifier.height(12.dp))
                if (error != null) Text(stringResource(R.string.tv_playback_failed), color = MaterialTheme.colorScheme.error)
                Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
                    TvPlayerAction(if (playing) R.drawable.pause else R.drawable.play,
                        stringResource(if (playing) R.string.tv_pause else R.string.play), track != null,
                        prominent = true, modifier = Modifier.focusRequester(playFocus)) { connection.togglePlayPause() }
                    Column(Modifier.weight(1f)) {
                        TvSeekBar(position, duration, likeFocus, shuffleFocus, playFocus) { target ->
                            position = target
                            connection.seekTo(target)
                        }
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                            Text(tvPlayerTime(position), fontSize = 11.sp, color = Color.White.copy(alpha = .65f))
                            if (playbackState == Player.STATE_BUFFERING) CircularProgressIndicator(Modifier.size(14.dp), strokeWidth = 2.dp, color = Color.White)
                            Text("−" + tvPlayerTime((duration - position).coerceAtLeast(0)), fontSize = 11.sp, color = Color.White.copy(alpha = .65f))
                        }
                    }
                }
                Row(Modifier.fillMaxWidth().padding(top = 6.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(6.dp)) {
                    TvPlayerAction(R.drawable.skip_previous, stringResource(R.string.tv_previous), previous) { connection.seekToPrevious() }
                    TvPlayerAction(R.drawable.skip_next, stringResource(R.string.tv_next), next) { connection.seekToNext() }
                    TvPlayerAction(if (shuffle) R.drawable.shuffle_on else R.drawable.shuffle, stringResource(R.string.shuffle), track != null,
                        modifier = Modifier.focusRequester(shuffleFocus)) { connection.player.shuffleModeEnabled = !shuffle }
                    TvPlayerAction(when (repeat) { Player.REPEAT_MODE_ONE -> R.drawable.repeat_one_on; Player.REPEAT_MODE_ALL -> R.drawable.repeat_on; else -> R.drawable.repeat },
                        stringResource(when (repeat) { Player.REPEAT_MODE_ONE -> R.string.repeat_mode_one; Player.REPEAT_MODE_ALL -> R.string.repeat_mode_all; else -> R.string.repeat_mode_off }), track != null) { connection.player.toggleRepeatMode() }
                    Spacer(Modifier.weight(1f))
                    TvPanelButton(R.drawable.queue_music, stringResource(R.string.queue)) { panel = "queue" }
                    TvPanelButton(R.drawable.lyrics, stringResource(R.string.lyrics)) { panel = "lyrics" }
                    TvPlayerAction(R.drawable.expand_more, stringResource(R.string.close_dialog), true, onClick = { closePlayer() })
                }
            }
            }
        }
        if (panel != null) TvPlayerPanel(connection, panel == "lyrics", onClose = { panel = null })
    }
}

@Composable
private fun TvUpNextCard(item: MediaItem, modifier: Modifier = Modifier, onFocusChanged: (Boolean) -> Unit, onClick: () -> Unit) {
    var focused by remember { mutableStateOf(false) }
    val metadata = item.metadata
    Surface(
        onClick = onClick,
        modifier = modifier.width(212.dp).height(168.dp).onFocusChanged {
            focused = it.isFocused
            onFocusChanged(it.isFocused)
        },
        color = TvSurface,
        contentColor = Color.White,
        shape = RoundedCornerShape(8.dp),
        border = BorderStroke(if (focused) 1.dp else .75.dp, Color.White.copy(alpha = if (focused) .75f else .3f)),
    ) {
        Column(Modifier.padding(8.dp), verticalArrangement = Arrangement.spacedBy(6.dp)) {
            Text(stringResource(R.string.tv_up_next), color = Color.White.copy(alpha = .65f), fontSize = 10.sp, lineHeight = 12.sp)
            Box(Modifier.align(Alignment.CenterHorizontally).width(128.dp).height(88.dp).clip(RoundedCornerShape(3.dp))) {
                Icon(painterResource(R.drawable.music_note), null, Modifier.align(Alignment.Center).size(24.dp), tint = Color.White.copy(alpha = .3f))
                AsyncImage(metadata?.thumbnailUrl ?: item.mediaMetadata.artworkUri, null, Modifier.fillMaxSize(), contentScale = ContentScale.Fit)
            }
            Text(metadata?.title ?: item.mediaMetadata.title?.toString() ?: stringResource(R.string.tv_next),
                maxLines = 1, overflow = TextOverflow.Ellipsis, fontSize = 14.sp, lineHeight = 18.sp)
            Text(metadata?.artists?.joinToString { it.name }?.takeIf { it.isNotBlank() } ?: item.mediaMetadata.artist?.toString().orEmpty(),
                maxLines = 1, overflow = TextOverflow.Ellipsis, fontSize = 10.sp, lineHeight = 14.sp, color = Color.White.copy(alpha = .65f))
        }
    }
}

@Composable
private fun TvArtistBackdrop(url: String?, fallbackUrl: String?) {
    val displayUrl = url ?: fallbackUrl
    if (displayUrl == null) return
    Box(Modifier.fillMaxSize()) {
        Crossfade(displayUrl, animationSpec = tween(900), modifier = Modifier.align(Alignment.TopEnd).fillMaxWidth(.82f).fillMaxHeight(.86f), label = "artist photo") { photo ->
            AsyncImage(photo, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop, alpha = .73f)
        }
        Box(Modifier.fillMaxSize().background(Brush.horizontalGradient(colorStops = arrayOf(0f to Color.Black, .28f to Color.Black.copy(alpha = .97f), .62f to Color.Black.copy(alpha = .64f), 1f to Color.Black.copy(alpha = .1f)))))
        Box(Modifier.fillMaxSize().background(Brush.verticalGradient(colorStops = arrayOf(0f to Color.Black.copy(alpha = .08f), .3f to Color.Black.copy(alpha = .24f), .72f to Color.Black.copy(alpha = .94f), 1f to Color.Black))))
    }
}

@Composable
private fun TvSeekBar(position: Long, duration: Long, up: FocusRequester, down: FocusRequester, left: FocusRequester, onSeek: (Long) -> Unit) {
    var focused by remember { mutableStateOf(false) }
    val fraction by animateFloatAsState(
        if (duration > 0) (position.toFloat() / duration).coerceIn(0f, 1f) else 0f,
        tween(180, easing = FastOutSlowInEasing), label = "seek progress",
    )
    Canvas(Modifier.fillMaxWidth().height(32.dp)
        .focusProperties { this.up = up; this.down = down; this.left = left }
        .onFocusChanged { focused = it.isFocused }
        .onPreviewKeyEvent { event ->
            when (event.key) {
                Key.DirectionUp, Key.DirectionDown -> {
                    if (event.type == KeyEventType.KeyDown) {
                        if (event.key == Key.DirectionUp) up.requestFocus() else down.requestFocus()
                    }
                    true
                }
                Key.DirectionLeft, Key.DirectionRight -> {
                    if (duration > 0 && event.type == KeyEventType.KeyDown && event.nativeKeyEvent.repeatCount == 0) {
                        onSeek((position + if (event.key == Key.DirectionRight) 10_000 else -10_000).coerceIn(0L, duration))
                    }
                    true
                }
                else -> false
            }
        }.focusable(enabled = duration > 0)) {
        val radius = if (focused) 7.dp.toPx() else 4.dp.toPx()
        val start = Offset(8.dp.toPx(), size.height / 2)
        val end = Offset(size.width - 8.dp.toPx(), start.y)
        val thumb = Offset(start.x + (end.x - start.x) * fraction, start.y)
        if (focused) drawRoundRect(Color.White.copy(alpha = .08f), cornerRadius = androidx.compose.ui.geometry.CornerRadius(12.dp.toPx()))
        drawLine(Color.White.copy(alpha = .2f), start, end, 3.dp.toPx(), StrokeCap.Round)
        drawLine(Color.White, start, thumb, 3.dp.toPx(), StrokeCap.Round)
        drawCircle(Color.White, radius, thumb)
    }
}

@Composable
private fun TvPanelButton(icon: Int, label: String, onClick: () -> Unit) {
    var focused by remember { mutableStateOf(false) }
    Surface(onClick = onClick,
        modifier = Modifier.onFocusChanged { focused = it.isFocused },
        color = if (focused) Color.White else Color.Black,
        contentColor = if (focused) Color.Black else Color.White,
        shape = RoundedCornerShape(24.dp),
        border = BorderStroke(2.dp, if (focused) TvAccent else Color.Transparent),
    ) {
        Row(Modifier.padding(horizontal = 18.dp, vertical = 10.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(painterResource(icon), null, Modifier.size(24.dp))
            Spacer(Modifier.width(10.dp))
            Text(label)
        }
    }
}

@Composable
private fun TvPlayerPanel(connection: PlayerConnection, lyrics: Boolean, onClose: () -> Unit) {
    val windows by connection.queueWindows.collectAsState()
    val currentIndex by connection.currentWindowIndex.collectAsState()
    val currentLyrics by connection.currentLyrics.collectAsState(initial = null)
    val track by connection.mediaMetadata.collectAsState()
    val focus = remember { FocusRequester() }
    val listState = rememberLazyListState()
    var loading by remember(track?.id) { mutableStateOf(false) }
    var fetched by remember(track?.id) { mutableStateOf<String?>(null) }
    val useLyricsV2 by rememberPreference(UseLyricsV2Key, false)
    val menuState = remember { MenuState() }
    LaunchedEffect(Unit) { focus.requestFocus() }
    LaunchedEffect(currentIndex, lyrics) {
        if (!lyrics && currentIndex in windows.indices) listState.scrollToItem(currentIndex)
    }
    LaunchedEffect(track?.id, lyrics) {
        val metadata = track
        if (lyrics && metadata != null && currentLyrics == null) {
            loading = true
            try {
                fetched = withContext(Dispatchers.IO) { connection.service.lyricsHelper.getLyrics(metadata) }
                fetched?.let { text -> connection.database.query { upsert(LyricsEntity(metadata.id, text)) } }
            } catch (cancel: CancellationException) { throw cancel
            } catch (_: Exception) { /* The panel keeps a readable empty state. */
            } finally { loading = false }
        }
    }
    Surface(Modifier.fillMaxSize(), color = Color.Black) {
        Column(Modifier.padding(horizontal = 52.dp, vertical = 28.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Text(stringResource(if (lyrics) R.string.lyrics else R.string.queue), fontSize = 26.sp, modifier = Modifier.weight(1f))
                TvPlayerAction(R.drawable.expand_more, stringResource(R.string.close_dialog), true, modifier = Modifier.focusRequester(focus), onClick = onClose)
            }
            Spacer(Modifier.height(16.dp))
            if (lyrics) {
                if (loading) CircularProgressIndicator(color = Color.White)
                else CompositionLocalProvider(LocalPlayerConnection provides connection, LocalMenuState provides menuState) {
                    Box(Modifier.fillMaxWidth().weight(1f)) {
                        if (useLyricsV2) LyricsV2(
                            sliderPositionProvider = { null }, keepScreenOn = false,
                            contentColor = Color.White, showResumeAutoscrollButton = false,
                        ) else Lyrics(
                            sliderPositionProvider = { null }, keepScreenOn = false,
                            contentColor = Color.White, showResumeAutoscrollButton = false,
                        )
                    }
                }
            } else LazyColumn(state = listState, verticalArrangement = Arrangement.spacedBy(8.dp)) {
                itemsIndexed(windows, key = { _, window -> window.uid.toString() }) { index, window ->
                    val metadata = window.mediaItem.metadata
                    TvButton(
                        listOfNotNull(metadata?.title ?: window.mediaItem.mediaMetadata.title?.toString(), metadata?.artists?.joinToString { it.name }).joinToString(" · "),
                        { connection.player.seekToDefaultPosition(window.firstPeriodIndex); connection.player.prepare(); connection.player.play() },
                        Modifier.fillMaxWidth(), selected = index == currentIndex,
                    )
                }
            }
        }
    }
}

private fun tvPlayerTime(ms: Long): String {
    val seconds = ms.coerceAtLeast(0) / 1000
    return if (seconds >= 3600) "%d:%02d:%02d".format(seconds / 3600, seconds / 60 % 60, seconds % 60)
    else "%d:%02d".format(seconds / 60, seconds % 60)
}
