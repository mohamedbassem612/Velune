package com.nikhil.yt.ui.player

import androidx.compose.foundation.layout.*
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.nikhil.yt.R
import com.nikhil.yt.handover.HandoverController
import com.nikhil.yt.localsync.LibrarySyncStatus

/** Shared management UI; pairing consent explicitly covers playback and mirrored library access. */
@Composable
fun ConnectPanel(controller: HandoverController, tv: Boolean = false, entryFocus: FocusRequester? = null) {
    val devices by controller.devices.collectAsState()
    val trusted by controller.trustedDevices.collectAsState()
    val pairing by controller.pairing.collectAsState()
    val pairingOpen by controller.pairingOpen.collectAsState()
    val coordinator by controller.coordinator.collectAsState()
    val connected by controller.connected.collectAsState()
    val selected by controller.selected.collectAsState()
    val state by controller.playback.collectAsState()
    val peers by controller.connectedDevices.collectAsState()
    val status by controller.libraryStatus.collectAsState()
    val error by controller.error.collectAsState()
    val transfer by controller.transferStatus.collectAsState()
    val approvalFocus = remember { FocusRequester() }
    LaunchedEffect(pairing?.deviceId) { if (pairing != null) approvalFocus.requestFocus() }
    var revokeId by remember { mutableStateOf<String?>(null) }

    LaunchedEffect(controller) { if (!tv) controller.startDiscovery() }
    Column(verticalArrangement = Arrangement.spacedBy(10.dp)) {
        Text(stringResource(R.string.connect_title), style = MaterialTheme.typography.titleLarge)
        Text(stringResource(R.string.connect_library_notice), style = MaterialTheme.typography.bodySmall)
        Text(stringResource(if (connected) R.string.connect_connected else R.string.connect_disconnected))
        Text(stringResource(if (state?.rendererId == controller.localId) R.string.connect_audio_here else R.string.connect_audio_remote))
        coordinator?.let { Text(stringResource(R.string.connect_peer, it.name)) }
        if (selected != null) Text(selected!!.name)
        Text(stringResource(when (status) {
            LibrarySyncStatus.Idle -> R.string.connect_sync_idle
            LibrarySyncStatus.Syncing -> R.string.connect_syncing
            LibrarySyncStatus.Synced -> R.string.connect_synced
            LibrarySyncStatus.Retrying -> R.string.connect_sync_retry
        }))
        if (tv) {
            Button(onClick = { if (pairingOpen) controller.closePairing() else controller.openPairing() },
                modifier = if (entryFocus != null) Modifier.focusRequester(entryFocus) else Modifier) {
                Text(stringResource(if (pairingOpen) R.string.connect_close_pairing else R.string.connect_open_pairing))
            }
            peers.forEach { Text(stringResource(R.string.connect_peer, it.name)) }
            Text(stringResource(R.string.connect_tv_independent), style = MaterialTheme.typography.bodySmall)
        } else {
            Text(stringResource(R.string.connect_available), style = MaterialTheme.typography.titleMedium)
            devices.forEach { device ->
                OutlinedButton(onClick = { controller.select(device) }, modifier = Modifier.fillMaxWidth()) {
                    Text(device.name + if (trusted.any { it.id == device.id }) " ✓" else "")
                }
            }
            if (devices.isEmpty()) Text(stringResource(R.string.connect_searching))
            if (connected) {
                Button(onClick = { controller.transferToTv() }, enabled = transfer == null) { Text(stringResource(R.string.connect_transfer_tv)) }
                OutlinedButton(onClick = { controller.choosePhone() }, enabled = transfer == null) { Text(stringResource(R.string.connect_transfer_phone)) }
            }
            OutlinedButton(onClick = { controller.disconnect() }) { Text(stringResource(R.string.connect_disconnect)) }
        }
        pairing?.let { request ->
            HorizontalDivider()
            Text(stringResource(R.string.connect_compare, request.name, request.code), style = MaterialTheme.typography.titleMedium)
            Text(stringResource(R.string.connect_consent))
            Button(onClick = { controller.approvePairing(true) }, modifier = Modifier.focusRequester(approvalFocus)) { Text(stringResource(R.string.connect_approve)) }
            OutlinedButton(onClick = { controller.approvePairing(false) }) { Text(stringResource(R.string.connect_reject)) }
        }
        Text(stringResource(R.string.connect_trusted), style = MaterialTheme.typography.titleMedium)
        if (trusted.isEmpty()) Text(stringResource(R.string.connect_no_trust))
        trusted.forEach { device ->
            Text("${device.name} · ${device.id.take(12)}")
            OutlinedButton(onClick = { revokeId = device.id }) { Text(stringResource(R.string.connect_revoke)) }
        }
        transfer?.let { Text(stringResource(R.string.connect_transferring)) }
        error?.let { Text(it, color = MaterialTheme.colorScheme.error) }
    }
    revokeId?.let { id ->
        AlertDialog(onDismissRequest = { revokeId = null },
            title = { Text(stringResource(R.string.connect_revoke)) },
            text = { Text(stringResource(R.string.connect_revoke_notice)) },
            confirmButton = { TextButton(onClick = { controller.revoke(id); revokeId = null }) { Text(stringResource(R.string.connect_revoke)) } },
            dismissButton = { TextButton(onClick = { revokeId = null }) { Text(stringResource(R.string.connect_cancel)) } })
    }
}
