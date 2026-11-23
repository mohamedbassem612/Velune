package com.nikhil.yt.utils

import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.SettableFuture
import java.io.IOException
import java.util.concurrent.ExecutionException
import org.junit.Assert.*
import org.junit.Test

class ArtworkLoadCacheTest {
    @Test fun prefetchedArtworkIsImmediatelyAvailableWhenTheSongChanges() {
        val source = SettableFuture.create<String>()
        var loads = 0
        val cache = ArtworkLoadCache<String, String>(6) { loads++; source }
        cache.prefetch("next-cover")
        source.set("decoded-cover")
        val notification = cache.get("next-cover")
        assertTrue(notification.isDone)
        assertEquals("decoded-cover", notification.get())
        assertEquals(1, loads)
    }

    @Test fun concurrentConsumersShareOneLoadAndCannotCancelEachOther() {
        val source = SettableFuture.create<String>()
        var loads = 0
        val cache = ArtworkLoadCache<String, String>(6) { loads++; source }
        cache.prefetch("cover")
        val oldNotification = cache.get("cover")
        val mediaSession = cache.get("cover")
        oldNotification.cancel(true)
        assertFalse(source.isCancelled)
        source.set("correct-cover")
        assertEquals("correct-cover", mediaSession.get())
        assertEquals(1, loads)
    }

    @Test fun leastRecentlyUsedArtworkIsEvictedInsteadOfKeepingTheWholeQueue() {
        val loads = mutableListOf<String>()
        val cache = ArtworkLoadCache<String, String>(2) { loads.add(it); Futures.immediateFuture(it) }
        cache.get("a").get()
        cache.get("b").get()
        cache.get("a").get()
        cache.get("c").get()
        cache.get("a").get()
        cache.get("b").get()
        assertEquals(listOf("a", "b", "c", "b"), loads)
    }

    @Test fun failedArtworkCanRetryAndNeverBecomesACachedTransparentSuccess() {
        var loads = 0
        val cache = ArtworkLoadCache<String, String>(6) {
            if (++loads == 1) Futures.immediateFailedFuture(IOException("offline"))
            else Futures.immediateFuture("real-cover")
        }
        try {
            cache.get("cover").get()
            fail("The failed load must remain a failure")
        } catch (expected: ExecutionException) {
            assertTrue(expected.cause is IOException)
        }
        assertEquals("real-cover", cache.get("cover").get())
        assertEquals(2, loads)
    }

    @Test fun speculativeLoadsAreLimitedWithoutBlockingCurrentSongArtwork() {
        val sources = mutableMapOf<String, SettableFuture<String>>()
        val cache = ArtworkLoadCache<String, String>(6) { sources.getOrPut(it) { SettableFuture.create() } }
        cache.prefetch("next")
        cache.prefetch("previous")
        cache.prefetch("further-away")
        assertEquals(setOf("next", "previous"), sources.keys)
        val current = cache.get("current")
        sources.getValue("current").set("current-cover")
        assertEquals("current-cover", current.get())
    }

    @Test fun differentCoversAreNeverReplacedWithThePreviousSongsArtwork() {
        val sources = mutableMapOf<String, SettableFuture<String>>()
        val cache = ArtworkLoadCache<String, String>(6) { sources.getOrPut(it) { SettableFuture.create() } }
        val first = cache.get("first")
        sources.getValue("first").set("first-cover")
        assertEquals("first-cover", first.get())
        val second = cache.get("second")
        assertFalse(second.isDone)
        sources.getValue("second").set("second-cover")
        assertEquals("second-cover", second.get())
    }
}
