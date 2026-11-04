package com.nikhil.yt.ui.component

import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.mutableIntStateOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.remember
import androidx.compose.runtime.setValue
import androidx.compose.ui.Modifier
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.platform.LocalContext
import coil3.compose.AsyncImage
import coil3.request.ImageRequest
import coil3.request.crossfade
import com.nikhil.yt.utils.ArtworkConnectivity
import kotlinx.coroutines.delay
import timber.log.Timber

/** Retry failed Home covers after reconnecting without reloading successful images. */
@Composable
fun RetryAsyncImage(
    url: String?,
    modifier: Modifier = Modifier,
    contentDescription: String? = null,
    contentScale: ContentScale = ContentScale.Fit,
) {
    val context = LocalContext.current
    val online by remember(context) { ArtworkConnectivity.online(context) }.collectAsState()
    var imageFailed by remember(url) { mutableStateOf(false) }
    var imageLoaded by remember(url) { mutableStateOf(false) }
    var retryGeneration by remember(url) { mutableIntStateOf(0) }

    LaunchedEffect(online, url) {
        if (!online || url.isNullOrBlank()) return@LaunchedEffect
        var tries = 0
        while (tries < 5 && !imageLoaded) {
            delay(400L + 1_500L * tries)
            if (imageLoaded) break
            if (imageFailed) {
                Timber.tag("ThumbRetry").d("retry %d for %s", tries + 1, url)
                imageFailed = false
                retryGeneration++
            }
            tries++
        }
    }

    val request = remember(context, url) {
        ImageRequest.Builder(context)
            .data(url)
            .crossfade(true)
            .build()
    }
    key(retryGeneration) {
        AsyncImage(
            model = request,
            contentDescription = contentDescription,
            contentScale = contentScale,
            modifier = modifier,
            onSuccess = {
                imageFailed = false
                imageLoaded = true
            },
            onError = { imageFailed = true },
        )
    }
}
