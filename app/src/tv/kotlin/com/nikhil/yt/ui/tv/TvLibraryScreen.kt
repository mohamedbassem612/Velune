package com.nikhil.yt.ui.tv

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.lazy.LazyRow
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.nikhil.yt.R
import com.nikhil.yt.db.MusicDatabase
import com.nikhil.yt.models.MediaMetadata
import com.nikhil.yt.models.toMediaMetadata

@Composable
internal fun TvLibraryScreen(
    database: MusicDatabase, ready: Boolean,
    selected: Int, onSelect: (Int) -> Unit,
    contentFocus: FocusRequester, railFocus: FocusRequester,
    onOpen: (TvCollection) -> Unit,
    onFocusTrack: (MediaMetadata) -> Unit,
    onPlay: (List<MediaMetadata>, MediaMetadata) -> Unit,
) {
    val playlists by remember(database) { database.playlistsByCreateDateAsc() }.collectAsState(initial = emptyList())
    val albums by remember(database) { database.albumsLikedByCreateDateAsc() }.collectAsState(initial = emptyList())
    val libraryArtists by remember(database) { database.artistsByCreateDateAsc() }.collectAsState(initial = emptyList())
    val followedArtists by remember(database) { database.artistsBookmarkedByCreateDateAsc() }.collectAsState(initial = emptyList())
    val history by remember(database) { database.recentEvents(100) }.collectAsState(initial = emptyList())
    val liked = TvCollection(TV_LIKED_ID, stringResource(R.string.tv_liked), local = true, artwork = R.drawable.tv_liked_cover)
    val playlistCards = listOf(liked) + playlists.asReversed().map {
        TvCollection(it.id, it.title, it.thumbnails.firstOrNull(), local = true)
    }
    val albumCards = albums.asReversed().map {
        TvCollection(it.id, it.title, it.thumbnailUrl, it.artists.joinToString { artist -> artist.name }, album = true, year = it.album.year, local = true)
    }
    val artistCards = (followedArtists.asReversed() + libraryArtists.asReversed()).distinctBy { it.id }.map {
        TvCollection(it.id, it.title, it.thumbnailUrl, local = true, artist = true)
    }
    val recent = history.map { it.song.toMediaMetadata() }.distinctBy { it.id }
    Column(Modifier.fillMaxSize().padding(top = 22.dp)) {
        Text(stringResource(R.string.filter_library), style = MaterialTheme.typography.headlineMedium, modifier = Modifier.padding(horizontal = 28.dp))
        LazyRow(contentPadding = PaddingValues(horizontal = 28.dp, vertical = 16.dp), horizontalArrangement = Arrangement.spacedBy(10.dp)) {
            val tabs = listOf(R.string.filter_all, R.string.playlists, R.string.albums, R.string.artists, R.string.history)
            tabs.forEachIndexed { index, label ->
                item(key = label) {
                    TvButton(stringResource(label), { onSelect(index) },
                        modifier = if (index == 0) Modifier.focusRequester(contentFocus).focusProperties { left = railFocus } else Modifier,
                        selected = selected == index,
                    )
                }
            }
        }
        if (selected == 4) TvSongGrid(
            recent, ready, stringResource(R.string.no_results_found), railFocus,
            onFocusTrack, onPlay = { onPlay(recent, it) },
        ) else TvCollectionGrid(
            collections = when (selected) {
                1 -> playlistCards
                2 -> albumCards
                3 -> artistCards
                else -> playlistCards + albumCards + artistCards
            },
            railFocus = railFocus, onOpen = onOpen,
            message = stringResource(R.string.tv_collections_empty),
        )
    }
}
