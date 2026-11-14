package com.nikhil.yt.connect

import java.util.concurrent.ConcurrentHashMap
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async

/** One complete phone cache fill per song, independent of an individual TV reader. */
internal class ConnectCacheLoader(
    private val scope: () -> CoroutineScope,
    private val cache: suspend (String) -> Unit,
) {
    private val loading = ConcurrentHashMap<String, Deferred<Result<Unit>>>()

    suspend fun prepare(id: String) {
        val job = synchronized(loading) {
            loading[id] ?: scope().async(start = CoroutineStart.LAZY) {
                try {
                    cache(id)
                    Result.success(Unit)
                } catch (cancel: CancellationException) {
                    throw cancel
                } catch (failure: Exception) {
                    // A failed download must not cancel the service's playback scope.
                    Result.failure(failure)
                }
            }.also { task ->
                loading[id] = task
                task.invokeOnCompletion { loading.remove(id, task) }
                task.start()
            }
        }
        job.await().getOrThrow()
    }
}
