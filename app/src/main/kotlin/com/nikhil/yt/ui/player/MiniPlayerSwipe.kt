package com.nikhil.yt.ui.player

import androidx.compose.foundation.gestures.detectHorizontalDragGestures
import androidx.compose.animation.core.Animatable
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.spring
import androidx.compose.runtime.Composable
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.remember
import androidx.compose.runtime.rememberCoroutineScope
import androidx.compose.runtime.rememberUpdatedState
import androidx.compose.ui.Modifier
import androidx.compose.ui.input.pointer.pointerInput
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.unit.dp
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.launch
import kotlin.math.abs

@Stable
internal class MiniPlayerSwipeState {
    private val translation = Animatable(0f)
    private var animation: Job? = null
    private var startOffset = 0f
    val offset: Float get() = translation.value

    fun start() {
        animation?.cancel()
        startOffset = offset
    }

    fun drag(distance: Float, limit: Float, scope: CoroutineScope) {
        // Follow the finger with resistance instead of sliding the whole card off screen.
        val resisted = limit * distance / (limit + abs(distance))
        scope.launch(start = CoroutineStart.UNDISPATCHED) {
            translation.snapTo((startOffset + resisted).coerceIn(-limit, limit))
        }
    }

    fun settle(scope: CoroutineScope) {
        animation?.cancel()
        animation = scope.launch(start = CoroutineStart.UNDISPATCHED) {
            translation.animateTo(0f, spring(dampingRatio = Spring.DampingRatioNoBouncy, stiffness = Spring.StiffnessMediumLow))
        }
    }

    suspend fun reset() {
        animation?.cancel()
        translation.snapTo(0f)
    }
}

@Composable
internal fun rememberMiniPlayerSwipeState() = remember { MiniPlayerSwipeState() }

/** Share motion between the mini player's background, cover and title layers. */
@Composable
internal fun Modifier.miniPlayerSwipe(
    state: MiniPlayerSwipeState,
    enabled: Boolean,
    onPrevious: () -> Unit,
    onNext: () -> Unit,
    canPrevious: Boolean,
    canNext: Boolean,
    sensitivity: Float,
    active: Boolean = true,
): Modifier {
    val scope = rememberCoroutineScope()
    val previous by rememberUpdatedState(onPrevious)
    val next by rememberUpdatedState(onNext)
    val previousAvailable by rememberUpdatedState(canPrevious)
    val nextAvailable by rememberUpdatedState(canNext)
    val threshold = with(LocalDensity.current) { (104f - 80f * sensitivity.coerceIn(0f, 1f)).dp.toPx() }
    val limit = with(LocalDensity.current) { 40.dp.toPx() }
    // Even when skipping is disabled, consume drags so they cannot turn into expand taps.
    return if (!active) this else pointerInput(state, threshold, limit, enabled) {
        var distance = 0f
        detectHorizontalDragGestures(
            onDragStart = { distance = 0f; if (enabled) state.start() },
            onHorizontalDrag = { change, amount ->
                change.consume()
                if (enabled) {
                    distance += amount
                    state.drag(distance, limit, scope)
                }
            },
            onDragCancel = { distance = 0f; if (enabled) state.settle(scope) },
            onDragEnd = {
                if (enabled) {
                    state.settle(scope)
                    when {
                        distance <= -threshold && nextAvailable -> next()
                        distance >= threshold && previousAvailable -> previous()
                    }
                }
                distance = 0f
            },
        )
    }
}
