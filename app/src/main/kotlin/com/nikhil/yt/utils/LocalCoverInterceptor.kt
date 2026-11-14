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

    @OptIn(ExperimentalCoilApi::class)
    override suspend fun intercept(chain: Interceptor.Chain): ImageResult {
        val request = chain.request
        val song = request.data as? SongCover
        val url = song?.url ?: when (val data = request.data) {
            is String -> data
            is android.net.Uri -> data.toString()
            else -> null
        }
        if (song == null && (url == null || !url.startsWith("http"))) return chain.proceed()
        val ids = withContext(Dispatchers.IO) {
            if (song != null) {
                if (url != null) CoverStore.register(appContext, song.id, url)
                listOf(song.id)
            } else CoverStore.idsForUrl(appContext, url!!)
        }
        var local = withContext(Dispatchers.IO) {
            ids.firstNotNullOfOrNull { id -> CoverStore.localSource(appContext, id, mp3Library())?.let { id to it } }
        }
        if (local == null && ids.isNotEmpty() && com.nikhil.yt.connect.ConnectAssetAccess.fetcher != null) {
            com.nikhil.yt.connect.ConnectAssetAccess.fetch(ids.first())
            local = withContext(Dispatchers.IO) {
                ids.firstNotNullOfOrNull { id -> CoverStore.localSource(appContext, id, mp3Library())?.let { id to it } }
            }
        }
        val savedLocal = local
        if (savedLocal != null) {
            val result = chain.withRequest(request.newBuilder().data(savedLocal.second).memoryCacheKey("cover:${savedLocal.first}").build()).proceed()
            if (result is SuccessResult) return result
        }
        val id = ids.firstOrNull()
        val cacheKey = request.diskCacheKey ?: id?.let { "song-cover:$it" } ?: url?.let(ArtworkIdentity::canonical)
        val remoteRequest = request.newBuilder().data(url.orEmpty()).diskCacheKey(cacheKey).build()
        val remoteChain = chain.withRequest(remoteRequest)
        val cache = appContext.imageLoader.diskCache
        // Reuse older URL-keyed entries before creating a canonical entry or making a network call.
        val legacyKeys = withContext(Dispatchers.IO) {
            (listOfNotNull(cacheKey, url) + ids.flatMap { CoverStore.urlsForId(appContext, it) }).distinct()
        }
        for (key in legacyKeys) {
            val legacy = withContext(Dispatchers.IO) { cache?.openSnapshot(key) } ?: continue
            legacy.use { snapshot ->
                val file = java.io.File(snapshot.data.toString())
                val result = chain.withRequest(request.newBuilder().data(file).build()).proceed()
                if (result is SuccessResult) {
                    if (id != null) withContext(Dispatchers.IO) {
                        if (file.length() <= 2 * 1024 * 1024) CoverStore.saveBytes(appContext, id, file.readBytes())
                    }
                    return result
                }
            }
        }
        val result = proceedAndHeal(remoteChain, url.orEmpty())
        if (result is SuccessResult && id != null && cacheKey != null) withContext(Dispatchers.IO) {
            cache?.openSnapshot(cacheKey)?.use { snapshot ->
                val file = java.io.File(snapshot.data.toString())
                if (file.length() <= 2 * 1024 * 1024) CoverStore.saveBytes(appContext, id, file.readBytes())
            }
        }
        return result
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
