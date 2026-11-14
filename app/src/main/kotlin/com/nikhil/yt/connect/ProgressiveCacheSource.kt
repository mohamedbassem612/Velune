package com.nikhil.yt.connect

import android.net.Uri
import androidx.media3.common.C
import androidx.media3.datasource.DataSource
import androidx.media3.datasource.DataSpec
import androidx.media3.datasource.TransferListener
import java.io.IOException

/** Preparation suspends outside the RPC reader lock; reads only touch committed local bytes. */
internal class ProgressiveCacheSource(
    private val chunkSize: Long,
    private val awaitRange: suspend (String, Long, Long) -> Long,
    private val cachedSource: () -> DataSource,
    private val localSource: () -> DataSource,
) : DataSource {
    private lateinit var spec: DataSpec
    private var total = C.LENGTH_UNSET.toLong()
    private var position = 0L
    private var remaining = 0L
    private var rangeRemaining = 0L
    private var source: DataSource? = null

    suspend fun prepareOpen(dataSpec: DataSpec) {
        spec = dataSpec
        position = spec.position
        total = awaitRange(checkNotNull(spec.key), position, chunkSize)
    }

    override fun open(dataSpec: DataSpec): Long {
        if (total == C.LENGTH_UNSET.toLong()) {
            return localSource().also { source = it }.open(dataSpec)
        }
        if (position > total) throw IOException("Position is beyond cached media")
        remaining = (total - position).let { if (spec.length < 0) it else minOf(it, spec.length) }
        return remaining
    }

    suspend fun prepareRead() {
        if (total >= 0 && remaining > 0 && rangeRemaining == 0L) {
            awaitRange(checkNotNull(spec.key), position, minOf(chunkSize, remaining))
        }
    }

    override fun read(buffer: ByteArray, offset: Int, length: Int): Int {
        if (length == 0) return 0
        if (total < 0) return checkNotNull(source).read(buffer, offset, length)
        if (remaining == 0L) return C.RESULT_END_OF_INPUT
        if (rangeRemaining == 0L) {
            source?.close()
            source = null
            rangeRemaining = minOf(chunkSize, remaining)
            source = cachedSource().also {
                it.open(spec.buildUpon().setPosition(position).setLength(rangeRemaining).build())
            }
        }
        val count = checkNotNull(source).read(buffer, offset, minOf(length.toLong(), rangeRemaining).toInt())
        if (count == C.RESULT_END_OF_INPUT) throw IOException("Committed cache range ended early")
        position += count
        remaining -= count
        rangeRemaining -= count
        return count
    }

    override fun getUri(): Uri = spec.uri
    override fun addTransferListener(transferListener: TransferListener) = Unit
    override fun close() { try { source?.close() } finally { source = null } }
}
