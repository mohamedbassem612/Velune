package com.nikhil.yt.ui.theme

import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.material3.Shapes
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.unit.dp

/** Music surfaces are intentionally neutral: artwork is the visual focus. */
object MusicDesign {
    val ScreenPadding = 24.dp
    val TouchTarget = 48.dp
    val ArtworkRadius = 12.dp
    val PanelRadius = 30.dp
    val MiniPlayerRadius = 32.dp
}

val MusicShapes = Shapes(
    extraSmall = RoundedCornerShape(8.dp),
    small = RoundedCornerShape(12.dp),
    medium = RoundedCornerShape(20.dp),
    large = RoundedCornerShape(24.dp),
    extraLarge = RoundedCornerShape(MusicDesign.PanelRadius),
)

val MusicDarkColorScheme = darkColorScheme(
    primary = Color(0xFFF5F5F5), onPrimary = Color(0xFF000000),
    primaryContainer = Color(0xFF282828), onPrimaryContainer = Color(0xFFF5F5F5),
    inversePrimary = Color(0xFF191919),
    secondary = Color(0xFFB5B5B5), onSecondary = Color(0xFF171717),
    secondaryContainer = Color(0xFF222222), onSecondaryContainer = Color(0xFFF5F5F5),
    tertiary = Color(0xFFF5F5F5), onTertiary = Color(0xFF000000),
    tertiaryContainer = Color(0xFF282828), onTertiaryContainer = Color(0xFFF5F5F5),
    background = Color(0xFF080808), onBackground = Color(0xFFF5F5F5),
    surface = Color(0xFF080808), onSurface = Color(0xFFF5F5F5),
    surfaceVariant = Color(0xFF222222), onSurfaceVariant = Color(0xFFA6A6A6),
    surfaceTint = Color.Transparent,
    inverseSurface = Color(0xFFF5F5F5), inverseOnSurface = Color(0xFF171717),
    outline = Color(0xFF757575), outlineVariant = Color(0xFF2B2B2B),
    surfaceDim = Color.Black, surfaceBright = Color(0xFF2B2B2B),
    surfaceContainerLowest = Color.Black, surfaceContainerLow = Color(0xFF101010),
    surfaceContainer = Color(0xFF171717), surfaceContainerHigh = Color(0xFF222222),
    surfaceContainerHighest = Color(0xFF2B2B2B),
    error = Color(0xFFF5F5F5), onError = Color(0xFF000000),
    errorContainer = Color(0xFF303030), onErrorContainer = Color(0xFFF5F5F5),
    scrim = Color.Black,
)

// The day palette uses black actions and deliberately authored neutral surfaces.
val MusicLightColorScheme = lightColorScheme(
    primary = Color(0xFF191919), onPrimary = Color.White,
    primaryContainer = Color(0xFFE4E4E4), onPrimaryContainer = Color(0xFF191919),
    inversePrimary = Color(0xFFF5F5F5),
    secondary = Color(0xFF626262), onSecondary = Color.White,
    secondaryContainer = Color(0xFFEAEAEA), onSecondaryContainer = Color(0xFF191919),
    tertiary = Color(0xFF191919), onTertiary = Color.White,
    tertiaryContainer = Color(0xFFE4E4E4), onTertiaryContainer = Color(0xFF191919),
    background = Color(0xFFF6F6F6), onBackground = Color(0xFF191919),
    surface = Color(0xFFF6F6F6), onSurface = Color(0xFF191919),
    surfaceVariant = Color(0xFFEAEAEA), onSurfaceVariant = Color(0xFF626262),
    surfaceTint = Color.Transparent,
    inverseSurface = Color(0xFF2B2B2B), inverseOnSurface = Color(0xFFF5F5F5),
    outline = Color(0xFF777777), outlineVariant = Color(0xFFDEDEDE),
    surfaceDim = Color(0xFFE5E5E5), surfaceBright = Color.White,
    surfaceContainerLowest = Color.White, surfaceContainerLow = Color(0xFFF1F1F1),
    surfaceContainer = Color.White, surfaceContainerHigh = Color(0xFFEEEEEE),
    surfaceContainerHighest = Color(0xFFE5E5E5),
    error = Color(0xFF191919), onError = Color.White,
    errorContainer = Color(0xFFF5F5F5), onErrorContainer = Color(0xFF191919),
    scrim = Color.Black,
)
