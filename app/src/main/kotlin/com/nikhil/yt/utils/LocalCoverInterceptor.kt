/*
 * Velune - makes every image in the app use the local cover when one exists.
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.utils

import android.content.Context
import coil3.ImageLoader
import coil3.annotation.ExperimentalCoilApi
import coil3.imageLoader
import coil3.decode.DataSource
import coil3.decode.ImageSource
import coil3.fetch.FetchResult
import coil3.fetch.Fetcher
import coil3.fetch.SourceFetchResult
import coil3.intercept.Interceptor
import coil3.network.HttpException
import coil3.request.ErrorResult
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
        if (ids.isEmpty()) return proceedAndHeal(chain, url)

        val (id, local) = withContext(Dispatchers.IO) {
            val mp3 = mp3Library()
            ids.firstNotNullOfOrNull { candidate ->
                CoverStore.localSource(appContext, candidate, mp3)?.let { candidate to it }
            }
        } ?: return proceedAndHeal(chain, url)

        val localRequest = request.newBuilder()
            .data(local)
            .memoryCacheKey("cover:$id")
            .build()
        val result = chain.withRequest(localRequest).proceed()
        return if (result is SuccessResult) result else proceedAndHeal(chain, url)
    }

    /**
     * Images saved by older versions may hold a saved server error (for example 404 or 429) that is
     * read back every time. If the load fails with an HTTP error and the image cache really holds an
     * entry for it, delete that entry and ask the server once more.
     */
    @OptIn(ExperimentalCoilApi::class)
    private suspend fun proceedAndHeal(chain: Interceptor.Chain, url: String): ImageResult {
        val result = chain.proceed()
        if (result !is ErrorResult || result.throwable !is HttpException) return result
        val cache = appContext.imageLoader.diskCache ?: return result
        val key = chain.request.diskCacheKey ?: url
        val removed = withContext(Dispatchers.IO) {
            val snapshot = cache.openSnapshot(key)
            if (snapshot != null) {
                snapshot.close()
                cache.remove(key)
            } else {
                false
            }
        }
        return if (removed) chain.proceed() else result
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
