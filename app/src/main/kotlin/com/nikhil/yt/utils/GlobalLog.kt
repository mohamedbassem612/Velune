/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */



package com.nikhil.yt.utils

import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.util.concurrent.atomic.AtomicBoolean
import timber.log.Timber
import java.text.SimpleDateFormat
import java.util.*

data class LogEntry(val time: Long, val level: Int, val tag: String?, val message: String)

object GlobalLog {
    private const val MAX_ENTRIES = 500
    private const val PUBLISH_INTERVAL_MS = 250L

    private val _logs = MutableStateFlow<List<LogEntry>>(emptyList())
    val logs = _logs.asStateFlow()

    // Ring buffer: adding a log line is O(1). The list shown in the UI is rebuilt at most
    // a few times per second instead of copying 500 entries for every single log line.
    private val buffer = java.util.ArrayDeque<LogEntry>(MAX_ENTRIES + 1)
    private val lock = Any()
    private val publishPending = AtomicBoolean(false)
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Default)

    private val timeFormat = SimpleDateFormat("HH:mm:ss.SSS", Locale.getDefault())

    fun append(level: Int, tag: String?, message: String) {
        val entry = LogEntry(System.currentTimeMillis(), level, tag, message)
        synchronized(lock) {
            buffer.addLast(entry)
            while (buffer.size > MAX_ENTRIES) buffer.removeFirst()
        }
        schedulePublish()
    }

    private fun schedulePublish() {
        if (!publishPending.compareAndSet(false, true)) return
        scope.launch {
            delay(PUBLISH_INTERVAL_MS)
            publishPending.set(false)
            _logs.value = synchronized(lock) { buffer.toList() }
        }
    }

    fun clear() {
        synchronized(lock) { buffer.clear() }
        _logs.value = emptyList()
    }

    fun format(entry: LogEntry): String {
        val ts = timeFormat.format(Date(entry.time))
        val lvl = when (entry.level) {
            android.util.Log.VERBOSE -> "V"
            android.util.Log.DEBUG -> "D"
            android.util.Log.INFO -> "I"
            android.util.Log.WARN -> "W"
            android.util.Log.ERROR -> "E"
            else -> "?"
        }
        val tag = entry.tag ?: ""
        return "[$ts] $lvl/$tag: ${entry.message}"
    }
}

/** Timber Tree that forwards logs to GlobalLog */
class GlobalLogTree : Timber.Tree() {
    override fun log(priority: Int, tag: String?, message: String, t: Throwable?) {
        try {
            val final = if (t != null) "$message\n$t" else message
            GlobalLog.append(priority, tag, final)
        } catch (_: Exception) {
            // swallow
        }
    }
}
