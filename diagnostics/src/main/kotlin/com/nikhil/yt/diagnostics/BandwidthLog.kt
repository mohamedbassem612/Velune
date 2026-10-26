package com.nikhil.yt.diagnostics

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
    )
    data class Snapshot(
        val enabled: Boolean,
        val startedAtMs: Long,
        val received: Long,
        val sent: Long,
        val dropped: Long,
        val entries: List<Entry>,
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
        received = 0
        sent = 0
        dropped = 0
        startedAtMs = if (enabled) System.currentTimeMillis() else 0
    }

    @Synchronized fun rollback() {
        enabled = false
        clear()
    }

    @Synchronized fun snapshot() = Snapshot(enabled, startedAtMs, received, sent, dropped, entries.values.toList())

    @Synchronized fun begin(source: String, function: String, reason: String, endpoint: String, method: String): Transfer? {
        if (!enabled) return null
        val id = ++sequence
        if (entries.size == MAX_ENTRIES) {
            entries.remove(order.removeFirst())
            dropped++
        }
        entries[id] = Entry(id, System.currentTimeMillis(), source, function, reason, endpoint, method)
        order.addLast(id)
        return Transfer(id, generation, System.nanoTime())
    }

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
        appendLine("# Velune bandwidth diagnostics; HTTP body bytes and WebSocket application payload bytes")
        appendLine("# Excludes HTTP headers, TLS/TCP overhead, automatic WebSocket control frames and cache hits.")
        appendLine("# OBSERVED rows identify opaque library / WebView activity; blank byte fields mean unmeasured, not zero.")
        appendLine("# Totals include evicted rows. Rows are limited to $MAX_ENTRIES; evicted=${snapshot.dropped}.")
        appendLine("# sessionStartMs=${snapshot.startedAtMs}\treceived=${snapshot.received}\tsent=${snapshot.sent}")
        appendLine("timeMs\tsource\tfunction\treason\tendpoint\tmethod\treceivedBytes\tsentBytes\tstatus\tdurationMs")
        snapshot.entries.forEach { e ->
            appendLine(listOf(e.timeMs, e.source, e.function, e.reason, e.endpoint, e.method,
                if (e.method.startsWith("OBSERVED")) "" else e.received,
                if (e.method.startsWith("OBSERVED")) "" else e.sent,
                e.status, e.durationMs).joinToString("\t") { it.toString().replace('\t', ' ').replace('\n', ' ') })
        }
    }
}
