package com.nikhil.yt.utils

/** Similar Content is the current song's source list; ranking only selects automatic additions. */
object SimilarContentPolicy {
    data class Batch<T>(val queue: List<T>, val preview: List<T>)

    fun <T> select(
        source: List<T>,
        ranked: List<T>,
        queuedIds: Set<String>,
        idOf: (T) -> String,
        limit: Int = 10,
    ): Batch<T> {
        val additions = RadioQueuePolicy.unseen(ranked, queuedIds, idOf).take(limit.coerceAtLeast(0))
        val preview = RadioQueuePolicy.unseen(source, queuedIds + additions.map(idOf), idOf)
        return Batch(additions, preview)
    }
}
