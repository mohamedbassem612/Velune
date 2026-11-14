package com.nikhil.yt.connect

import java.io.IOException
import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class ConnectCacheTransferTest {
    @Test fun uncachedSongsUseBoundedRangesAndAnExactFinalRange() = runBlocking {
        val ranges = mutableListOf<Pair<Long, Long>>()
        val size = 2 * 524288L + 91
        val result = cachePhoneSongInChunks(524288, { size }) { offset, length ->
            ranges += offset to length
            length
        }
        assertEquals(size, result)
        assertEquals(listOf(0L to 524288L, 524288L to 524288L, 1048576L to 91L), ranges)
    }

    @Test fun firstRangeIsBoundedBeforeTheResolverKnowsTheTotalSize() = runBlocking {
        var total: Long? = null
        val ranges = mutableListOf<Pair<Long, Long>>()
        cachePhoneSongInChunks(524288, { total }) { offset, length ->
            ranges += offset to length
            total = 600000
            length
        }
        assertEquals(listOf(0L to 524288L, 524288L to 75712L), ranges)
    }

    @Test fun anUnknownLengthEndsAtTheShortFinalRange() = runBlocking {
        var calls = 0
        val size = cachePhoneSongInChunks(524288, { null }) { _, _ ->
            if (++calls == 1) 524288 else 15
        }
        assertEquals(524303L, size)
        assertEquals(2, calls)
    }

    @Test fun aTruncatedResponseIsNotReportedAsACompleteSong() = runBlocking {
        val result = runCatching { cachePhoneSongInChunks(524288, { 600000 }) { _, _ -> 1024 } }
        assertTrue(result.exceptionOrNull() is IOException)
    }
}
