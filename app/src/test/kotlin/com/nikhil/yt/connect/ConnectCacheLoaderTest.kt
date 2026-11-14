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
    @Test fun committedPrefixCanPlayWhileTheRestIsStillDownloading() = runBlocking {
        val background = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val prefix = CompletableDeferred<Unit>()
        val finish = CompletableDeferred<Unit>()
        val loader = ConnectCacheLoader({ background }) {
            prefix.complete(Unit)
            finish.await()
        }
        try {
            val complete = loader.start("song", prioritize = true)
            withTimeout(2000) { loader.prepareUntil("song") { prefix.isCompleted } }
            assertFalse(complete.isCompleted)
            finish.complete(Unit)
            withTimeout(2000) { complete.await().getOrThrow() }
        } finally { background.cancel() }
    }

    @Test fun anUncommittedRangeWaitsAndReportsDownloadFailure() = runBlocking {
        val background = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val started = CompletableDeferred<Unit>()
        val finish = CompletableDeferred<Unit>()
        val loader = ConnectCacheLoader({ background }) {
            started.complete(Unit)
            finish.await()
            throw IOException("Connection lost")
        }
        try {
            val waiting = async { runCatching { loader.prepareUntil("song") { false } } }
            started.await()
            assertFalse(waiting.isCompleted)
            finish.complete(Unit)
            assertTrue(withTimeout(2000) { waiting.await() }.exceptionOrNull() is IOException)
            assertTrue(background.isActive)
        } finally { background.cancel() }
    }

    @Test fun switchingSongsPausesOldRangesUntilTheNewSongFinishes() = runBlocking {
        val background = CoroutineScope(SupervisorJob() + Dispatchers.Default)
        val oldFirstRange = CompletableDeferred<Unit>()
        val allowOldNextRange = CompletableDeferred<Unit>()
        val oldResumed = CompletableDeferred<Unit>()
        val newStarted = CompletableDeferred<Unit>()
        val finishNew = CompletableDeferred<Unit>()
        lateinit var loader: ConnectCacheLoader
        loader = ConnectCacheLoader({ background }) { id ->
            if (id == "old") {
                oldFirstRange.complete(Unit)
                allowOldNextRange.await()
                loader.awaitTurn(id)
                oldResumed.complete(Unit)
            } else {
                newStarted.complete(Unit)
                finishNew.await()
            }
        }
        try {
            val old = loader.start("old", prioritize = true)
            oldFirstRange.await()
            val newer = loader.start("new", prioritize = true)
            newStarted.await()
            allowOldNextRange.complete(Unit)
            delay(100)
            assertFalse(oldResumed.isCompleted)
            finishNew.complete(Unit)
            withTimeout(2000) { newer.await().getOrThrow(); old.await().getOrThrow() }
            assertTrue(oldResumed.isCompleted)
        } finally { background.cancel() }
    }

    @Test fun anAlreadyCachedRangeDoesNotStartAnotherDownload() = runBlocking {
        val loader = ConnectCacheLoader({ this }) { error("Must use saved bytes") }
        loader.prepareUntil("song") { true }
    }

}
