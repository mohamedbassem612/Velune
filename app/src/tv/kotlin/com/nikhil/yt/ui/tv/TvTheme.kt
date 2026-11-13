package com.nikhil.yt.ui.tv

import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.ui.graphics.Color
import com.nikhil.yt.ui.theme.VeluneTheme

internal val TvBackground = Color.Black
internal val TvAccent = Color.White
internal val TvSurface = Color(0xFF121212)
internal val TvFocusedSurface = Color(0xFF242424)

@Composable
internal fun TvAmoledTheme(content: @Composable () -> Unit) {
    VeluneTheme(darkTheme = true, pureBlack = true, themeColor = Color.White) {
        MaterialTheme(
            colorScheme = MaterialTheme.colorScheme.copy(
                primary = Color.White,
                onPrimary = Color.Black,
                primaryContainer = TvSurface,
                onPrimaryContainer = Color.White,
                secondary = Color.White,
                onSecondary = Color.Black,
                secondaryContainer = TvSurface,
                onSecondaryContainer = Color.White,
                tertiary = Color.White,
                onTertiary = Color.Black,
                tertiaryContainer = TvSurface,
                onTertiaryContainer = Color.White,
                background = Color.Black,
                onBackground = Color.White,
                surface = Color.Black,
                onSurface = Color.White,
                surfaceTint = Color.White,
                surfaceDim = Color.Black,
                surfaceBright = TvFocusedSurface,
                surfaceContainerLowest = Color.Black,
                surfaceContainerLow = Color.Black,
                surfaceContainer = TvSurface,
                surfaceContainerHigh = TvFocusedSurface,
                surfaceContainerHighest = Color(0xFF303030),
            ),
            content = content,
        )
    }
}
