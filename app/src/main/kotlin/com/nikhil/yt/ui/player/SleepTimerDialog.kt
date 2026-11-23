package com.nikhil.yt.ui.player

import androidx.compose.animation.AnimatedContent
import androidx.compose.animation.SizeTransform
import androidx.compose.animation.core.Spring
import androidx.compose.animation.core.animateFloatAsState
import androidx.compose.animation.core.spring
import androidx.compose.animation.core.tween
import androidx.compose.animation.fadeIn
import androidx.compose.animation.fadeOut
import androidx.compose.animation.slideInVertically
import androidx.compose.animation.slideOutVertically
import androidx.compose.animation.togetherWith
import androidx.compose.foundation.background
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.interaction.collectIsDraggedAsState
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.drawBehind
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.graphicsLayer
import androidx.compose.ui.res.painterResource
import androidx.compose.ui.res.pluralStringResource
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.clearAndSetSemantics
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.semantics.stateDescription
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.nikhil.yt.R
import kotlin.math.roundToInt

/** Shared by the player and queue, with the same presentation as Connect. */
@OptIn(ExperimentalMaterial3Api::class)
@Composable
fun SleepTimerDialog(
    onDismiss: () -> Unit,
    onConfirm: (Int) -> Unit,
    onEndOfSong: () -> Unit,
    initialValue: Float = 30f,
) {
    var value by rememberSaveable { mutableFloatStateOf(initialValue.coerceIn(5f, 120f)) }
    val minutes = value.roundToInt()
    val interaction = remember { MutableInteractionSource() }
    val dragging by interaction.collectIsDraggedAsState()
    val halo by animateFloatAsState(if (dragging) 1f else 0f, tween(180), label = "TimerHalo")
    val scale by animateFloatAsState(if (dragging) 1.12f else 1f,
        spring(dampingRatio = Spring.DampingRatioNoBouncy), label = "TimerThumbScale")
    val tilt by animateFloatAsState(if (dragging) ((value - 62.5f) / 57.5f) * 18f else 0f,
        spring(dampingRatio = Spring.DampingRatioNoBouncy), label = "TimerMoonTilt")
    val primary = MaterialTheme.colorScheme.primary
    val title = stringResource(R.string.sleep_timer)
    val duration = pluralStringResource(R.plurals.minute, minutes, minutes)

    PlayerModalSheet(onDismiss) {
        Text(title, style = MaterialTheme.typography.headlineSmall)
        Spacer(Modifier.height(24.dp))
        Column(Modifier.fillMaxWidth(), horizontalAlignment = Alignment.CenterHorizontally) {
            AnimatedContent(targetState = minutes, contentAlignment = Alignment.Center,
                transitionSpec = {
                    val direction = if (targetState >= initialState) 1 else -1
                    ((slideInVertically(tween(180)) { direction * it / 3 } + fadeIn(tween(140))) togetherWith
                        (slideOutVertically(tween(180)) { -direction * it / 3 } + fadeOut(tween(100))))
                        .using(SizeTransform(clip = false))
                }, label = "TimerMinutes") { displayed ->
                Text(pluralStringResource(R.plurals.minute, displayed, displayed),
                    style = MaterialTheme.typography.headlineMedium, maxLines = 1, overflow = TextOverflow.Ellipsis,
                    modifier = if (displayed == minutes) Modifier else Modifier.clearAndSetSemantics { })
            }
            Spacer(Modifier.height(16.dp))
            Slider(value = value, onValueChange = { value = it }, valueRange = 5f..120f,
                steps = (120 - 5) / 5 - 1, interactionSource = interaction,
                modifier = Modifier.fillMaxWidth().semantics {
                    contentDescription = title
                    stateDescription = duration
                },
                track = { sliderState ->
                    val fraction = ((sliderState.value - 5f) / 115f).coerceIn(0f, 1f)
                    Box(Modifier.fillMaxWidth().height(12.dp).clip(CircleShape)
                        .background(MaterialTheme.colorScheme.onSurface.copy(alpha = 0.12f))) {
                        Box(Modifier.fillMaxWidth(fraction).fillMaxHeight().background(primary))
                        Box(Modifier.align(Alignment.CenterEnd).padding(end = 4.dp).size(4.dp)
                            .background(primary, CircleShape))
                    }
                },
                thumb = {
                    Box(Modifier.size(48.dp), contentAlignment = Alignment.Center) {
                        Box(Modifier.size(32.dp).graphicsLayer { scaleX = scale; scaleY = scale }
                            .drawBehind {
                                drawCircle(primary.copy(alpha = 0.16f * halo),
                                    radius = size.minDimension / 2 + 8.dp.toPx() * halo)
                            }.background(primary, CircleShape), contentAlignment = Alignment.Center) {
                            Icon(painterResource(R.drawable.bedtime), null,
                                tint = MaterialTheme.colorScheme.onPrimary,
                                modifier = Modifier.size(20.dp).graphicsLayer { rotationZ = tilt })
                        }
                    }
                })
            Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
                Text(pluralStringResource(R.plurals.minute, 5, 5), style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
                Text(pluralStringResource(R.plurals.minute, 120, 120), style = MaterialTheme.typography.labelSmall,
                    color = MaterialTheme.colorScheme.onSurfaceVariant)
            }
            Spacer(Modifier.height(16.dp))
            OutlinedButton(onClick = onEndOfSong, modifier = Modifier.fillMaxWidth()) {
                Text(stringResource(R.string.end_of_song))
            }
        }
        Spacer(Modifier.height(16.dp))
        Row(Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
            TextButton(onClick = { value = 30f }) { Text(stringResource(R.string.reset)) }
            Spacer(Modifier.weight(1f))
            TextButton(onClick = onDismiss) { Text(stringResource(android.R.string.cancel)) }
            TextButton(onClick = { onConfirm(minutes) }) { Text(stringResource(android.R.string.ok)) }
        }
    }
}
