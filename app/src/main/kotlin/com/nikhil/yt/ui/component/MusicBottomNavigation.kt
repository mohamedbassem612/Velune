package com.nikhil.yt.ui.component

import androidx.compose.animation.animateColorAsState
import androidx.compose.animation.core.tween
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.selection.selectable
import androidx.compose.foundation.selection.selectableGroup
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.Role
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.nikhil.yt.ui.screens.Screens

@Composable
fun MusicBottomNavigation(
    items: List<Screens>,
    currentRoute: String,
    pureBlack: Boolean,
    barHeight: Dp,
    bottomInset: Dp,
    onTabSelected: (Screens) -> Unit,
    modifier: Modifier = Modifier,
) {
    Row(
        modifier = modifier.fillMaxWidth().height(barHeight + bottomInset)
            .background(MaterialTheme.colorScheme.surface)
            .padding(bottom = bottomInset).selectableGroup(),
    ) {
        items.forEach { item ->
            val selected = item.route == currentRoute ||
                (item == Screens.Search && currentRoute.startsWith("search/"))
            val tint by animateColorAsState(
                if (selected) MaterialTheme.colorScheme.primary else MaterialTheme.colorScheme.onSurfaceVariant,
                tween(180), label = "navigation selection",
            )
            Column(
                modifier = Modifier.weight(1f).fillMaxHeight()
                    .clip(RoundedCornerShape(24.dp))
                    .selectable(selected = selected, role = Role.Tab, onClick = { onTabSelected(item) }),
                horizontalAlignment = Alignment.CenterHorizontally,
                verticalArrangement = Arrangement.Center,
            ) {
                Icon(
                    painterResource(if (selected) item.iconIdActive else item.iconIdInactive),
                    contentDescription = null, tint = tint, modifier = Modifier.size(24.dp),
                )
                Spacer(Modifier.height(4.dp))
                Text(stringResource(item.titleId), color = tint, style = MaterialTheme.typography.labelMedium,
                    maxLines = 1)
            }
        }
    }
}
