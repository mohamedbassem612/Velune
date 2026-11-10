package com.nikhil.yt.ui.player

import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.material3.AlertDialog
import androidx.compose.material3.Icon
import androidx.compose.material3.IconButton
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.material3.TextButton
import androidx.compose.runtime.Composable
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import com.nikhil.yt.R
import com.nikhil.yt.playback.PlayerConnection

@Composable
fun HandoverButton(playerConnection: PlayerConnection) {
    val controller = playerConnection.service.handoverController
    val selected by controller.selected.collectAsState()
    val devices by controller.devices.collectAsState()
    val error by controller.error.collectAsState()
    var open by remember { mutableStateOf(false) }

    IconButton(onClick = { controller.startDiscovery(); open = true }) {
        Icon(
            painter = painterResource(R.drawable.ic_handover),
            contentDescription = stringResource(R.string.handover_choose_device),
            tint = if (selected != null) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface,
        )
    }

    if (open) {
        AlertDialog(
            onDismissRequest = { open = false },
            title = { Text(stringResource(R.string.handover_listen_on)) },
            text = {
                Column {
                    TextButton(onClick = { controller.choosePhone(); open = false }, modifier = Modifier.fillMaxWidth()) {
                        Text("${stringResource(R.string.handover_this_phone)}${if (selected == null) " ✓" else ""}")
                    }
                    devices.forEach { device ->
                        TextButton(onClick = { controller.select(device); open = false }, modifier = Modifier.fillMaxWidth()) {
                            Text("${device.name}${if (selected == device) " ✓" else ""}")
                        }
                    }
                    if (devices.isEmpty()) Text(stringResource(R.string.handover_searching))
                    if (error != null) Text(error.orEmpty(), color = MaterialTheme.colorScheme.error)
                }
            },
            confirmButton = { TextButton(onClick = { open = false }) { Text(stringResource(R.string.handover_close)) } },
        )
    }
}
