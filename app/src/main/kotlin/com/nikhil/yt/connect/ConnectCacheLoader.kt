package com.nikhil.yt.connect

import java.util.concurrent.ConcurrentHashMap
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Deferred
import kotlinx.coroutines.async
import kotlinx.coroutines.delay

/** One complete phone cache fill per song, independent of an individual TV reader. */
internal class ConnectCacheLoader(
    private val scope: () -> CoroutineScope,
    private val cache: suspend (String) -> Unit,
) {
    private val loading = ConcurrentHashMap<String, Deferred<Result<Unit>>>()

    @Volatile private var foreground: String? = null

    fun start(id: String, prioritize: Boolean = false): Deferred<Result<Unit>> {
        if (prioritize) foreground = id
        return synchronized(loading) {
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
    }

    suspend fun prepare(id: String) = start(id).await().getOrThrow()

    /** Older songs finish in the background after the current song has its bandwidth. */
    suspend fun awaitTurn(id: String) {
        while (foreground?.let { it != id && loading[it]?.isActive == true } == true) delay(50)
    }

    /** Readiness means committed cache bytes, not merely bytes received from the network. */
    suspend fun prepareUntil(id: String, ready: suspend () -> Boolean) {
        if (ready()) return
        val job = start(id)
        while (!ready()) {
            if (job.isCompleted) {
                job.await().getOrThrow()
                check(ready()) { "Requested range was not retained in the phone cache" }
                return
            }
            delay(50)
        }
    }
}
