package com.nikhil.yt.ui.tv

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import com.google.zxing.BarcodeFormat
import com.google.zxing.qrcode.QRCodeWriter
import com.nikhil.yt.R
import com.nikhil.yt.localsync.LibrarySyncStatus
import com.nikhil.yt.playback.PlayerConnection
import com.nikhil.yt.together.TogetherRoomSettings
import com.nikhil.yt.together.TogetherSessionState

@Composable
internal fun TvLocalSync(connection: PlayerConnection?, actionFocus: FocusRequester, modifier: Modifier = Modifier) {
    if (connection == null) {
        Text(stringResource(R.string.tv_connection_wait), modifier)
        return
    }
    val session by connection.service.togetherSessionState.collectAsState()
    val syncStatus by connection.service.localLibrarySync.status.collectAsState()
    val hosting = session as? TogetherSessionState.Hosting
    LaunchedEffect(hosting?.sessionId) {
        if (hosting != null) actionFocus.requestFocus()
    }
    Column(modifier.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Text(stringResource(R.string.tv_local_sync), style = MaterialTheme.typography.headlineMedium)
        Text(stringResource(R.string.tv_sync_intro))
        if (hosting != null) {
            val qr = remember(hosting.joinLink) {
                val matrix = QRCodeWriter().encode(hosting.joinLink, BarcodeFormat.QR_CODE, 512, 512)
                Bitmap.createBitmap(512, 512, Bitmap.Config.ARGB_8888).apply {
                    val pixels = IntArray(512 * 512) { i -> if (matrix[i % 512, i / 512]) android.graphics.Color.BLACK else android.graphics.Color.WHITE }
                    setPixels(pixels, 0, 512, 0, 0, 512, 512)
                }.asImageBitmap()
            }
            Row(horizontalArrangement = Arrangement.spacedBy(24.dp), verticalAlignment = Alignment.CenterVertically) {
                Image(qr, contentDescription = stringResource(R.string.tv_sync_scan), modifier = Modifier.size(220.dp))
                Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(12.dp)) {
                    Text(stringResource(R.string.tv_sync_scan))
                    Text(stringResource(R.string.tv_sync_connected, hosting.roomState?.participants?.count { !it.isHost && !it.isPending } ?: 0))
                    Text(stringResource(when(syncStatus) {
                        LibrarySyncStatus.Idle -> R.string.tv_sync_wait
                        LibrarySyncStatus.Syncing -> R.string.tv_sync_working
                        LibrarySyncStatus.Synced -> R.string.tv_sync_done
                        LibrarySyncStatus.Retrying -> R.string.tv_sync_retry
                    }))
                    TvButton(stringResource(R.string.tv_sync_stop), { connection.service.leaveTogether() }, Modifier.focusRequester(actionFocus))
                }
            }
        } else {
            if (session is TogetherSessionState.Error) {
                Text((session as TogetherSessionState.Error).message, color = MaterialTheme.colorScheme.error)
            }
            val idle = session is TogetherSessionState.Idle || session is TogetherSessionState.Error
            TvButton(
                stringResource(if (idle) R.string.tv_sync_start else R.string.tv_sync_stop),
                {
                    if (idle) connection.service.startTogetherHost(
                        port = 42117,
                        displayName = "Velune TV",
                        settings = TogetherRoomSettings(
                            allowGuestsToAddTracks = true,
                            allowGuestsToControlPlayback = true,
                            localLibrarySyncEnabled = true,
                        ),
                    ) else connection.service.leaveTogether()
                },
                modifier = Modifier.focusRequester(actionFocus),
            )
        }
    }
}
