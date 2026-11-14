package com.nikhil.yt.ui.tv

import androidx.activity.compose.BackHandler
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.focusGroup
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.lazy.itemsIndexed
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
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import androidx.compose.ui.window.Dialog
import androidx.compose.ui.window.DialogProperties
import coil3.compose.AsyncImage
import com.nikhil.yt.R
import com.nikhil.yt.db.entities.Playlist
import com.nikhil.yt.models.MediaMetadata

@Composable
internal fun TvCollectionScreen(
    state: TvCollectionState,
    ready: Boolean,
    likedIds: Set<String>,
    playlists: List<Playlist>,
    onBack: () -> Unit,
    onRetry: () -> Unit,
    onSave: () -> Unit,
    onPlay: (List<MediaMetadata>, Int) -> Unit,
    onQueue: (List<MediaMetadata>) -> Unit,
    onNext: (MediaMetadata) -> Unit,
    onLike: (MediaMetadata) -> Unit,
    onAddToPlaylist: (MediaMetadata, Playlist?, String) -> Unit,
) {
    val collection = state.collection ?: return
    val backFocus = remember(collection.id) { FocusRequester() }
    var menuSong by remember(collection.id) { mutableStateOf<MediaMetadata?>(null) }
    var menuOpener by remember { mutableStateOf<FocusRequester?>(null) }
    LaunchedEffect(collection.id) { backFocus.requestFocus() }
    BackHandler { onBack() }
    val actionable = ready && !state.loading && !state.failed && state.songs.isNotEmpty()
    Column(Modifier.fillMaxSize().padding(horizontal = 26.dp, vertical = 16.dp)) {
        Row(verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            TvPlayerAction(R.drawable.arrow_back, stringResource(R.string.tv_back), true, modifier = Modifier.focusRequester(backFocus), onClick = onBack)
            Text(stringResource(collection.typeLabel), color = Color.White.copy(alpha = .5f), fontSize = 12.sp)
        }
        Row(Modifier.weight(1f).padding(top = 12.dp), horizontalArrangement = Arrangement.spacedBy(30.dp)) {
            // No dividers: spacing and surface contrast separate artwork and track list.
            Column(Modifier.width(224.dp), verticalArrangement = Arrangement.spacedBy(9.dp)) {
                TvCollectionArtwork(collection.thumbnail, Modifier.size(150.dp).align(Alignment.CenterHorizontally), artwork = collection.artwork, artist = collection.artist)
                Text(collection.title, fontSize = 21.sp, fontWeight = FontWeight.SemiBold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                Text(
                    if (collection.album) listOfNotNull(stringResource(R.string.tv_album), collection.year?.toString()).joinToString(" · ") else collection.subtitle,
                    color = Color.White.copy(alpha = .55f), fontSize = 12.sp, maxLines = 1, overflow = TextOverflow.Ellipsis,
                )
                if (collection.album && collection.subtitle.isNotBlank()) Text(collection.subtitle, color = Color.White.copy(alpha = .55f), fontSize = 11.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (!state.loading && !state.failed) Text(stringResource(R.string.tv_collection_count, state.songs.size), color = Color.White.copy(alpha = .45f), fontSize = 11.sp)
                Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly) {
                    TvPlayerAction(if (state.saved) R.drawable.check else R.drawable.add, stringResource(if (state.saved) R.string.tv_saved_library else R.string.add_to_library), !state.loading && !state.failed && !state.saving && !state.saved, onClick = onSave)
                    TvPlayerAction(R.drawable.play, stringResource(R.string.play), actionable, prominent = true) { onPlay(state.songs, 0) }
                    TvPlayerAction(R.drawable.queue_music, stringResource(R.string.add_to_queue), actionable) { onQueue(state.songs) }
                }
                TvButton(stringResource(R.string.shuffle), {
                    val shuffled = state.songs.shuffled()
                    onPlay(shuffled, 0)
                }, Modifier.align(Alignment.CenterHorizontally), enabled = actionable)
            }
            Column(Modifier.weight(1f)) {
                when {
                    state.loading -> Row(Modifier.padding(24.dp), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
                        CircularProgressIndicator(Modifier.size(22.dp), color = TvAccent)
                        Text(stringResource(R.string.loading), fontSize = 13.sp)
                    }
                    state.failed -> Column(Modifier.padding(24.dp), verticalArrangement = Arrangement.spacedBy(16.dp)) {
                        Text(stringResource(R.string.tv_collection_failed), color = Color.White.copy(alpha = .65f))
                        TvButton(stringResource(R.string.retry), onRetry)
                    }
                    state.songs.isEmpty() -> Text(stringResource(R.string.no_results_found), Modifier.padding(24.dp), color = Color.White.copy(alpha = .6f))
                    else -> LazyColumn(verticalArrangement = Arrangement.spacedBy(6.dp), contentPadding = PaddingValues(bottom = 16.dp)) {
                        itemsIndexed(state.songs, key = { index, song -> "$index:${song.id}" }) { index, song ->
                            val moreFocus = remember { FocusRequester() }
                            TvCollectionSongRow(
                                song, index, song.id in likedIds, ready, moreFocus,
                                onPlay = { onPlay(state.songs, index) },
                                onLike = { onLike(song) },
                                onMore = { menuOpener = moreFocus; menuSong = song },
                            )
                        }
                    }
                }
            }
        }
    }
    menuSong?.let { song ->
        TvSongActionsPanel(
            song, playlists, ready,
            onClose = { menuSong = null; menuOpener?.requestFocus() },
            onNext = { onNext(song) },
            onQueue = { onQueue(listOf(song)) },
            onAdd = { playlist, name -> onAddToPlaylist(song, playlist, name) },
        )
    }
}

@Composable
internal fun TvCollectionArtwork(url: String?, modifier: Modifier = Modifier, artwork: Int? = null, artist: Boolean = false) {
    Box(modifier.clip(RoundedCornerShape(10.dp)).background(TvSurface), contentAlignment = Alignment.Center) {
        if (artwork != null) Image(painterResource(artwork), null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        else {
            Icon(painterResource(if (artist) R.drawable.artist else R.drawable.album), null, Modifier.size(42.dp), tint = Color.White.copy(alpha = .15f))
            AsyncImage(url, null, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        }
    }
}

@Composable
private fun TvCollectionSongRow(
    song: MediaMetadata, index: Int, liked: Boolean, ready: Boolean, moreFocus: FocusRequester,
    onPlay: () -> Unit, onLike: () -> Unit, onMore: () -> Unit,
) {
    var focused by remember { mutableStateOf(false) }
    Row(
        Modifier.fillMaxWidth().height(64.dp).onFocusChanged { focused = it.hasFocus }
            .clip(RoundedCornerShape(10.dp)).background(if (focused) Color.White.copy(alpha = .12f) else TvSurface.copy(alpha = .65f)).focusGroup(),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        var playFocused by remember { mutableStateOf(false) }
        Surface(
            onClick = onPlay, enabled = ready,
            modifier = Modifier.weight(1f).fillMaxHeight().onFocusChanged { playFocused = it.isFocused },
            shape = RoundedCornerShape(10.dp), color = Color.Transparent,
            border = BorderStroke(2.dp, if (playFocused) TvAccent else Color.Transparent),
        ) {
            Row(Modifier.padding(horizontal = 10.dp, vertical = 8.dp), horizontalArrangement = Arrangement.spacedBy(10.dp), verticalAlignment = Alignment.CenterVertically) {
                Text("${index + 1}", Modifier.width(20.dp), color = Color.White.copy(alpha = .4f), fontSize = 10.sp)
                TvCollectionArtwork(song.thumbnailUrl, Modifier.size(46.dp))
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Text(song.title, fontSize = 13.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    Row(horizontalArrangement = Arrangement.spacedBy(5.dp), verticalAlignment = Alignment.CenterVertically) {
                        if (song.explicit) Text("E", color = Color.White.copy(alpha = .6f), fontSize = 9.sp, fontWeight = FontWeight.Bold)
                        Text(song.album?.title ?: song.artists.joinToString { it.name }, color = Color.White.copy(alpha = .5f), fontSize = 10.sp, maxLines = 1, overflow = TextOverflow.Ellipsis)
                    }
                }
                if (song.duration > 0) Text("${song.duration / 60}:${(song.duration % 60).toString().padStart(2, '0')}", color = Color.White.copy(alpha = .4f), fontSize = 10.sp)
            }
        }
        TvPlayerAction(if (liked) R.drawable.favorite else R.drawable.favorite_border, stringResource(if (liked) R.string.tv_unlike else R.string.tv_like), true, onClick = onLike)
        TvPlayerAction(R.drawable.more_horiz, stringResource(R.string.tv_song_options), true, modifier = Modifier.focusRequester(moreFocus), onClick = onMore)
        Spacer(Modifier.width(6.dp))
    }
}

@Composable
private fun TvSongActionsPanel(
    song: MediaMetadata, playlists: List<Playlist>, ready: Boolean,
    onClose: () -> Unit, onNext: () -> Unit, onQueue: () -> Unit, onAdd: (Playlist?, String) -> Unit,
) {
    var choosing by remember { mutableStateOf(false) }
    var name by remember { mutableStateOf("") }
    val firstFocus = remember { FocusRequester() }
    Dialog(onDismissRequest = onClose, properties = DialogProperties(usePlatformDefaultWidth = false)) {
        LaunchedEffect(choosing) { firstFocus.requestFocus() }
        Box(Modifier.fillMaxSize().background(Color.Black.copy(alpha = .45f)), contentAlignment = Alignment.CenterEnd) {
            Surface(Modifier.width(270.dp).fillMaxHeight(), color = TvSurface) {
                LazyColumn(Modifier.padding(22.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                    item {
                        TvPlayerAction(R.drawable.arrow_back, stringResource(R.string.tv_back), true, modifier = Modifier.focusRequester(firstFocus)) { if (choosing) choosing = false else onClose() }
                        Spacer(Modifier.height(14.dp))
                        TvCollectionArtwork(song.thumbnailUrl, Modifier.size(106.dp))
                        Spacer(Modifier.height(12.dp))
                        Text(song.title, fontSize = 18.sp, fontWeight = FontWeight.SemiBold, maxLines = 2, overflow = TextOverflow.Ellipsis)
                        Text(song.artists.joinToString { it.name }, color = Color.White.copy(alpha = .5f), fontSize = 12.sp, maxLines = 2)
                        Spacer(Modifier.height(16.dp))
                    }
                    if (!choosing) {
                        item { TvButton(stringResource(R.string.play_next), { onNext(); onClose() }, Modifier.fillMaxWidth(), enabled = ready) }
                        item { TvButton(stringResource(R.string.add_to_queue), { onQueue(); onClose() }, Modifier.fillMaxWidth(), enabled = ready) }
                        item { TvButton(stringResource(R.string.add_to_playlist), { choosing = true }, Modifier.fillMaxWidth()) }
                    } else {
                        item { Text(stringResource(R.string.add_to_playlist), fontSize = 14.sp) }
                        items(playlists.filter { it.playlist.isEditable }, key = { it.id }) { playlist ->
                            TvButton(playlist.title, { onAdd(playlist, ""); onClose() }, Modifier.fillMaxWidth())
                        }
                        item {
                            OutlinedTextField(name, { name = it }, label = { Text(stringResource(R.string.playlist_name)) }, singleLine = true, modifier = Modifier.fillMaxWidth())
                        }
                        item { TvButton(stringResource(R.string.create_playlist), { onAdd(null, name); onClose() }, Modifier.fillMaxWidth(), enabled = name.isNotBlank()) }
                    }
                }
            }
        }
    }
}
