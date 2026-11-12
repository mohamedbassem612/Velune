package com.nikhil.yt.ui.player

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.heightIn
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.nikhil.yt.R
import com.nikhil.yt.playback.PlayerConnection

@Composable
fun HandoverButton(playerConnection: PlayerConnection) {
    var open by remember { mutableStateOf(false) }
    IconButton(onClick = { open = true }) {
        Icon(painterResource(R.drawable.ic_handover), stringResource(R.string.connect_title))
    }
    if (open) ConnectDialog(playerConnection, onClose = { open = false })
}

@Composable
fun ConnectDialog(playerConnection: PlayerConnection, onClose: () -> Unit) {
    AlertDialog(onDismissRequest = onClose,
        text = {
            Column(Modifier.heightIn(max = 540.dp).verticalScroll(rememberScrollState())) {
                ConnectPanel(playerConnection.service.handoverController)
            }
        },
        confirmButton = { TextButton(onClick = onClose) { Text(stringResource(R.string.connect_close)) } })
}
