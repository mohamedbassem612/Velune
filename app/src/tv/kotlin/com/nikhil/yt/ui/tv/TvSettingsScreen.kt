package com.nikhil.yt.ui.tv

import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.focus.FocusRequester
import androidx.compose.ui.focus.focusProperties
import androidx.compose.ui.focus.focusRequester
import androidx.compose.ui.focus.onFocusChanged
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nikhil.yt.BuildConfig
import com.nikhil.yt.R
import com.nikhil.yt.constants.HideExplicitKey
import com.nikhil.yt.constants.StopMusicOnTaskClearKey
import com.nikhil.yt.utils.rememberPreference

@Composable
internal fun TvSettingsScreen(contentFocus: FocusRequester, railFocus: FocusRequester, onPhoneConnection: () -> Unit, onRefresh: () -> Unit) {
    val (hideExplicit, setHideExplicit) = rememberPreference(HideExplicitKey, false)
    val (stopOnExit, setStopOnExit) = rememberPreference(StopMusicOnTaskClearKey, false)
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(28.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Text(stringResource(R.string.settings), style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(4.dp))
        TvSettingsTile(stringResource(R.string.tv_phone_connection), stringResource(R.string.tv_phone_connection_hint), Modifier.focusRequester(contentFocus).focusProperties { left = railFocus }, onClick = onPhoneConnection)
        TvSettingsTile(stringResource(R.string.tv_refresh_picks), stringResource(R.string.tv_refresh_hint), onClick = onRefresh)
        TvSettingsTile(stringResource(R.string.hide_explicit), stringResource(if (hideExplicit) R.string.tv_enabled else R.string.tv_disabled), onClick = { setHideExplicit(!hideExplicit); onRefresh() })
        TvSettingsTile(stringResource(R.string.tv_stop_on_exit), stringResource(if (stopOnExit) R.string.tv_enabled else R.string.tv_disabled), onClick = { setStopOnExit(!stopOnExit) })
        Text("Velune TV · ${BuildConfig.VERSION_NAME}", color = Color.White.copy(alpha = .35f), fontSize = 12.sp, modifier = Modifier.padding(top = 12.dp))
    }
}

@Composable
private fun TvSettingsTile(title: String, description: String, modifier: Modifier = Modifier, onClick: () -> Unit) {
    var focused by remember { mutableStateOf(false) }
    Surface(
        onClick = onClick,
        modifier = modifier.fillMaxWidth(.86f).onFocusChanged { focused = it.isFocused },
        color = if (focused) Color(0xFF272C36) else Color(0xFF151A22),
        border = BorderStroke(2.dp, if (focused) Color.White else Color.Transparent),
        shape = RoundedCornerShape(12.dp),
    ) {
        Column(Modifier.padding(horizontal = 20.dp, vertical = 14.dp), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, color = Color.White, fontSize = 16.sp)
            Text(description, color = Color.White.copy(alpha = .55f), fontSize = 12.sp)
        }
    }
}
