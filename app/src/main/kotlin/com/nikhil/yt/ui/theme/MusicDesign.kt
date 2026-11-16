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
    primary = Color(0xFF36D167), onPrimary = Color(0xFF052D13),
    primaryContainer = Color(0xFF143A21), onPrimaryContainer = Color(0xFF98F5B3),
    inversePrimary = Color(0xFF157638),
    secondary = Color(0xFFB3B6BA), onSecondary = Color(0xFF17191C),
    secondaryContainer = Color(0xFF202326), onSecondaryContainer = Color(0xFFF3F4F5),
    tertiary = Color(0xFF36D167), onTertiary = Color(0xFF052D13),
    tertiaryContainer = Color(0xFF143A21), onTertiaryContainer = Color(0xFF98F5B3),
    background = Color(0xFF08090B), onBackground = Color(0xFFF3F4F5),
    surface = Color(0xFF08090B), onSurface = Color(0xFFF3F4F5),
    surfaceVariant = Color(0xFF202326), onSurfaceVariant = Color(0xFFA3A7AC),
    surfaceTint = Color.Transparent,
    inverseSurface = Color(0xFFF3F4F5), inverseOnSurface = Color(0xFF17191C),
    outline = Color(0xFF72777D), outlineVariant = Color(0xFF292C30),
    surfaceDim = Color.Black, surfaceBright = Color(0xFF292C30),
    surfaceContainerLowest = Color.Black, surfaceContainerLow = Color(0xFF101214),
    surfaceContainer = Color(0xFF17191C), surfaceContainerHigh = Color(0xFF202326),
    surfaceContainerHighest = Color(0xFF292C30),
    error = Color(0xFFFFB4AB), onError = Color(0xFF690005),
    errorContainer = Color(0xFF93000A), onErrorContainer = Color(0xFFFFDAD6),
    scrim = Color.Black,
)

// A deliberate day palette; the green is darker so small selected labels remain legible.
val MusicLightColorScheme = lightColorScheme(
    primary = Color(0xFF157638), onPrimary = Color.White,
    primaryContainer = Color(0xFFD8F6E1), onPrimaryContainer = Color(0xFF103E20),
    inversePrimary = Color(0xFF36D167),
    secondary = Color(0xFF60656D), onSecondary = Color.White,
    secondaryContainer = Color(0xFFE9EBED), onSecondaryContainer = Color(0xFF191C20),
    tertiary = Color(0xFF157638), onTertiary = Color.White,
    tertiaryContainer = Color(0xFFD8F6E1), onTertiaryContainer = Color(0xFF103E20),
    background = Color(0xFFF6F7F8), onBackground = Color(0xFF191C20),
    surface = Color(0xFFF6F7F8), onSurface = Color(0xFF191C20),
    surfaceVariant = Color(0xFFE9EBED), onSurfaceVariant = Color(0xFF60656D),
    surfaceTint = Color.Transparent,
    inverseSurface = Color(0xFF292C30), inverseOnSurface = Color(0xFFF3F4F5),
    outline = Color(0xFF747981), outlineVariant = Color(0xFFDDE0E3),
    surfaceDim = Color(0xFFE3E5E8), surfaceBright = Color.White,
    surfaceContainerLowest = Color.White, surfaceContainerLow = Color(0xFFF0F2F4),
    surfaceContainer = Color.White, surfaceContainerHigh = Color(0xFFECEEF0),
    surfaceContainerHighest = Color(0xFFE3E5E8),
    error = Color(0xFFBA1A1A), onError = Color.White,
    errorContainer = Color(0xFFFFDAD6), onErrorContainer = Color(0xFF410002),
    scrim = Color.Black,
)
