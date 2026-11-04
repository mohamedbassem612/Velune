/*
 * Velune - makes every image in the app use the local cover when one exists.
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.utils

import android.content.Context
import coil3.ImageLoader
import coil3.decode.DataSource
import coil3.decode.ImageSource
import coil3.fetch.FetchResult
import coil3.fetch.Fetcher
import coil3.fetch.SourceFetchResult
import coil3.intercept.Interceptor
import coil3.request.ImageResult
import coil3.request.Options
import coil3.request.SuccessResult
import com.nikhil.yt.playback.Mp3Library
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext
import okio.Buffer
import okio.FileSystem

/**
 * Looks at every image request. If the url belongs to a song that has a local cover (inside its
 * mp3, or saved in the covers folder) the request is redirected to it. If that fails for any
 * reason, the normal url is loaded, so nothing gets worse than before.
 */
class LocalCoverInterceptor(
    context: Context,
    private val mp3Library: () -> Mp3Library?,
) : Interceptor {
    private val appContext = context.applicationContext

    override suspend fun intercept(chain: Interceptor.Chain): ImageResult {
        val request = chain.request
        val url = when (val data = request.data) {
            is String -> data
            is android.net.Uri -> data.toString()
            else -> null
        } ?: return chain.proceed()
        if (!url.startsWith("http")) return chain.proceed()

        val ids = CoverStore.idsForUrl(appContext, url)
        if (ids.isEmpty()) return chain.proceed()

        val (id, local) = withContext(Dispatchers.IO) {
            val mp3 = mp3Library()
            ids.firstNotNullOfOrNull { candidate ->
                CoverStore.localSource(appContext, candidate, mp3)?.let { candidate to it }
            }
        } ?: return chain.proceed()

        val localRequest = request.newBuilder()
            .data(local)
            .memoryCacheKey("cover:$id")
            .build()
        val result = chain.withRequest(localRequest).proceed()
        return if (result is SuccessResult) result else chain.proceed()
    }
}

class CoverBytesFetcher(private val data: CoverBytes) : Fetcher {
    override suspend fun fetch(): FetchResult =
        SourceFetchResult(
            source = ImageSource(Buffer().write(data.bytes), FileSystem.SYSTEM),
            mimeType = null,
            dataSource = DataSource.DISK,
        )

    class Factory : Fetcher.Factory<CoverBytes> {
        override fun create(data: CoverBytes, options: Options, imageLoader: ImageLoader): Fetcher =
            CoverBytesFetcher(data)
    }
}
