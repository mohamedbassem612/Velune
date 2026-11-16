package com.nikhil.yt.ui.component

import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.*
import androidx.compose.runtime.Composable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.Dp
import androidx.compose.ui.unit.dp
import com.nikhil.yt.R

@Composable
fun MusicMenuHeader(
    title: String,
    subtitle: String,
    thumbnailUrl: String?,
    songId: String? = null,
    trailingContent: @Composable () -> Unit = {},
) {
    Row(
        Modifier.fillMaxWidth().padding(horizontal = 16.dp, vertical = 12.dp),
        horizontalArrangement = Arrangement.spacedBy(16.dp),
        verticalAlignment = Alignment.CenterVertically,
    ) {
        MusicArtwork(thumbnailUrl, Modifier.size(56.dp), songId)
        Column(Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
            Text(title, style = MaterialTheme.typography.titleMedium,
                fontWeight = FontWeight.SemiBold, maxLines = 2, overflow = TextOverflow.Ellipsis)
            Text(subtitle.ifBlank { stringResource(R.string.music_unknown_artist) },
                style = MaterialTheme.typography.bodyMedium, color = MaterialTheme.colorScheme.onSurfaceVariant,
                maxLines = 2, overflow = TextOverflow.Ellipsis)
        }
        trailingContent()
    }
}

/** Transparent option rows keep every section on the same sheet surface. */
@Composable
fun MusicMenuItem(
    headlineContent: @Composable () -> Unit,
    modifier: Modifier = Modifier,
    overlineContent: @Composable (() -> Unit)? = null,
    supportingContent: @Composable (() -> Unit)? = null,
    leadingContent: @Composable (() -> Unit)? = null,
    trailingContent: @Composable (() -> Unit)? = null,
    colors: ListItemColors = ListItemDefaults.colors(
        containerColor = Color.Transparent,
        headlineColor = MaterialTheme.colorScheme.onSurface,
        supportingColor = MaterialTheme.colorScheme.onSurfaceVariant,
        leadingIconColor = MaterialTheme.colorScheme.onSurfaceVariant,
    ),
    tonalElevation: Dp = 0.dp,
    shadowElevation: Dp = 0.dp,
) {
    androidx.compose.material3.ListItem(
        headlineContent = headlineContent, overlineContent = overlineContent,
        supportingContent = supportingContent, leadingContent = leadingContent,
        trailingContent = trailingContent, colors = colors,
        tonalElevation = tonalElevation, shadowElevation = shadowElevation,
        modifier = Modifier.clip(RoundedCornerShape(12.dp)).then(modifier)
            .fillMaxWidth().heightIn(min = 56.dp),
    )
}
