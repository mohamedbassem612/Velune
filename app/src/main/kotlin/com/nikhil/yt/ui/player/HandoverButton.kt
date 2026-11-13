package com.nikhil.yt.ui.player

import androidx.compose.foundation.layout.*
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
    val selected by playerConnection.service.handoverController.selected.collectAsState()
    IconButton(onClick = { open = true }) {
        Icon(painterResource(R.drawable.ic_handover), stringResource(R.string.connect_devices),
            tint = if (selected != null) MaterialTheme.colorScheme.primary else LocalContentColor.current)
    }
    if (open) ConnectDialog(playerConnection, onClose = { open = false })
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun ConnectDialog(playerConnection: PlayerConnection, onClose: () -> Unit) {
    val song by playerConnection.mediaMetadata.collectAsState()
    ModalBottomSheet(onDismissRequest = onClose,
        sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)) {
        Column(Modifier.fillMaxWidth().heightIn(min = 240.dp, max = 520.dp)
            .verticalScroll(rememberScrollState()).padding(horizontal = 20.dp).padding(bottom = 32.dp)) {
            ConnectPanel(playerConnection.service.handoverController, currentSong = song?.title)
        }
    }
}
