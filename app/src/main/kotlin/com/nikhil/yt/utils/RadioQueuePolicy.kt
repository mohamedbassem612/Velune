package com.nikhil.yt.utils

/** Session context for radio: keep the selected song's direction while using the saved taste. */
object RadioQueuePolicy {
    fun isRadio(videoId: String?, playlistId: String?): Boolean =
        (!videoId.isNullOrBlank() && playlistId.isNullOrBlank()) || playlistId?.startsWith("RD") == true

    fun context(
        taste: QuickPicksRecommender.Profile,
        current: QuickPicksRecommender.Track,
    ): QuickPicksRecommender.Profile {
        val affinity = taste.artistAffinity.toMutableMap()
        current.artists.forEach { affinity[it] = maxOf(affinity[it] ?: 0.0, 0.75) }
        return taste.copy(
            artistAffinity = affinity,
            seeds = listOf(QuickPicksRecommender.Seed(current, 1.0)) + taste.seeds
                .filter { it.track.id != current.id }.take(2).map { it.copy(weight = it.weight * 0.5) },
            hasTaste = true,
        )
    }

    /** Deduplicate incoming pages without removing a valid first recommendation. */
    fun <T> unseen(items: List<T>, excludedIds: Set<String>, idOf: (T) -> String): List<T> =
        items.distinctBy(idOf).filterNot { idOf(it) in excludedIds }

    fun isCurrent(requestRevision: Long, revision: Long, seedId: String, currentId: String?): Boolean =
        requestRevision == revision && seedId == currentId
}
