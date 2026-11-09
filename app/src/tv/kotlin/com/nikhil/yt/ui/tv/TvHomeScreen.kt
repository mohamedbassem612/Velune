package com.nikhil.yt.ui.tv

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.foundation.lazy.itemsIndexed
import androidx.compose.foundation.lazy.rememberLazyListState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
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
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import coil3.compose.AsyncImage
import com.nikhil.yt.R
import com.nikhil.yt.models.MediaMetadata

internal val TvBackground = Color(0xFF080B10)
internal val TvAccent = Color(0xFFF07888)

@Composable
internal fun TvHomeScreen(
    personal: List<MediaMetadata>,
    recommendations: List<MediaMetadata>,
    rediscover: List<MediaMetadata>,
    recent: List<MediaMetadata>,
    hero: MediaMetadata?,
    loading: Boolean,
    ready: Boolean,
    contentFocus: FocusRequester,
    railFocus: FocusRequester,
    onSearch: () -> Unit,
    onFocusTrack: (MediaMetadata) -> Unit,
    onPlay: (List<MediaMetadata>, MediaMetadata) -> Unit,
    onRadio: (MediaMetadata) -> Unit,
) {
    val state = rememberLazyListState()
    LazyColumn(
        state = state,
        modifier = Modifier.fillMaxSize(),
        contentPadding = PaddingValues(top = 28.dp, bottom = 24.dp),
        verticalArrangement = Arrangement.spacedBy(22.dp),
    ) {
        item(key = "hero") {
            Column(
                modifier = Modifier.fillMaxWidth().padding(horizontal = 28.dp).heightIn(min = 122.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp),
            ) {
                Text(stringResource(R.string.tv_home_eyebrow), color = TvAccent, fontSize = 12.sp, fontWeight = FontWeight.SemiBold)
                Text(
                    hero?.title ?: stringResource(R.string.tv_home_title),
                    modifier = Modifier.fillMaxWidth(.68f),
                    maxLines = 1, overflow = TextOverflow.Ellipsis,
                    style = MaterialTheme.typography.headlineMedium, color = Color.White,
                )
                Text(
                    hero?.artists?.joinToString { it.name } ?: stringResource(R.string.tv_home_subtitle),
                    modifier = Modifier.fillMaxWidth(.65f),
                    color = Color.White.copy(alpha = .65f), maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
                Row(horizontalArrangement = Arrangement.spacedBy(10.dp)) {
                    TvButton(
                        stringResource(if (hero == null || !ready) R.string.search else R.string.play),
                        onClick = { if (hero == null || !ready) onSearch() else onPlay(listOf(hero), hero) },
                        modifier = Modifier.focusRequester(contentFocus).focusProperties { left = railFocus },
                    )
                    if (hero != null) {
                        TvButton(stringResource(R.string.tv_start_radio), { onRadio(hero) }, enabled = ready)
                    }
                    if (loading) CircularProgressIndicator(Modifier.padding(8.dp).size(22.dp), strokeWidth = 2.dp, color = TvAccent)
                }
            }
        }
        item(key = "personal") {
            TvMusicShelf(
                title = stringResource(R.string.tv_personal_picks), songs = personal,
                emptyMessage = stringResource(R.string.tv_home_empty), railFocus = railFocus,
                ready = ready, onFocusTrack = onFocusTrack,
                onClick = { onPlay(personal, it) },
            )
        }
        item(key = "radio") {
            TvMusicShelf(
                title = stringResource(R.string.tv_your_radio), songs = personal.take(12),
                emptyMessage = stringResource(R.string.tv_radio_empty), railFocus = railFocus,
                radio = true, ready = ready, onFocusTrack = onFocusTrack, onClick = onRadio,
            )
        }
        if (recommendations.isNotEmpty()) {
            item(key = "discover") {
                TvMusicShelf(
                    title = stringResource(R.string.tv_discover_next), songs = recommendations,
                    railFocus = railFocus, ready = ready, onFocusTrack = onFocusTrack,
                    onClick = { onPlay(recommendations, it) },
                )
            }
        }
        if (rediscover.isNotEmpty()) {
            item(key = "rediscover") {
                TvMusicShelf(
                    title = stringResource(R.string.tv_rediscover), songs = rediscover,
                    railFocus = railFocus, ready = ready, onFocusTrack = onFocusTrack,
                    onClick = { onPlay(rediscover, it) },
                )
            }
        }
        if (recent.isNotEmpty()) {
            item(key = "recent") {
                TvMusicShelf(
                    title = stringResource(R.string.tv_recently_played), songs = recent,
                    railFocus = railFocus, ready = ready, onFocusTrack = onFocusTrack,
                    onClick = { onPlay(recent, it) },
                )
            }
        }
    }
}

@Composable
private fun TvMusicShelf(
    title: String,
    songs: List<MediaMetadata>,
    railFocus: FocusRequester,
    ready: Boolean,
    onFocusTrack: (MediaMetadata) -> Unit,
    onClick: (MediaMetadata) -> Unit,
    emptyMessage: String = "",
    radio: Boolean = false,
) {
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Row(Modifier.padding(horizontal = 28.dp), verticalAlignment = Alignment.CenterVertically) {
            if (radio) {
                Icon(painterResource(R.drawable.radio), null, Modifier.size(18.dp), tint = TvAccent)
                Spacer(Modifier.width(8.dp))
            }
            Text(title, fontWeight = FontWeight.SemiBold, fontSize = 18.sp, color = Color.White)
        }
        if (songs.isEmpty()) {
            Text(
                emptyMessage, color = Color.White.copy(alpha = .5f), fontSize = 13.sp,
                modifier = Modifier.padding(horizontal = 28.dp, vertical = 10.dp),
            )
        } else {
            LazyRow(
                contentPadding = PaddingValues(start = 28.dp, end = 28.dp, top = 4.dp, bottom = 6.dp),
                horizontalArrangement = Arrangement.spacedBy(14.dp),
            ) {
                itemsIndexed(songs, key = { index, song -> "$index:${song.id}" }) { index, song ->
                    TvTrackCard(
                        song = song, ready = ready, radio = radio,
                        modifier = if (index == 0) Modifier.focusProperties { left = railFocus } else Modifier,
                        onFocused = { onFocusTrack(song) }, onClick = { onClick(song) },
                    )
                }
            }
        }
    }
}

@Composable
internal fun TvTrackCard(
    song: MediaMetadata,
    ready: Boolean,
    modifier: Modifier = Modifier,
    radio: Boolean = false,
    onFocused: () -> Unit = {},
    onClick: () -> Unit,
) {
    var focused by remember { mutableStateOf(false) }
    Surface(
        onClick = onClick,
        enabled = ready,
        modifier = modifier.width(if (radio) 174.dp else 142.dp).onFocusChanged {
            focused = it.isFocused
            if (it.isFocused) onFocused()
        },
        color = if (focused) Color(0xFF21252E) else Color(0xFF11151C).copy(alpha = .84f),
        shape = RoundedCornerShape(12.dp),
        border = BorderStroke(2.dp, if (focused) Color.White else Color.White.copy(alpha = .06f)),
    ) {
        Column(Modifier.padding(7.dp), verticalArrangement = Arrangement.spacedBy(5.dp)) {
            Box(Modifier.fillMaxWidth().height(if (radio) 92.dp else 104.dp).clip(RoundedCornerShape(7.dp)).background(Color(0xFF202632))) {
                Icon(painterResource(R.drawable.music_note), null, Modifier.size(32.dp).align(Alignment.Center), tint = Color.White.copy(alpha = .15f))
                AsyncImage(song.thumbnailUrl, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
                if (radio) {
                    Box(Modifier.fillMaxSize().background(Brush.verticalGradient(listOf(Color.Transparent, Color.Black.copy(alpha = .8f)))))
                    Icon(painterResource(R.drawable.radio), null, Modifier.align(Alignment.BottomEnd).padding(8.dp).size(22.dp), tint = Color.White)
                } else if (focused) {
                    Surface(Modifier.align(Alignment.BottomEnd).padding(6.dp), color = Color.White, shape = RoundedCornerShape(50)) {
                        Icon(painterResource(R.drawable.play), null, Modifier.padding(6.dp).size(16.dp), tint = Color.Black)
                    }
                }
            }
            Text(song.title, color = Color.White, maxLines = 1, overflow = TextOverflow.Ellipsis, fontSize = 13.sp, fontWeight = FontWeight.Medium)
            Text(
                if (radio) stringResource(R.string.tv_radio_subtitle) else song.artists.joinToString { it.name },
                color = Color.White.copy(alpha = .5f), maxLines = 1, overflow = TextOverflow.Ellipsis, fontSize = 11.sp,
            )
        }
    }
}
