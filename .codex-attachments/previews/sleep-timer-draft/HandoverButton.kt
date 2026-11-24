package com.nikhil.yt.ui.player

import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import com.nikhil.yt.R
import com.nikhil.yt.playback.PlayerConnection

@Composable
fun HandoverButton(playerConnection: PlayerConnection) {
    var open by remember { mutableStateOf(false) }
    val selected by playerConnection.service.handoverController.selected.collectAsState()
    IconButton(onClick = { open = true }) {
        Icon(painterResource(R.drawable.ic_handover), stringResource(R.string.connect_devices),
            tint = if (selected != null) MaterialTheme.colorScheme.primary else LocalContentColor.current)
    }
    if (open) ConnectDialog(playerConnection, onClose = { open = false })
}

@Composable
fun ConnectDialog(playerConnection: PlayerConnection, onClose: () -> Unit) {
    val song by playerConnection.mediaMetadata.collectAsState()
    PlayerModalSheet(onClose) {
        ConnectPanel(playerConnection.service.handoverController, currentSong = song?.title)
    }
}
