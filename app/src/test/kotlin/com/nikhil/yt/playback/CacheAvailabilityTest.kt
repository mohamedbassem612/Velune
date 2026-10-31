package com.nikhil.yt.playback

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class CacheAvailabilityTest {
    @Test fun unknownLengthDoesNotProveCompleteness() {
        assertNull(CacheAvailability.requiredLength(0, -1, null))
        assertNull(CacheAvailability.requiredLength(0, -1, -1))
        assertNull(CacheAvailability.requiredLength(0, -1, 0))
    }

    @Test fun seekChecksOnlyRemainingBytes() {
        assertEquals(60L, CacheAvailability.requiredLength(40, -1, 100))
        assertEquals(10L, CacheAvailability.requiredLength(40, 10, 100))
    }

    @Test fun emptyAndInvalidRangesAreNotPlayableSongs() {
        assertNull(CacheAvailability.requiredLength(100, -1, 100))
        assertNull(CacheAvailability.requiredLength(101, -1, 100))
        assertNull(CacheAvailability.requiredLength(0, 0, 100))
        assertNull(CacheAvailability.requiredLength(-1, 10, 100))
    }

    @Test fun largeContentDoesNotOverflowWhenSeeking() {
        assertEquals(1L, CacheAvailability.requiredLength(Long.MAX_VALUE - 1, -1, Long.MAX_VALUE))
    }

    @Test fun cacheMetadataWorksWhenDatabaseLengthIsUnknown() {
        val cache = fakeCache(100, 100)
        org.junit.Assert.assertTrue(CacheAvailability.isCached(cache, "song", fallbackContentLength = -1))
    }

    @Test fun partialCacheIsNotOfflinePlayable() {
        org.junit.Assert.assertFalse(CacheAvailability.isCached(fakeCache(100, 99), "song"))
    }

    @Test fun positiveDatabaseLengthCanRecoverMissingCacheMetadata() {
        org.junit.Assert.assertTrue(CacheAvailability.isCached(fakeCache(-1, 100), "song", fallbackContentLength = 100))
        org.junit.Assert.assertFalse(CacheAvailability.isCached(fakeCache(-1, 100), "song", fallbackContentLength = -1))
    }

    @Test fun eachCacheUsesItsOwnContentLength() {
        org.junit.Assert.assertFalse(CacheAvailability.isCached(fakeCache(200, 100), "song", fallbackContentLength = 100))
    }

    private fun fakeCache(contentLength: Long, contiguousBytes: Long): androidx.media3.datasource.cache.Cache {
        val metadataType = androidx.media3.datasource.cache.ContentMetadata::class.java
        val metadata = java.lang.reflect.Proxy.newProxyInstance(metadataType.classLoader, arrayOf(metadataType)) { _, method, _ ->
            check(method.name == "get")
            contentLength
        }
        val cacheType = androidx.media3.datasource.cache.Cache::class.java
        return java.lang.reflect.Proxy.newProxyInstance(cacheType.classLoader, arrayOf(cacheType)) { _, method, args ->
            when (method.name) {
                "getContentMetadata" -> metadata
                "isCached" -> (args!![1] as Long) <= contiguousBytes &&
                    (args[2] as Long) <= contiguousBytes - (args[1] as Long)
                else -> error("Unexpected cache operation: ${method.name}")
            }
        } as androidx.media3.datasource.cache.Cache
    }
}
