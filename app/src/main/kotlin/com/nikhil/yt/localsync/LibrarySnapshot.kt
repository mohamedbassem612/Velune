package com.nikhil.yt.localsync

import kotlinx.serialization.Serializable

@Serializable
data class SyncVersion(val counter: Long = 0, val device: String = "") : Comparable<SyncVersion> {
    override fun compareTo(other: SyncVersion): Int =
        compareValuesBy(this, other, { it.counter }, { it.device })
}

@Serializable
data class SyncTrack(
    val id: String,
    val title: String,
    val artists: List<String> = emptyList(),
    val duration: Int = -1,
    val thumbnail: String? = null,
)

@Serializable
data class SyncLike(val track: SyncTrack, val liked: Boolean, val version: SyncVersion = SyncVersion())

@Serializable
data class SyncPlaylist(
    val id: String,
    val name: String,
    val browseId: String? = null,
    val thumbnail: String? = null,
    val songs: List<SyncTrack> = emptyList(),
    val removed: Boolean = false,
    val version: SyncVersion = SyncVersion(),
)

@Serializable
data class SyncHistory(
    val track: SyncTrack, val timestamp: String, val playTime: Long,
    val removed: Boolean = false, val version: SyncVersion = SyncVersion(),
) {
    // Existing history has no cross-device ID. This natural key remains stable on import.
    val key: String get() = "${track.id}|$timestamp|$playTime"
}

@Serializable
data class LibrarySnapshot(
    val schema: Int = 1,
    val device: String,
    val counter: Long = 0,
    val likes: List<SyncLike> = emptyList(),
    val playlists: List<SyncPlaylist> = emptyList(),
    val history: List<SyncHistory> = emptyList(),
)

/** Logical clocks avoid relying on device wall clocks. Deletions retain their version. */
object LibraryMerge {
    const val HISTORY_LIMIT = 5000

    fun merge(local: LibrarySnapshot, remote: LibrarySnapshot): LibrarySnapshot {
        require(local.schema == 1 && remote.schema == 1) { "Unsupported library sync version" }
        return local.copy(
            counter = maxOf(local.counter, remote.counter),
            likes = (local.likes + remote.likes).groupBy { it.track.id }.values
                .map { versions -> versions.maxBy { it.version } }.sortedBy { it.track.id },
            playlists = (local.playlists + remote.playlists).groupBy { it.id }.values
                .map { versions -> versions.maxBy { it.version } }.sortedBy { it.id },
            history = (local.history + remote.history).groupBy { it.key }.values
                .map { records -> records.maxWith(compareBy<SyncHistory> { it.version }.thenBy { it.removed }) }
                .let { records -> records.filter { it.removed } + records.filterNot { it.removed }
                    .sortedByDescending { it.timestamp }.take(HISTORY_LIMIT) },
        )
    }

    /** Detect edits made by either UI since the last persisted sync snapshot. */
    fun observe(previous: LibrarySnapshot, current: LibrarySnapshot): LibrarySnapshot {
        var clock = maxOf(previous.counter, current.counter)
        fun next() = SyncVersion(++clock, previous.device)
        val oldLikes = previous.likes.associateBy { it.track.id }
        val currentLikes = current.likes.associateBy { it.track.id }
        val likes = (oldLikes.keys + currentLikes.keys).map { id ->
            val old = oldLikes[id]
            val now = currentLikes[id]
            when {
                now != null && old?.liked != true -> now.copy(version = next())
                now != null -> old!!
                old!!.liked -> old.copy(liked = false, version = next())
                else -> old
            }
        }
        val oldLists = previous.playlists.associateBy { it.id }
        val currentLists = current.playlists.associateBy { it.id }
        val playlists = (oldLists.keys + currentLists.keys).map { id ->
            val old = oldLists[id]
            val now = currentLists[id]
            when {
                now != null && (old == null || now.copy(version = SyncVersion()) != old.copy(version = SyncVersion())) -> now.copy(version = next())
                now != null -> old!!
                !old!!.removed -> old.copy(removed = true, version = next())
                else -> old
            }
        }
        val oldHistory = previous.history.associateBy { it.key }
        val currentHistory = current.history.associateBy { it.key }
        val history = (oldHistory.keys + currentHistory.keys).map { key ->
            val old = oldHistory[key]
            val now = currentHistory[key]
            when {
                now != null && old == null -> now.copy(version = next())
                now != null -> old!! // Imported records and tombstones retain identity/version.
                old!!.removed -> old
                else -> old.copy(removed = true, version = next())
            }
        }
        return current.copy(device = previous.device, counter = clock, likes = likes, playlists = playlists, history = history)
    }
}
