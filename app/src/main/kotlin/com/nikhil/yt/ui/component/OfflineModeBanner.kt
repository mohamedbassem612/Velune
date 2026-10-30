package com.nikhil.yt.ui.component

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.delay

private const val BANNER_HIDDEN = 0
private const val BANNER_OFFLINE = 1
private const val BANNER_BACK_ONLINE = 2

/**
 * Small pill above the mini player. Shows "Offline mode" while there is no internet.
 * When the internet returns it says "Back online" in green and disappears after 3 seconds.
 */
@Composable
fun OfflineModeBanner(
    isOnline: Boolean,
    visible: Boolean,
    modifier: Modifier = Modifier,
) {
    var mode by remember { mutableIntStateOf(if (isOnline) BANNER_HIDDEN else BANNER_OFFLINE) }

    LaunchedEffect(isOnline) {
        if (!isOnline) {
            mode = BANNER_OFFLINE
        } else if (mode == BANNER_OFFLINE) {
            mode = BANNER_BACK_ONLINE
            delay(3000)
            mode = BANNER_HIDDEN
        }
    }

    val container by animateColorAsState(
        targetValue = if (mode == BANNER_BACK_ONLINE) Color(0xFF2E7D32) else MaterialTheme.colorScheme.surfaceContainerHighest,
        label = "offlineBannerColor",
    )
    val content by animateColorAsState(
        targetValue = if (mode == BANNER_BACK_ONLINE) Color.White else MaterialTheme.colorScheme.onSurface,
        label = "offlineBannerContent",
    )

    AnimatedVisibility(
        visible = visible && mode != BANNER_HIDDEN,
        enter = fadeIn() + slideInVertically { it / 2 },
        exit = fadeOut() + slideOutVertically { it / 2 },
        modifier = modifier,
    ) {
        Surface(
            shape = RoundedCornerShape(50),
            color = container,
            contentColor = content,
            tonalElevation = 2.dp,
        ) {
            Text(
                text = if (mode == BANNER_BACK_ONLINE) "Back online" else "Offline mode",
                style = MaterialTheme.typography.labelMedium,
                modifier = Modifier.padding(horizontal = 14.dp, vertical = 5.dp),
            )
        }
    }
}
