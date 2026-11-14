package com.nikhil.yt.connect

import java.io.IOException
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

/** Bound every upstream range, including the first one, just like normal phone playback. */
internal suspend fun cachePhoneSongInChunks(
    chunkSize: Long,
    contentLength: suspend () -> Long?,
    readRange: suspend (position: Long, length: Long) -> Long,
): Long {
    require(chunkSize > 0)
    var position = 0L
    while (true) {
        currentCoroutineContext().ensureActive()
        // The first resolver can persist the total length while reading the first range.
        val total = contentLength()?.takeIf { it > 0 }
        if (total != null && position >= total) return position
        val requested = if (total == null) chunkSize else minOf(chunkSize, total - position)
        val read = readRange(position, requested)
        check(read in 0..requested) { "Invalid audio range length" }
        position += read
        if (read < requested) {
            if (total != null && position < total) throw IOException("Audio range ended before the advertised length")
            return position
        }
    }
}
