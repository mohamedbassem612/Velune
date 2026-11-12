package com.nikhil.yt.ui.tv

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
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
import com.nikhil.yt.playback.PlayerConnection

@Composable
internal fun TvPhoneConnection(
    connection: PlayerConnection?,
    actionFocus: FocusRequester,
    railFocus: FocusRequester,
    onBack: () -> Unit,
    modifier: Modifier = Modifier,
) {
    Column(modifier.focusProperties { left = railFocus }.verticalScroll(rememberScrollState()), verticalArrangement = Arrangement.spacedBy(16.dp)) {
        Text(stringResource(R.string.tv_phone_connection), style = MaterialTheme.typography.headlineMedium)

        if (connection == null) {
            Text(stringResource(R.string.tv_connection_wait))
        } else {
            com.nikhil.yt.ui.player.ConnectPanel(connection.service.handoverController, tv = true, entryFocus = actionFocus)
        }
        TvButton(stringResource(R.string.tv_phone_back), onBack, (if (connection == null) Modifier.focusRequester(actionFocus) else Modifier).focusProperties { left = railFocus })
    }
    LaunchedEffect(connection) { actionFocus.requestFocus() }
}
