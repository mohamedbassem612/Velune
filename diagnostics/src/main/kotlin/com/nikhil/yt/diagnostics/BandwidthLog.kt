package com.nikhil.yt.diagnostics

import java.security.MessageDigest
import java.security.SecureRandom
import java.util.ArrayDeque

/** Bounded, opt-in metadata only. No URLs with queries, headers, bodies or exception messages. */
object BandwidthLog {
    const val MAX_ENTRIES = 2000
    data class Entry(
        val id: Long,
        val timeMs: Long,
        val source: String,
        val function: String,
        val reason: String,
        val endpoint: String,
        val method: String,
        val received: Long = 0,
        val sent: Long = 0,
        val status: String = "active",
        val durationMs: Long = 0,
        /** NET, REVALIDATE, HIT, HIT-DISK or blank when the layer cannot tell. */
        val cache: String = "",
        /** Bytes served from a local cache for this row (only where the cache layer reports it). */
        val cachedBytes: Long = 0,
        /** Salted per-process hash of the full URL / media id; equal keys mean the same resource. */
        val key: String = "",
        /** FG when the app UI was in the foreground at the time of the row, BG otherwise. */
        val appState: String = "",
        /** Increases every time the app UI returns to the foreground. */
        val fgSession: Int = 0,
        /** Milliseconds since the app UI last returned to the foreground; -1 while in background. */
        val sinceFgMs: Long = -1,
    )
    data class Snapshot(
        val enabled: Boolean,
        val startedAtMs: Long,
        val received: Long,
        val sent: Long,
        val dropped: Long,
        val entries: List<Entry>,
        val cacheCounts: Map<String, Long> = emptyMap(),
    )

    @Volatile var enabled: Boolean = false
        private set
    private var generation = 0L
    private var sequence = 0L
    private var startedAtMs = 0L
    private var received = 0L
    private var sent = 0L
    private var dropped = 0L
    private val entries = LinkedHashMap<Long, Entry>()
    private val order = ArrayDeque<Long>()
    private val cacheCounts = HashMap<String, Long>()

    // App lifecycle is tracked even while recording is off so the first rows are stamped correctly.
    private var foreground = true
    private var fgSession = 0
    private var fgSinceMs = 0L
    private var lastStateChangeMs = 0L
    private val salt = ByteArray(16).also { SecureRandom().nextBytes(it) }

    /** Extra `# ...` header lines supplied by the app, such as process start time. */
    @Volatile var headerExtras: () -> String = { "" }

    @Synchronized fun setEnabled(value: Boolean) {
        if (enabled == value) return
        // Invalidate in-flight observers even when a new session is immediately started.
        generation++
        if (!value) entries.replaceAll { _, entry ->
            if (entry.status == "active") entry.copy(status = "recording stopped", durationMs = System.currentTimeMillis() - entry.timeMs)
            else entry
        }
        enabled = value
        if (value && startedAtMs == 0L) startedAtMs = System.currentTimeMillis()
    }

    @Synchronized fun clear() {
        generation++
        entries.clear()
        order.clear()
        cacheCounts.clear()
        received = 0
        sent = 0
        dropped = 0
        startedAtMs = if (enabled) System.currentTimeMillis() else 0
    }

    @Synchronized fun rollback() {
        enabled = false
        clear()
    }

    @Synchronized fun snapshot() = Snapshot(enabled, startedAtMs, received, sent, dropped, entries.values.toList(), cacheCounts.toMap())

    /** Called when the app UI moves between foreground and background. Adds a marker row while recording. */
    @Synchronized fun setForeground(value: Boolean) {
        val now = System.currentTimeMillis()
        if (value == foreground && lastStateChangeMs != 0L) return
        val previousMs = if (lastStateChangeMs == 0L) -1L else now - lastStateChangeMs
        foreground = value
        lastStateChangeMs = now
        if (value) {
            fgSession++
            fgSinceMs = now
        }
        note(
            "App", "lifecycle", "App UI moved to ${if (value) "foreground" else "background"}", "APP",
            if (value) "foreground; was in background ${previousMs}ms" else "background; was in foreground ${previousMs}ms",
        )
    }

    @Synchronized fun begin(
        source: String,
        function: String,
        reason: String,
        endpoint: String,
        method: String,
        key: String = "",
    ): Transfer? {
        if (!enabled) return null
        val id = ++sequence
        if (entries.size == MAX_ENTRIES) {
            entries.remove(order.removeFirst())
            dropped++
        }
        val now = System.currentTimeMillis()
        entries[id] = Entry(
            id, now, source, function, reason, endpoint, method,
            key = key,
            appState = if (foreground) "FG" else "BG",
            fgSession = fgSession,
            sinceFgMs = if (foreground && fgSinceMs > 0) now - fgSinceMs else -1,
        )
        order.addLast(id)
        return Transfer(id, generation, System.nanoTime())
    }

    /** A zero-byte marker row: explains a decision (for example why a cache could not be used). */
    fun note(source: String, function: String, reason: String, endpoint: String, status: String, key: String = "") {
        begin(source, function, reason, endpoint, "NOTE", key)?.finish(status)
    }

    /** A request that was answered from a local cache and therefore moved no network bytes. */
    fun cacheHit(
        source: String,
        function: String,
        reason: String,
        endpoint: String,
        method: String,
        cache: String,
        cachedBytes: Long = 0,
        key: String = "",
    ) {
        begin(source, function, reason, endpoint, method, key)?.let {
            it.cache(cache, cachedBytes)
            it.finish("served from cache")
        }
    }

    /** For very frequent hits (memory caches): only a counter is kept, never a row. */
    @Synchronized fun countCache(label: String) {
        if (!enabled) return
        cacheCounts[label] = (cacheCounts[label] ?: 0L) + 1L
    }

    /** Salted, per-process, truncated hash. Lets the export show repeats without storing URLs or ids. */
    fun keyOf(value: String): String = runCatching {
        val digest = MessageDigest.getInstance("SHA-256")
        digest.update(salt)
        digest.update(value.toByteArray())
        digest.digest().take(4).joinToString("") { "%02x".format(it) }
    }.getOrDefault("")

    class Transfer internal constructor(private val id: Long, private val session: Long, private val startNanos: Long) {
        private var finished = false

        fun bytes(received: Long = 0, sent: Long = 0) = synchronized(BandwidthLog) {
            if (finished || !enabled || session != generation) return@synchronized
            BandwidthLog.received += received.coerceAtLeast(0)
            BandwidthLog.sent += sent.coerceAtLeast(0)
            entries[id]?.let {
                entries[id] = it.copy(received = it.received + received.coerceAtLeast(0), sent = it.sent + sent.coerceAtLeast(0))
            }
        }

        fun cache(status: String, cachedBytes: Long = 0) = synchronized(BandwidthLog) {
            if (finished || !enabled || session != generation) return@synchronized
            entries[id]?.let { entries[id] = it.copy(cache = status, cachedBytes = cachedBytes.coerceAtLeast(0)) }
        }

        fun finish(status: String) = synchronized(BandwidthLog) {
            if (finished) return@synchronized
            finished = true
            if (!enabled || session != generation) return@synchronized
            entries[id]?.let {
                entries[id] = it.copy(status = status, durationMs = (System.nanoTime() - startNanos) / 1_000_000)
            }
        }
    }

    fun caller(fallback: String): String {
        if (!enabled) return fallback
        return Throwable().stackTrace.asSequence()
            .filter { (it.className.startsWith("com.nikhil.yt.") || it.className.startsWith("com.my.kizzy.")) &&
                !it.className.startsWith("com.nikhil.yt.diagnostics.") }
            .take(8)
            .joinToString(" <- ") { "${it.className.substringAfterLast('.')}.${it.methodName}:${it.lineNumber}" }
            .ifBlank { fallback }
    }

    /** A portable TSV with no user-controlled cells: endpoint is sanitized before recording. */
    fun export(snapshot: Snapshot = snapshot()): String = buildString {
        // Same resource fetched from the network more than once in this recording.
        val repeats = snapshot.entries
            .filter { it.key.isNotEmpty() && it.received > 0 && it.method != "NOTE" }
            .groupBy { it.key }
            .filterValues { it.size > 1 }
        val repeatedBytes = repeats.values.sumOf { rows -> rows.drop(1).sumOf { it.received } }
        appendLine("# Velune bandwidth diagnostics; HTTP body bytes and WebSocket application payload bytes")
        appendLine("# Byte totals exclude HTTP headers, TLS/TCP overhead and automatic WebSocket control frames.")
        appendLine("# Cache-served requests appear as rows with cache=HIT / HIT-DISK and zero network bytes where instrumented (OkHttp cache, Coil disk cache, Media3 caches). Coil memory hits are only counted below.")
        appendLine("# cache column: NET = went to the network, REVALIDATE = conditional request, HIT = served from cache. NOTE rows are zero-byte explanations. appState/fgSession/sinceFgMs show app foreground/background context.")
        appendLine("# OBSERVED rows identify opaque library / WebView activity; blank byte fields mean unmeasured, not zero.")
        appendLine("# Totals include evicted rows. Rows are limited to $MAX_ENTRIES; evicted=${snapshot.dropped}.")
        appendLine("# sessionStartMs=${snapshot.startedAtMs}\treceived=${snapshot.received}\tsent=${snapshot.sent}")
        appendLine("# repeatedNetworkKeys=${repeats.size}\trepeatedNetworkBytes=$repeatedBytes\t(same GET resource fetched from the network again)")
        appendLine("# cacheCounters ${snapshot.cacheCounts.entries.sortedBy { it.key }.joinToString(" ") { "${it.key}=${it.value}" }.ifBlank { "none" }}")
        headerExtras().takeIf { it.isNotBlank() }?.let { appendLine(it) }
        appendLine("timeMs\tsource\tfunction\treason\tendpoint\tmethod\treceivedBytes\tsentBytes\tstatus\tdurationMs\tcache\tcachedBytes\tkey\tappState\tfgSession\tsinceFgMs")
        snapshot.entries.forEach { e ->
            appendLine(listOf(e.timeMs, e.source, e.function, e.reason, e.endpoint, e.method,
                if (e.method.startsWith("OBSERVED")) "" else e.received,
                if (e.method.startsWith("OBSERVED")) "" else e.sent,
                e.status, e.durationMs, e.cache, e.cachedBytes, e.key, e.appState, e.fgSession, e.sinceFgMs)
                .joinToString("\t") { it.toString().replace('\t', ' ').replace('\n', ' ') })
        }
    }
}
