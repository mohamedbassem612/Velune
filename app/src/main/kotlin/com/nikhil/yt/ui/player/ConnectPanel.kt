package com.nikhil.yt.ui.player

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.nikhil.yt.R
import com.nikhil.yt.handover.HandoverController

/** Choose the audio output directly; connection mechanics stay out of the picker. */
@Composable
fun ConnectPanel(controller: HandoverController, tv: Boolean = false, currentSong: String? = null) {
    val devices by controller.devices.collectAsState()
    val coordinator by controller.coordinator.collectAsState()
    val connected by controller.connected.collectAsState()
    val state by controller.playback.collectAsState()
    val peers by controller.connectedDevices.collectAsState()
    val error by controller.error.collectAsState()
    val pending by controller.pendingDeviceId.collectAsState()

    LaunchedEffect(controller) { if (!tv) controller.startDiscovery() }
    Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
        Text(stringResource(R.string.connect_devices), style = MaterialTheme.typography.headlineSmall)
        if (tv) {
            Text(stringResource(R.string.connect_lan_ready))
            peers.forEach { Text(stringResource(R.string.connect_peer, it.name)) }
        } else {
            val phoneActive = state == null || state?.rendererId == controller.localId
            DeviceRow(stringResource(R.string.connect_this_phone), R.drawable.ic_connect_phone,
                active = phoneActive, pending = pending == controller.localId, enabled = pending == null,
                song = if (phoneActive) currentSong else null,
                onClick = { if (!phoneActive) controller.choosePhone() })
            val outputs = (devices + listOfNotNull(coordinator)).distinctBy { it.id }
            outputs.forEach { device ->
                val active = connected && state?.rendererId == device.id
                DeviceRow(device.name, R.drawable.ic_handover,
                    active = active, pending = pending == device.id, enabled = pending == null,
                    song = if (active) state?.song?.title else null,
                    offline = !connected && coordinator?.id == device.id,
                    onClick = { if (!active) controller.select(device) })
            }
            if (outputs.isEmpty()) Text(stringResource(R.string.connect_searching),
                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant)
        }
        error?.let { Text(it, style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.error) }
    }
}

@Composable
private fun DeviceRow(name: String, icon: Int, active: Boolean, pending: Boolean,
                      enabled: Boolean, song: String?, offline: Boolean = false, onClick: () -> Unit) {
    val color = if (active) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurface
    Surface(shape = RoundedCornerShape(16.dp),
        color = if (active) MaterialTheme.colorScheme.surfaceContainerHigh else MaterialTheme.colorScheme.surface) {
        Row(Modifier.fillMaxWidth().selectable(selected = active, enabled = enabled,
            role = Role.RadioButton, onClick = onClick).padding(horizontal = 16.dp, vertical = 20.dp),
            verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(16.dp)) {
            Icon(painterResource(icon), contentDescription = null, modifier = Modifier.size(28.dp), tint = color)
            Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                Text(name, style = MaterialTheme.typography.titleMedium, color = color,
                    maxLines = 1, overflow = TextOverflow.Ellipsis)
                if (pending) Text(stringResource(R.string.connect_transferring), style = MaterialTheme.typography.bodySmall)
                else if (offline) Text(stringResource(R.string.connect_device_offline), style = MaterialTheme.typography.bodySmall)
                else if (active) Text(song ?: stringResource(R.string.connect_current_device),
                    style = MaterialTheme.typography.bodySmall, color = color, maxLines = 1, overflow = TextOverflow.Ellipsis)
            }
            if (pending) CircularProgressIndicator(Modifier.size(22.dp), strokeWidth = 2.dp)
            else if (active) Icon(painterResource(R.drawable.check), stringResource(R.string.connect_current_device), tint = color)
        }
    }
}
