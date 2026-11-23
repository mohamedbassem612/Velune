package com.nikhil.yt.utils

import com.google.common.util.concurrent.FutureCallback
import com.google.common.util.concurrent.Futures
import com.google.common.util.concurrent.ListenableFuture
import com.google.common.util.concurrent.MoreExecutors
import com.google.common.util.concurrent.SettableFuture

/** Ready artwork is returned synchronously; simultaneous consumers share one background load. */
internal class ArtworkLoadCache<K : Any, V : Any>(
    private val maxEntries: Int,
    private val load: (K) -> ListenableFuture<V>,
) {
    private val lock = Any()
    private val ready = LinkedHashMap<K, V>(maxEntries, 0.75f, true)
    private val pending = mutableMapOf<K, SettableFuture<V>>()

    init { require(maxEntries > 0) }

    fun get(key: K): ListenableFuture<V> = checkNotNull(request(key, maxPending = null))

    /** Speculation is bounded; an actual current-artwork request can always call get directly. */
    fun prefetch(key: K, maxPending: Int = 2) {
        request(key, maxPending)
    }

    private fun request(key: K, maxPending: Int?): ListenableFuture<V>? {
        val promise: SettableFuture<V>
        synchronized(lock) {
            ready[key]?.let { return Futures.immediateFuture(it) }
            pending[key]?.let { return Futures.nonCancellationPropagating(it) }
            if (maxPending != null && pending.size >= maxPending) return null
            promise = SettableFuture.create()
            pending[key] = promise
        }
        try {
            Futures.addCallback(load(key), object : FutureCallback<V> {
                override fun onSuccess(result: V) {
                    synchronized(lock) {
                        ready[key] = result
                        while (ready.size > maxEntries) ready.remove(ready.keys.first())
                        pending.remove(key)
                    }
                    promise.set(result)
                }

                override fun onFailure(error: Throwable) {
                    synchronized(lock) { pending.remove(key) }
                    promise.setException(error)
                }
            }, MoreExecutors.directExecutor())
        } catch (error: Exception) {
            synchronized(lock) { pending.remove(key) }
            promise.setException(error)
        }
        // Cancelling a speculative consumer must not cancel the image needed by MediaSession.
        return Futures.nonCancellationPropagating(promise)
    }

}
