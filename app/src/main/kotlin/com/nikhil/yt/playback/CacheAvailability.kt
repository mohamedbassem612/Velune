package com.nikhil.yt.playback

import androidx.media3.datasource.cache.Cache
import androidx.media3.datasource.cache.ContentMetadata

/** A cache key or a nonzero byte count alone never proves that a song is complete. */
object CacheAvailability {
    internal fun requiredLength(position: Long, requestedLength: Long, contentLength: Long?): Long? {
        if (position < 0) return null
        if (requestedLength > 0) return requestedLength
        if (requestedLength == 0L) return null
        return contentLength?.takeIf { it > position }?.minus(position)
    }

    fun isCached(
        cache: Cache,
        key: String,
        position: Long = 0,
        requestedLength: Long = -1,
        fallbackContentLength: Long? = null,
    ): Boolean {
        val storedLength = cache.getContentMetadata(key).get(ContentMetadata.KEY_CONTENT_LENGTH, -1L)
        val contentLength = storedLength.takeIf { it > 0 }
            ?: fallbackContentLength?.takeIf { it > 0 }
        val length = requiredLength(position, requestedLength, contentLength) ?: return false
        return cache.isCached(key, position, length)
    }
}
