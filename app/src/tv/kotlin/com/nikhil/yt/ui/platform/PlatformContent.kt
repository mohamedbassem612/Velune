package com.nikhil.yt.ui.platform

import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.Composable
import com.nikhil.yt.playback.PlayerConnection
import com.nikhil.yt.ui.tv.TvApp

/** Read the connection inside composition so service reconnections update the UI. */
fun ComponentActivity.setPlatformContent(
    playerConnection: () -> PlayerConnection?,
    mobileContent: @Composable () -> Unit,
) {
    setContent { TvApp(playerConnection()) }
}
