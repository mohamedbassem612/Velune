package com.nikhil.yt.connect

import com.nikhil.yt.localsync.LibrarySnapshot
import com.nikhil.yt.localsync.LibrarySyncStatus
import com.nikhil.yt.localsync.LocalLibrarySync
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONObject
import java.util.UUID

/** Acknowledged per-connection deltas. Reconnect safely sends a full, idempotent snapshot. */
internal class ConnectLibraryExchange(private val library: LocalLibrarySync, private val authorized: () -> Boolean) {
    private val mutex = Mutex()
    private var acknowledged: LibrarySnapshot? = null
    private var pending: Pair<String, LibrarySnapshot>? = null

    suspend fun publish(send: (JSONObject) -> Unit) = mutex.withLock {
        check(authorized()) { "Connect authorization ended" }
        val snapshot = library.snapshot(connect = true)
        val base = acknowledged
        val delta = if (base == null) snapshot else libraryDelta(snapshot, base)
        if (base != null && delta.likes.isEmpty() && delta.playlists.isEmpty() && delta.history.isEmpty()) return@withLock
        // Re-send unacknowledged content on the next cycle; merges are idempotent.
        val id = UUID.randomUUID().toString()
        pending = id to snapshot
        library.status.value = LibrarySyncStatus.Syncing
        val raw = library.encode(delta)
        require(raw.toByteArray().size <= LocalLibrarySync.MAX_BODY_BYTES) { "Library delta exceeds the supported size" }
        send(JSONObject().put("type", "library").put("exchangeId", id).put("snapshot", JSONObject(raw)))
    }
    suspend fun receive(message: JSONObject, send: (JSONObject) -> Unit) {
        val raw = message.getJSONObject("snapshot").toString()
        require(raw.toByteArray().size <= LocalLibrarySync.MAX_BODY_BYTES)
        library.accept(library.decode(raw), connect = true, authorized = authorized)
        send(JSONObject().put("type", "libraryAck").put("exchangeId", message.getString("exchangeId")))
        publish(send)
    }
    suspend fun acknowledge(message: JSONObject) = mutex.withLock {
        val value = pending ?: return@withLock
        if (value.first == message.getString("exchangeId")) {
            acknowledged = value.second; pending = null; library.status.value = LibrarySyncStatus.Synced
        }
    }
}

