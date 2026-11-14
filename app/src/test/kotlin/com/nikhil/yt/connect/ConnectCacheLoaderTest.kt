package com.nikhil.yt.connect

import java.io.IOException
import kotlinx.coroutines.*
import org.junit.Assert.*
import org.junit.Test

class ConnectCacheLoaderTest {
    @Test fun aSkippedTvReaderDoesNotLeaveThePhoneCachePartial() = runBlocking {
        val background = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val started = CompletableDeferred<Unit>()
        val finish = CompletableDeferred<Unit>()
        var downloads = 0
        val loader = ConnectCacheLoader({ background }) {
            downloads++
            started.complete(Unit)
            finish.await()
        }
        try {
            val first = launch { loader.prepare("song") }
            started.await()
            first.cancelAndJoin()
            val next = async { loader.prepare("song") }
            yield()
            assertFalse(next.isCompleted)
            finish.complete(Unit)
            withTimeout(2000) { next.await() }
            assertEquals(1, downloads)
        } finally { background.cancel() }
    }

    @Test fun aFailedCacheFillCanRetryWithoutStoppingTheMusicService() = runBlocking {
        val background = CoroutineScope(Job() + Dispatchers.Default)
        var attempts = 0
        val loader = ConnectCacheLoader({ background }) {
            if (++attempts == 1) throw IOException("Network unavailable")
        }
        try {
            assertTrue(runCatching { loader.prepare("song") }.exceptionOrNull() is IOException)
            assertTrue(background.isActive)
            withTimeout(2000) { loader.prepare("song") }
            assertEquals(2, attempts)
        } finally { background.cancel() }
    }
}
