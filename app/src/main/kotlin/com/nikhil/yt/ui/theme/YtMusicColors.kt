/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.ui.theme

import androidx.compose.material3.ColorScheme
import androidx.compose.material3.darkColorScheme
import androidx.compose.material3.lightColorScheme
import androidx.compose.ui.graphics.Color

/** Fixed colors that follow the look of the YouTube Music app. */
val YtMusicRed = Color(0xFFFF0000)

val YtMusicDarkColorScheme: ColorScheme = darkColorScheme(
    primary = Color(0xFFFFFFFF),
    onPrimary = Color(0xFF000000),
    primaryContainer = Color(0xFF2B2B2B),
    onPrimaryContainer = Color(0xFFFFFFFF),
    inversePrimary = Color(0xFF030303),
    secondary = Color(0xFFAAAAAA),
    onSecondary = Color(0xFF000000),
    secondaryContainer = Color(0xFF272727),
    onSecondaryContainer = Color(0xFFFFFFFF),
    tertiary = YtMusicRed,
    onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFF3A0A0A),
    onTertiaryContainer = Color(0xFFFFDAD6),
    background = Color(0xFF0F0F0F),
    onBackground = Color(0xFFFFFFFF),
    surface = Color(0xFF0F0F0F),
    onSurface = Color(0xFFFFFFFF),
    surfaceVariant = Color(0xFF272727),
    onSurfaceVariant = Color(0xFFAAAAAA),
    surfaceTint = Color(0xFFFFFFFF),
    inverseSurface = Color(0xFFE6E6E6),
    inverseOnSurface = Color(0xFF0F0F0F),
    error = Color(0xFFFF4E45),
    onError = Color(0xFF000000),
    errorContainer = Color(0xFF5C1410),
    onErrorContainer = Color(0xFFFFDAD6),
    outline = Color(0xFF717171),
    outlineVariant = Color(0xFF3A3A3A),
    scrim = Color(0xFF000000),
    surfaceBright = Color(0xFF2F2F2F),
    surfaceDim = Color(0xFF0F0F0F),
    surfaceContainerLowest = Color(0xFF000000),
    surfaceContainerLow = Color(0xFF121212),
    surfaceContainer = Color(0xFF1A1A1A),
    surfaceContainerHigh = Color(0xFF212121),
    surfaceContainerHighest = Color(0xFF2B2B2B),
)

val YtMusicLightColorScheme: ColorScheme = lightColorScheme(
    primary = Color(0xFF030303),
    onPrimary = Color(0xFFFFFFFF),
    primaryContainer = Color(0xFFEDEDED),
    onPrimaryContainer = Color(0xFF030303),
    inversePrimary = Color(0xFFFFFFFF),
    secondary = Color(0xFF606060),
    onSecondary = Color(0xFFFFFFFF),
    secondaryContainer = Color(0xFFF2F2F2),
    onSecondaryContainer = Color(0xFF030303),
    tertiary = YtMusicRed,
    onTertiary = Color(0xFFFFFFFF),
    tertiaryContainer = Color(0xFFFFDAD6),
    onTertiaryContainer = Color(0xFF410002),
    background = Color(0xFFFFFFFF),
    onBackground = Color(0xFF030303),
    surface = Color(0xFFFFFFFF),
    onSurface = Color(0xFF030303),
    surfaceVariant = Color(0xFFF2F2F2),
    onSurfaceVariant = Color(0xFF606060),
    surfaceTint = Color(0xFF030303),
    inverseSurface = Color(0xFF212121),
    inverseOnSurface = Color(0xFFFFFFFF),
    error = Color(0xFFBA1A1A),
    onError = Color(0xFFFFFFFF),
    errorContainer = Color(0xFFFFDAD6),
    onErrorContainer = Color(0xFF410002),
    outline = Color(0xFF909090),
    outlineVariant = Color(0xFFD9D9D9),
    scrim = Color(0xFF000000),
    surfaceBright = Color(0xFFFFFFFF),
    surfaceDim = Color(0xFFE8E8E8),
    surfaceContainerLowest = Color(0xFFFFFFFF),
    surfaceContainerLow = Color(0xFFF8F8F8),
    surfaceContainer = Color(0xFFF2F2F2),
    surfaceContainerHigh = Color(0xFFECECEC),
    surfaceContainerHighest = Color(0xFFE6E6E6),
)
