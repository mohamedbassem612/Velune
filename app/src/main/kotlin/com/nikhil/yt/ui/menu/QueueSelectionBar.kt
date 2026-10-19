package com.nikhil.yt.ui.menu

import android.widget.Toast
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.RowScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import androidx.media3.common.Player
import androidx.media3.common.Timeline
import com.nikhil.yt.LocalDatabase
import com.nikhil.yt.LocalPlayerConnection
import com.nikhil.yt.R
import com.nikhil.yt.extensions.toMediaItem
import com.nikhil.yt.models.MediaMetadata

/**
 * Bottom bar shown in the queue while songs are selected.
 * Actions: play next, add to playlist, like, remove from queue.
 */
@Composable
fun QueueSelectionBar(
    songSelection: List<MediaMetadata>,
    currentItems: List<Timeline.Window>,
    allSelected: Boolean,
    canRemove: Boolean,
    onSelectAll: () -> Unit,
    onFinish: () -> Unit,
    modifier: Modifier = Modifier,
) {
    val context = LocalContext.current
    val database = LocalDatabase.current
    val playerConnection = LocalPlayerConnection.current ?: return

    var showChoosePlaylistDialog by remember { mutableStateOf(false) }

    AddToPlaylistDialog(
        isVisible = showChoosePlaylistDialog,
        onGetSong = {
            songSelection.map {
                database.insert(it)
                it.id
            }
        },
        onDismiss = { showChoosePlaylistDialog = false },
        onAddComplete = { songCount, playlistNames ->
            val message = when {
                songCount == 1 && playlistNames.size == 1 -> context.getString(R.string.added_to_playlist, playlistNames.first())
                songCount > 1 && playlistNames.size == 1 -> context.getString(R.string.added_n_songs_to_playlist, songCount, playlistNames.first())
                songCount == 1 -> context.getString(R.string.added_to_n_playlists, playlistNames.size)
                else -> context.getString(R.string.added_n_songs_to_n_playlists, songCount, playlistNames.size)
            }
            Toast.makeText(context, message, Toast.LENGTH_SHORT).show()
            onFinish()
        },
    )

    val hasSelection = songSelection.isNotEmpty()
    val allLiked = hasSelection && songSelection.all { it.liked }

    Surface(
        modifier = modifier.fillMaxWidth(),
        tonalElevation = 6.dp,
        shadowElevation = 8.dp,
    ) {
        Column {
            Row(
                modifier = Modifier.height(48.dp),
                verticalAlignment = Alignment.CenterVertically,
            ) {
                IconButton(onClick = onFinish) {
                    Icon(
                        painter = painterResource(R.drawable.close),
                        contentDescription = null,
                    )
                }
                Text(
                    text = stringResource(R.string.elements_selected, songSelection.size),
                    modifier = Modifier.weight(1f),
                )
                IconButton(onClick = onSelectAll) {
                    Icon(
                        painter = painterResource(
                            if (allSelected) R.drawable.deselect else R.drawable.select_all
                        ),
                        contentDescription = null,
                    )
                }
            }

            Row(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.SpaceEvenly,
            ) {
                QueueSelectionAction(
                    iconRes = R.drawable.playlist_play,
                    label = stringResource(R.string.play_next),
                    enabled = hasSelection,
                    onClick = {
                        playerConnection.playNext(songSelection.map { it.toMediaItem() })
                        onFinish()
                    },
                )
                QueueSelectionAction(
                    iconRes = R.drawable.playlist_add,
                    label = stringResource(R.string.add_to_playlist),
                    enabled = hasSelection,
                    onClick = { showChoosePlaylistDialog = true },
                )
                QueueSelectionAction(
                    iconRes = if (allLiked) R.drawable.favorite else R.drawable.favorite_border,
                    label = stringResource(if (allLiked) R.string.dislike_all else R.string.like_all),
                    enabled = hasSelection,
                    onClick = {
                        val songs = songSelection.toList()
                        database.query {
                            if (allLiked) {
                                songs.forEach { song ->
                                    update(song.toSongEntity().toggleLike())
                                }
                            } else {
                                songs.filter { !it.liked }.forEach { song ->
                                    update(song.toSongEntity().toggleLike())
                                }
                            }
                        }
                        onFinish()
                    },
                )
                if (canRemove) {
                    QueueSelectionAction(
                        iconRes = R.drawable.delete,
                        label = stringResource(R.string.remove_from_queue),
                        enabled = currentItems.isNotEmpty(),
                        onClick = {
                            val player = playerConnection.player
                            if (player.availableCommands.contains(Player.COMMAND_CHANGE_MEDIA_ITEMS)) {
                                // Remove from the end so earlier indices stay valid
                                currentItems
                                    .toList()
                                    .sortedByDescending { it.firstPeriodIndex }
                                    .forEach { player.removeMediaItem(it.firstPeriodIndex) }
                            }
                            onFinish()
                        },
                    )
                }
            }
        }
    }
}

@Composable
private fun RowScope.QueueSelectionAction(
    iconRes: Int,
    label: String,
    enabled: Boolean,
    onClick: () -> Unit,
) {
    Column(
        modifier = Modifier
            .weight(1f)
            .clickable(enabled = enabled, onClick = onClick)
            .padding(vertical = 8.dp),
        horizontalAlignment = Alignment.CenterHorizontally,
    ) {
        Icon(
            painter = painterResource(iconRes),
            contentDescription = null,
        )
        Text(
            text = label,
            style = MaterialTheme.typography.labelSmall,
            maxLines = 1,
            overflow = TextOverflow.Ellipsis,
        )
    }
}
