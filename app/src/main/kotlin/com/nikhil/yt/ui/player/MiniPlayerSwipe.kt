package com.nikhil.yt.ui.player

import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp

/** Consume a horizontal drag so it cannot become a tap that expands the player. */
@Composable
internal fun Modifier.miniPlayerSwipe(enabled: Boolean, onPrevious: () -> Unit, onNext: () -> Unit): Modifier {
    val previous by rememberUpdatedState(onPrevious)
    val next by rememberUpdatedState(onNext)
    val threshold = with(LocalDensity.current) { 48.dp.toPx() }
    return if (!enabled) this else pointerInput(threshold) {
        var distance = 0f
        detectHorizontalDragGestures(
            onDragStart = { distance = 0f },
            onHorizontalDrag = { change, amount ->
                change.consume()
                distance += amount
            },
            onDragCancel = { distance = 0f },
            onDragEnd = {
                when {
                    distance <= -threshold -> next()
                    distance >= threshold -> previous()
                }
                distance = 0f
            },
        )
    }
}
