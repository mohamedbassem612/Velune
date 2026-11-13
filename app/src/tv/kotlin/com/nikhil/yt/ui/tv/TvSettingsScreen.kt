package com.nikhil.yt.ui.tv

import android.app.Activity
import android.content.res.Resources
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Modifier
import androidx.compose.ui.platform.LocalContext
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
import com.nikhil.yt.constants.AppLanguageKey
import com.nikhil.yt.constants.SYSTEM_DEFAULT
import com.nikhil.yt.constants.HideExplicitKey
import com.nikhil.yt.constants.StopMusicOnTaskClearKey
import com.nikhil.yt.utils.rememberPreference
import com.nikhil.yt.utils.chosenAppLocale
import com.nikhil.yt.utils.setAppLocale
import com.nikhil.yt.utils.dataStore
import androidx.datastore.preferences.core.edit
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

@Composable
internal fun TvSettingsScreen(contentFocus: FocusRequester, railFocus: FocusRequester, onPhoneConnection: () -> Unit, onRefresh: () -> Unit) {
    val (hideExplicit, setHideExplicit) = rememberPreference(HideExplicitKey, false)
    val (stopOnExit, setStopOnExit) = rememberPreference(StopMusicOnTaskClearKey, false)
    val (language, _) = rememberPreference(AppLanguageKey, SYSTEM_DEFAULT)
    var languageDialog by remember { mutableStateOf(false) }
    val context = LocalContext.current
    val scope = rememberCoroutineScope()
    Column(
        Modifier.fillMaxSize().verticalScroll(rememberScrollState()).padding(28.dp),
        verticalArrangement = Arrangement.spacedBy(14.dp),
    ) {
        Text(stringResource(R.string.settings), style = MaterialTheme.typography.headlineMedium)
        Spacer(Modifier.height(4.dp))
        TvSettingsTile(stringResource(R.string.tv_phone_connection), stringResource(R.string.tv_phone_connection_hint), Modifier.focusRequester(contentFocus).focusProperties { left = railFocus }, onClick = onPhoneConnection)
        TvSettingsTile(stringResource(R.string.tv_language),
            stringResource(when (language) {
                "en" -> R.string.tv_language_english
                "ar" -> R.string.tv_language_arabic
                else -> R.string.tv_language_system
            }), onClick = { languageDialog = true })
        TvSettingsTile(stringResource(R.string.tv_refresh_picks), stringResource(R.string.tv_refresh_hint), onClick = onRefresh)
        TvSettingsTile(stringResource(R.string.hide_explicit), stringResource(if (hideExplicit) R.string.tv_enabled else R.string.tv_disabled), onClick = { setHideExplicit(!hideExplicit); onRefresh() })
        TvSettingsTile(stringResource(R.string.tv_stop_on_exit), stringResource(if (stopOnExit) R.string.tv_enabled else R.string.tv_disabled), onClick = { setStopOnExit(!stopOnExit) })
        Text("Velune TV · ${BuildConfig.VERSION_NAME}", color = Color.White.copy(alpha = .35f), fontSize = 12.sp, modifier = Modifier.padding(top = 12.dp))
    }
    if (languageDialog) {
        AlertDialog(
            onDismissRequest = { languageDialog = false },
            title = { Text(stringResource(R.string.tv_language)) },
            text = {
                Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                    listOf(
                        SYSTEM_DEFAULT to R.string.tv_language_system,
                        "en" to R.string.tv_language_english,
                        "ar" to R.string.tv_language_arabic,
                    ).forEach { (choice, label) ->
                        TvSettingsTile(stringResource(label),
                            stringResource(if (language == choice) R.string.tv_language_selected else R.string.tv_language_choose),
                            Modifier.fillMaxWidth(), onClick = {
                                languageDialog = false
                                if (language != choice) scope.launch {
                                    withContext(Dispatchers.IO) {
                                        context.dataStore.edit { it[AppLanguageKey] = choice }
                                    }
                                    val system = Resources.getSystem().configuration.locales[0]
                                    setAppLocale(context, chosenAppLocale(choice, system))
                                    (context as? Activity)?.recreate()
                                }
                            })
                    }
                }
            },
            confirmButton = { TextButton(onClick = { languageDialog = false }) { Text(stringResource(R.string.cancel)) } },
        )
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
