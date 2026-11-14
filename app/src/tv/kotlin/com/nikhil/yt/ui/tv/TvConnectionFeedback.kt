package com.nikhil.yt.ui.tv

import androidx.compose.animation.AnimatedVisibility
import androidx.compose.animation.core.*
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInHorizontally
import androidx.compose.animation.slideOutHorizontally
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.Canvas
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.AbsoluteAlignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.geometry.CornerRadius
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.geometry.Size
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.graphics.drawscope.Stroke
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.nikhil.yt.R
import com.nikhil.yt.connect.ConnectTrustedDevice
import com.nikhil.yt.playback.PlayerConnection
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.collectLatest

/** Presentation only: networking and library ownership remain in the service. */
@Composable
internal fun TvConnectionFeedback(
    connection: PlayerConnection?,
    content: @Composable (Modifier) -> Unit,
) {
    val controller = connection?.service?.handoverController
    val notices = remember(controller) { Channel<ConnectTrustedDevice>(Channel.UNLIMITED) }
    var notice by remember(controller) { mutableStateOf<ConnectTrustedDevice?>(null) }
    var noticeVisible by remember(controller) { mutableStateOf(false) }
    var importing by remember(controller) { mutableStateOf(false) }
    var importingName by remember(controller) { mutableStateOf("") }

    LaunchedEffect(controller) {
        var previous = emptySet<String>()
        controller?.connectedDevices?.collect { devices ->
            devices.filterNot { it.id in previous }.forEach { notices.send(it) }
            previous = devices.mapTo(mutableSetOf()) { it.id }
        }
    }
    LaunchedEffect(notices) {
        for (device in notices) {
            if (controller?.connectedDevices?.value?.none { it.id == device.id } != false) continue
            notice = device
            noticeVisible = true
            delay(4500)
            noticeVisible = false
            delay(400)
        }
    }
    LaunchedEffect(controller) {
        controller?.firstLibrarySync?.collectLatest { devices ->
            if (devices.isNotEmpty()) {
                importingName = devices.values.first()
                importing = true
            } else {
                // Let Room's updated shelves settle before revealing the new library.
                delay(700)
                importing = false
            }
        }
    }
    val contentAlpha by animateFloatAsState(if (importing) .18f else 1f, tween(650), label = "library reveal")
    val contentScale by animateFloatAsState(if (importing) .985f else 1f, tween(700), label = "library depth")

    Box(Modifier.fillMaxSize()) {
        content(Modifier.graphicsLayer { alpha = contentAlpha; scaleX = contentScale; scaleY = contentScale })
        AnimatedVisibility(importing, Modifier.align(Alignment.Center), enter = fadeIn(tween(450)), exit = fadeOut(tween(350))) {
            Column(horizontalAlignment = Alignment.CenterHorizontally, verticalArrangement = Arrangement.spacedBy(14.dp)) {
                CircularProgressIndicator(Modifier.size(32.dp), color = Color.White, strokeWidth = 2.dp)
                Text(stringResource(R.string.tv_importing_library), style = MaterialTheme.typography.titleLarge)
                Text(stringResource(R.string.tv_importing_library_detail, importingName), color = Color.White.copy(alpha = .65f), style = MaterialTheme.typography.bodyMedium)
            }
        }
        // Absolute alignment keeps the notice in the reference position in Arabic too.
        AnimatedVisibility(
            noticeVisible, Modifier.align(AbsoluteAlignment.TopRight).padding(24.dp).widthIn(max = 360.dp),
            enter = fadeIn(tween(350)) + slideInHorizontally(tween(450, easing = FastOutSlowInEasing)) { it / 2 },
            exit = fadeOut(tween(250)) + slideOutHorizontally(tween(350)) { it / 2 },
        ) {
            Surface(shape = RoundedCornerShape(16.dp), color = Color(0xF21B1B1B), border = BorderStroke(1.dp, Color.White.copy(alpha = .16f)), shadowElevation = 12.dp) {
                Row(Modifier.padding(horizontal = 18.dp, vertical = 14.dp), verticalAlignment = Alignment.CenterVertically, horizontalArrangement = Arrangement.spacedBy(14.dp)) {
                    ConnectedDeviceGraphic(notice?.name.orEmpty())
                    Text(stringResource(R.string.tv_connected_to, notice?.name.orEmpty()), style = MaterialTheme.typography.titleMedium, maxLines = 2, overflow = TextOverflow.Ellipsis)
                }
            }
        }
    }
}

@Composable
private fun ConnectedDeviceGraphic(name: String) {
    val wide = listOf("tv", "television", "desktop", "laptop", "macbook", "pc").any { name.contains(it, ignoreCase = true) }
    Canvas(Modifier.size(38.dp)) {
        val w = size.width * if (wide) .88f else .48f
        val h = size.height * if (wide) .55f else .82f
        val x = (size.width - w) / 2
        val y = (size.height - h) / 2
        drawRoundRect(Color.White.copy(alpha = .08f), Offset(x, y), Size(w, h), CornerRadius(4.dp.toPx()))
        drawRoundRect(Color.White, Offset(x, y), Size(w, h), CornerRadius(4.dp.toPx()), style = Stroke(1.6.dp.toPx()))
        if (wide) {
            drawLine(Color.White, Offset(size.width / 2, y + h), Offset(size.width / 2, y + h + 4.dp.toPx()), 1.5.dp.toPx())
            drawLine(Color.White, Offset(size.width * .35f, y + h + 4.dp.toPx()), Offset(size.width * .65f, y + h + 4.dp.toPx()), 1.5.dp.toPx())
        } else {
            drawLine(Color.White.copy(alpha = .7f), Offset(size.width * .45f, y + h - 4.dp.toPx()), Offset(size.width * .55f, y + h - 4.dp.toPx()), 1.5.dp.toPx())
        }
        drawCircle(Color(0xFF91E2B2), 4.dp.toPx(), Offset(size.width * .84f, size.height * .77f))
    }
}
