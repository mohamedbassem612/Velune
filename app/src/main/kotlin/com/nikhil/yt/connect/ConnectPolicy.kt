package com.nikhil.yt.connect

import java.security.MessageDigest

/** Pure protocol decisions, shared by the transport and JVM tests. */
internal object ConnectPolicy {
    fun mayAuthenticate(trusted: Boolean, pairingOpen: Boolean, expected: String?, actual: String): Boolean =
        actual.matches(Regex("[0-9a-f]{64}")) && (expected == null || expected == actual) && (trusted || pairingOpen)

    fun nonceCommitment(id: String, nonce: String): String =
        MessageDigest.getInstance("SHA-256").digest("velune-connect-commit-v3|$id|$nonce".toByteArray())
            .joinToString("") { "%02x".format(it) }

    fun pairingCode(firstId: String, secondId: String, firstNonce: String, secondNonce: String): String {
        val pairs = listOf(firstId to firstNonce, secondId to secondNonce).sortedBy { it.first }
        val input = "velune-connect-v3|" + pairs.joinToString("|") { "${it.first}:${it.second}" }
        return MessageDigest.getInstance("SHA-256").digest(input.toByteArray())
            .take(6).joinToString("") { "%02X".format(it) }.chunked(4).joinToString(" ")
    }

    fun retryDelay(attempt: Int, jitter: Long): Long =
        ((1000L shl attempt.coerceIn(0, 5)) + jitter.coerceIn(0, 999)).coerceAtMost(30_000)
}

/** At-most-once execution within a live session; commands are never replayed after reconnect. */
internal class ConnectCommandWindow(private val capacity: Int = 1024) {
    private val seen = LinkedHashSet<String>()
    fun accept(id: String): Boolean {
        require(id.isNotBlank() && id.length <= 100)
        if (!seen.add(id)) return false
        if (seen.size > capacity) seen.remove(seen.first())
        return true
    }
}

/** Explicit mode lease prevents asynchronous Together startup from briefly reopening Connect. */
internal class ConnectModeGate {
    var connectAllowed = true
        private set
    fun enterTogether() { connectAllowed = false }
    fun leaveTogether() { connectAllowed = true }
}
