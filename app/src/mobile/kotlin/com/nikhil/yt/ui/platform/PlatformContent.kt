package com.nikhil.yt.ui.platform

import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.runtime.Composable
import com.nikhil.yt.playback.PlayerConnection

/** The mobile build keeps its existing UI and activity lifecycle. */
fun ComponentActivity.setPlatformContent(
    playerConnection: () -> PlayerConnection?,
    mobileContent: @Composable () -> Unit,
) {
    setContent(content = mobileContent)
}
