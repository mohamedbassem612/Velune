/*
 * Velune - by Nikhil
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.utils

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.net.Uri
import androidx.media3.common.util.BitmapLoader
import coil3.imageLoader
import coil3.request.ErrorResult
import coil3.request.ImageRequest
import coil3.request.SuccessResult
import coil3.request.allowHardware
import coil3.toBitmap
import com.google.common.util.concurrent.ListenableFuture
import java.io.IOException
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.delay
import kotlinx.coroutines.guava.future
import kotlin.math.roundToInt

class CoilBitmapLoader(
    private val context: Context,
    private val scope: CoroutineScope,
) : BitmapLoader {
    private val maxIconSizePx = (context.resources.displayMetrics.density * 128f)
        .roundToInt().coerceIn(128, 512)
    // Six prepared software bitmaps retain recent/adjacent covers without keeping the whole queue.
    private val artwork = ArtworkLoadCache<Uri, Bitmap>(maxEntries = 6) { uri ->
        scope.future(Dispatchers.IO) { fetchBitmap(uri) }
    }

    override fun supportsMimeType(mimeType: String): Boolean = mimeType.startsWith("image/")

    override fun decodeBitmap(data: ByteArray): ListenableFuture<Bitmap> =
        scope.future(Dispatchers.IO) {
            require(data.isNotEmpty()) { "Empty image data" }
            BitmapFactory.decodeByteArray(data, 0, data.size)
                ?: throw IOException("Could not decode notification artwork")
        }

    override fun loadBitmap(uri: Uri): ListenableFuture<Bitmap> = artwork.get(uri)

    fun prefetch(uris: List<Uri>) {
        uris.distinct().forEach { artwork.prefetch(it) }
    }

    private suspend fun fetchBitmap(uri: Uri): Bitmap {
        var lastFailure: Throwable? = null
        for (attempt in 1..3) {
            try {
                val request = ImageRequest.Builder(context)
                    .data(uri)
                    .allowHardware(false)
                    .size(maxIconSizePx, maxIconSizePx)
                    .build()
                when (val result = context.imageLoader.execute(request)) {
                    is SuccessResult -> {
                        val bitmap = result.image.toBitmap()
                        check(bitmap.width > 0 && bitmap.height > 0) { "Empty artwork dimensions" }
                        val scaled = if (bitmap.width <= maxIconSizePx && bitmap.height <= maxIconSizePx) {
                            bitmap
                        } else {
                            val scale = minOf(maxIconSizePx.toFloat() / bitmap.width,
                                maxIconSizePx.toFloat() / bitmap.height)
                            Bitmap.createScaledBitmap(bitmap,
                                (bitmap.width * scale).roundToInt().coerceAtLeast(1),
                                (bitmap.height * scale).roundToInt().coerceAtLeast(1), true)
                        }
                        return requireNotNull(scaled.copy(Bitmap.Config.ARGB_8888, false))
                    }
                    is ErrorResult -> lastFailure = result.throwable
                }
            } catch (cancel: CancellationException) {
                throw cancel
            } catch (error: Exception) {
                lastFailure = error
            }
            if (attempt < 3) delay(250L * attempt)
        }
        // A transparent success bitmap changes the system palette and hides a genuine failure.
        // Report failure so Android can use its normal missing-artwork presentation instead.
        throw IOException("Could not load notification artwork", lastFailure).also { reportException(it) }
    }
}
