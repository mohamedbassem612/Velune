package com.nikhil.yt.ui.player

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalLayoutDirection
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.LayoutDirection
import androidx.compose.ui.unit.dp
import androidx.media3.common.Player
import com.nikhil.yt.R
import com.nikhil.yt.extensions.toggleRepeatMode
import com.nikhil.yt.playback.PlayerConnection

/** Shared by the default and Metro players, with a fixed visual order in either language. */
@Composable
internal fun PlayerTrackActions(connection: PlayerConnection, liked: Boolean, color: Color) {
    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
        Row(verticalAlignment = Alignment.CenterVertically) {
            CompositionLocalProvider(LocalContentColor provides color) { HandoverButton(connection) }
            IconButton(onClick = connection::toggleLike) {
                Icon(painterResource(if (liked) R.drawable.favorite else R.drawable.favorite_border),
                    stringResource(if (liked) R.string.action_remove_like else R.string.action_like), tint = color)
            }
        }
    }
}

@Composable
internal fun PlayerTransportControls(
    connection: PlayerConnection,
    playing: Boolean,
    loading: Boolean,
    canPrevious: Boolean,
    canNext: Boolean,
    color: Color = MaterialTheme.colorScheme.onSurface,
    backgroundColor: Color = MaterialTheme.colorScheme.surface,
) {
    val shuffle by connection.shuffleModeEnabled.collectAsState()
    val repeat by connection.repeatMode.collectAsState()
    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
        BoxWithConstraints(Modifier.fillMaxWidth()) {
            val compact = maxWidth < 312.dp
            Row(Modifier.fillMaxWidth().padding(vertical = 8.dp),
                horizontalArrangement = Arrangement.spacedBy(if (compact) 0.dp else 8.dp),
                verticalAlignment = Alignment.CenterVertically) {
                IconButton(onClick = { connection.player.shuffleModeEnabled = !shuffle }, modifier = Modifier.size(48.dp)) {
                    Icon(painterResource(R.drawable.shuffle),
                        stringResource(if (shuffle) R.string.music_shuffle_on else R.string.music_shuffle_off),
                        tint = color.copy(alpha = if (shuffle) 1f else 0.5f))
                }
                FilledIconButton(onClick = connection::seekToPrevious, enabled = canPrevious,
                    modifier = Modifier.weight(1f).height(56.dp), shape = RoundedCornerShape(16.dp),
                    colors = IconButtonDefaults.filledIconButtonColors(
                        containerColor = color.copy(alpha = 0.1f), contentColor = color,
                        disabledContainerColor = color.copy(alpha = 0.06f), disabledContentColor = color.copy(alpha = 0.4f))) {
                    Icon(painterResource(R.drawable.skip_previous), stringResource(R.string.music_previous), Modifier.size(28.dp))
                }
                FilledIconButton(onClick = connection::togglePlayPause,
                    modifier = Modifier.size(if (compact) 72.dp else 80.dp), shape = CircleShape,
                    colors = IconButtonDefaults.filledIconButtonColors(containerColor = color, contentColor = backgroundColor)) {
                    if (loading) CircularProgressIndicator(Modifier.size(28.dp), color = backgroundColor, strokeWidth = 2.dp)
                    else Icon(painterResource(if (playing) R.drawable.pause else R.drawable.play),
                        stringResource(if (playing) R.string.music_pause else R.string.play), Modifier.size(32.dp))
                }
                FilledIconButton(onClick = connection::seekToNext, enabled = canNext,
                    modifier = Modifier.weight(1f).height(56.dp), shape = RoundedCornerShape(16.dp),
                    colors = IconButtonDefaults.filledIconButtonColors(
                        containerColor = color.copy(alpha = 0.1f), contentColor = color,
                        disabledContainerColor = color.copy(alpha = 0.06f), disabledContentColor = color.copy(alpha = 0.4f))) {
                    Icon(painterResource(R.drawable.skip_next), stringResource(R.string.music_next), Modifier.size(28.dp))
                }
                IconButton(onClick = { connection.player.toggleRepeatMode() }, modifier = Modifier.size(48.dp)) {
                    Icon(painterResource(if (repeat == Player.REPEAT_MODE_ONE) R.drawable.repeat_one else R.drawable.repeat),
                        stringResource(when (repeat) {
                            Player.REPEAT_MODE_ONE -> R.string.repeat_mode_one
                            Player.REPEAT_MODE_ALL -> R.string.repeat_mode_all
                            else -> R.string.repeat_mode_off
                        }), tint = color.copy(alpha = if (repeat != Player.REPEAT_MODE_OFF) 1f else 0.5f))
                }
            }
        }
    }
}

@Composable
internal fun PlayerFooterActions(connection: PlayerConnection, color: Color, onQueue: () -> Unit, onLyrics: () -> Unit, onMenu: () -> Unit) {
    var showSleepTimer by remember { mutableStateOf(false) }
    val sleepTimer = connection.service.sleepTimer
    CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Ltr) {
        BoxWithConstraints(Modifier.fillMaxWidth().heightIn(min = 56.dp)) {
            // Anchor the labels to the supplied reference, independently of their icon graphics.
            // Both player layouts provide the same 24 dp horizontal inset.
            val queueLabelX = maxWidth * 0.227f
            val lyricsLabelX = maxWidth * 0.735f
            val iconAndPadding = 34.dp // 4 dp inset + 24 dp icon + 6 dp gap.
            val queueLeft = (queueLabelX - iconAndPadding).coerceAtLeast(0.dp)
            val lyricsLeft = (lyricsLabelX - iconAndPadding).coerceAtLeast(0.dp)
            PlayerFooterTextAction(stringResource(R.string.queue), R.drawable.queue_music, color, onQueue,
                Modifier.align(Alignment.CenterStart).offset(x = queueLeft)
                    .width((maxWidth / 2 - 24.dp - queueLeft).coerceAtLeast(48.dp)))
            IconButton(onClick = { if (sleepTimer.isActive) sleepTimer.clear() else showSleepTimer = true },
                modifier = Modifier.align(Alignment.Center).size(48.dp)) {
                Icon(painterResource(R.drawable.bedtime), stringResource(R.string.sleep_timer),
                    tint = color.copy(alpha = if (sleepTimer.isActive) 1f else 0.65f))
            }
            PlayerFooterTextAction(stringResource(R.string.lyrics), R.drawable.lyrics, color, onLyrics,
                Modifier.align(Alignment.CenterStart).offset(x = lyricsLeft)
                    .width((maxWidth - 48.dp - lyricsLeft).coerceAtLeast(48.dp)))
            IconButton(onClick = onMenu, modifier = Modifier.align(Alignment.CenterEnd).size(48.dp)) {
                Icon(painterResource(R.drawable.more_vert), stringResource(R.string.music_options), tint = color)
            }
        }
    }
    if (showSleepTimer) SleepTimerDialog(
        onDismiss = { showSleepTimer = false },
        onConfirm = { sleepTimer.start(it); showSleepTimer = false },
        onEndOfSong = { sleepTimer.start(-1); showSleepTimer = false },
    )
}

@Composable
private fun PlayerFooterTextAction(label: String, icon: Int, color: Color, onClick: () -> Unit, modifier: Modifier) {
    TextButton(onClick = onClick, modifier = modifier.heightIn(min = 48.dp),
        contentPadding = PaddingValues(horizontal = 4.dp, vertical = 8.dp)) {
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            Icon(painterResource(icon), null, tint = color, modifier = Modifier.size(24.dp))
            Spacer(Modifier.width(6.dp))
            Text(label, color = color, modifier = Modifier.weight(1f), maxLines = 1, overflow = TextOverflow.Ellipsis)
        }
    }
}
