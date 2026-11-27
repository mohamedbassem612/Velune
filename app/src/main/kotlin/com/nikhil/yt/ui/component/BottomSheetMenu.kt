/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */



package com.nikhil.yt.ui.component

import androidx.compose.foundation.gestures.Orientation
import androidx.compose.foundation.gestures.draggable
import androidx.compose.foundation.gestures.rememberDraggableState
import androidx.compose.foundation.layout.height
import androidx.compose.runtime.mutableFloatStateOf
import androidx.compose.runtime.remember
import androidx.compose.ui.geometry.Offset
import androidx.compose.ui.input.nestedscroll.NestedScrollConnection
import androidx.compose.ui.input.nestedscroll.NestedScrollSource
import androidx.compose.ui.input.nestedscroll.nestedScroll
import androidx.compose.ui.platform.LocalDensity
import androidx.compose.ui.platform.LocalWindowInfo
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ColumnScope
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.BottomSheetDefaults
import androidx.compose.material3.ExperimentalMaterial3Api
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.ModalBottomSheet
import androidx.compose.material3.rememberModalBottomSheetState
import androidx.compose.runtime.Composable
import androidx.compose.runtime.compositionLocalOf
import androidx.compose.runtime.Stable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalFocusManager
import androidx.compose.ui.unit.dp

val LocalMenuState = compositionLocalOf { MenuState() }

@Stable
class MenuState(
    isVisible: Boolean = false,
    content: @Composable ColumnScope.() -> Unit = {},
) {
    var isVisible by mutableStateOf(isVisible)
    var content by mutableStateOf(content)

    var expandOnSwipe by mutableStateOf(false)
        private set

    @OptIn(ExperimentalMaterial3Api::class)
    fun show(expandOnSwipe: Boolean = false, content: @Composable ColumnScope.() -> Unit) {
        this.expandOnSwipe = expandOnSwipe
        isVisible = true
        this.content = content
    }

    @OptIn(ExperimentalMaterial3Api::class)
    fun dismiss() {
        isVisible = false
    }
}

@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun BottomSheetMenu(
    modifier: Modifier = Modifier,
    state: MenuState,
    background: Color = MaterialTheme.colorScheme.surfaceContainer,
) {
    val focusManager = LocalFocusManager.current

    if (state.isVisible) {
        val sheetState = rememberModalBottomSheetState(skipPartiallyExpanded = true)
        val windowHeight = LocalWindowInfo.current.containerSize.height
        var fraction by remember(state.content) { mutableFloatStateOf(0.7f) }
        fun resize(delta: Float): Float {
            if (windowHeight <= 0) return 0f
            val previous = fraction
            fraction = (previous - delta / windowHeight).coerceIn(0.7f, 1f)
            return (previous - fraction) * windowHeight
        }
        val scrollConnection = remember(windowHeight, state.content) {
            object : NestedScrollConnection {
                override fun onPreScroll(available: Offset, source: NestedScrollSource): Offset =
                    if (available.y < 0f) Offset(0f, resize(available.y)) else Offset.Zero

                override fun onPostScroll(consumed: Offset, available: Offset, source: NestedScrollSource): Offset =
                    if (available.y > 0f) Offset(0f, resize(available.y)) else Offset.Zero
            }
        }
        val dragState = rememberDraggableState { resize(it) }
        ModalBottomSheet(
            sheetState = sheetState,
            onDismissRequest = {
                focusManager.clearFocus()
                state.isVisible = false
            },
            containerColor = background,
            shape = RoundedCornerShape(topStart = 30.dp, topEnd = 30.dp),
            scrimColor = MaterialTheme.colorScheme.scrim.copy(alpha = 0.4f),
            contentColor = MaterialTheme.colorScheme.onSurface,
            dragHandle = {
                Box(
                    modifier = Modifier
                        .then(if (state.expandOnSwipe) Modifier.draggable(dragState, Orientation.Vertical) else Modifier)
                        .padding(vertical = 12.dp)
                        .size(width = 40.dp, height = 4.dp)
                        .clip(RoundedCornerShape(2.dp))
                        .background(MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.4f))
                )
            },
            modifier = if (state.expandOnSwipe) {
                modifier.height(with(LocalDensity.current) { (windowHeight * fraction).toDp() })
            } else modifier
        ) {
            Column(
                modifier = Modifier
                    .then(if (state.expandOnSwipe) Modifier.nestedScroll(scrollConnection) else Modifier)
                    .fillMaxWidth()
                    .padding(horizontal = 8.dp)
            ) {
                state.content(this)
            }
        }
    }
}
