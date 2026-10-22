/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.ui.component

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.fillMaxHeight
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.nikhil.yt.ui.screens.Screens

/**
 * Flat bottom navigation bar in the YouTube Music style:
 * full width, no pill indicator, icon above a small label,
 * white when selected and grey when not.
 */
@Composable
fun YtMusicNavigationBar(
    items: List<Screens>,
    currentRoute: String,
    pureBlack: Boolean,
    barHeight: Dp,
    bottomInset: Dp,
    onTabSelected: (Screens) -> Unit,
    modifier: Modifier = Modifier,
) {
    val barColor = if (pureBlack) Color.Black else MaterialTheme.colorScheme.surfaceContainer
    val selectedColor = MaterialTheme.colorScheme.onSurface
    val unselectedColor = MaterialTheme.colorScheme.onSurfaceVariant

    Box(
        modifier = modifier
            .fillMaxWidth()
            .height(barHeight + bottomInset)
            .background(barColor)
    ) {
        Box(
            modifier = Modifier
                .align(Alignment.TopStart)
                .fillMaxWidth()
                .height(0.5.dp)
                .background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f))
        )

        Row(
            modifier = Modifier
                .align(Alignment.TopStart)
                .fillMaxWidth()
                .height(barHeight)
        ) {
            items.forEach { item ->
                val isSelected = item.route == currentRoute

                Column(
                    modifier = Modifier
                        .weight(1f)
                        .fillMaxHeight()
                        .clickable(
                            interactionSource = remember { MutableInteractionSource() },
                            indication = null,
                            onClick = { onTabSelected(item) }
                        ),
                    horizontalAlignment = Alignment.CenterHorizontally,
                    verticalArrangement = Arrangement.Center,
                ) {
                    Icon(
                        painter = painterResource(
                            id = if (isSelected) item.iconIdActive else item.iconIdInactive
                        ),
                        contentDescription = stringResource(id = item.titleId),
                        tint = if (isSelected) selectedColor else unselectedColor,
                        modifier = Modifier.size(24.dp)
                    )

                    Text(
                        text = stringResource(id = item.titleId),
                        fontSize = 11.sp,
                        maxLines = 1,
                        overflow = TextOverflow.Ellipsis,
                        color = if (isSelected) selectedColor else unselectedColor,
                    )
                }
            }
        }
    }
}
