package com.nikhil.yt.utils

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Job
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch

/** Accessed on the main thread. The latest link wins, including during startup restore. */
internal class PendingLinkPlayback<T>(private val scope: CoroutineScope) {
    private class Request<T>(val item: T)
    private var pending: Request<T>? = null
    private var job: Job? = null

    fun pause() {
        job?.cancel()
        job = null
    }

    fun clear() {
        pause()
        pending = null
    }

    fun submit(item: T) {
        clear()
        pending = Request(item)
    }

    fun playIfReady(awaitReady: suspend () -> Unit, play: (T) -> Unit) {
        val request = pending ?: return
        if (job?.isActive == true) return
        // Assign before starting: an already ready player may complete without suspending.
        val playbackJob = scope.launch(start = CoroutineStart.LAZY) {
            awaitReady()
            coroutineContext.ensureActive()
            if (pending !== request) return@launch
            pending = null
            play(request.item)
        }
        job = playbackJob
        playbackJob.start()
    }
}
