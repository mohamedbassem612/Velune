package com.nikhil.yt.connect

import java.nio.file.Files
import java.security.KeyStore
import java.security.PrivateKey
import java.security.cert.X509Certificate
import javax.net.ssl.SSLSocket
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test

/** Real loopback TLS/protocol tests with ephemeral JVM keys. Android Keystore is tested separately. */
class ConnectTransportTest {
    private class Pins : ConnectPins {
        private val values = java.util.concurrent.ConcurrentHashMap<String, String>()
        override fun contains(id: String) = values.containsKey(id)
        override fun trust(id: String, name: String) { values[id] = name }
        override fun revoke(id: String) { values.remove(id) }
    }
    private fun credentials(): ConnectCredentials {
        val dir = Files.createTempDirectory("velune-connect-test").toFile()
        try {
            val file = dir.resolve("keys.p12")
            val process = ProcessBuilder("${System.getProperty("java.home")}/bin/keytool", "-genkeypair",
                "-alias", "device", "-keyalg", "RSA", "-keysize", "2048", "-sigalg", "SHA256withRSA",
                "-dname", "CN=Connect Test", "-validity", "2", "-storetype", "PKCS12", "-keystore", file.path,
                "-storepass", "test-only-password", "-keypass", "test-only-password", "-noprompt")
                .redirectErrorStream(true).start()
            val output = process.inputStream.bufferedReader().readText()
            check(process.waitFor() == 0) { output }
            val store = KeyStore.getInstance("PKCS12").apply { file.inputStream().use { load(it, "test-only-password".toCharArray()) } }
            return object : ConnectCredentials {
                override val certificate = store.getCertificate("device") as X509Certificate
                override val privateKey = store.getKey("device", "test-only-password".toCharArray()) as PrivateKey
            }
        } finally { dir.deleteRecursively() }
    }
    private val phone by lazy { credentials() }
    private val tv by lazy { credentials() }

    @Test fun unknownPeerCannotSendCommandsWithPairingClosed() = runBlocking {
        val a = ConnectTransport(phone, Pins()); val b = ConnectTransport(tv, Pins())
        val listener = b.server()
        try {
            val server = async(Dispatchers.IO) {
                val socket = listener.accept() as SSLSocket
                try { runCatching { b.authorize(socket, true) }.isFailure } finally { socket.close() }
            }
            val client = async(Dispatchers.IO) {
                runCatching { a.connect("127.0.0.1", listener.localPort, tv.id, false) }.isFailure
            }
            assertTrue(withTimeout(15_000) { client.await() })
            assertTrue(withTimeout(15_000) { server.await() })
        } finally { listener.close() }
    }

    @Test fun dualApprovalThenPinnedReconnectAndChannelReplayRejection() = runBlocking {
        val ap = Pins(); val bp = Pins()
        val a = ConnectTransport(phone, ap); val b = ConnectTransport(tv, bp)
        val listener = b.server()
        suspend fun open(pair: Boolean): Pair<ConnectChannel, ConnectChannel> = coroutineScope {
            val server = async(Dispatchers.IO) {
                val socket = listener.accept() as SSLSocket
                try { b.authorize(socket, true) } catch (e: Exception) { socket.close(); throw e }
            }
            val client = async(Dispatchers.IO) {
                val socket = a.connect("127.0.0.1", listener.localPort, tv.id, pair)
                try { a.authorize(socket, false) } catch (e: Exception) { socket.close(); throw e }
            }
            if (pair) {
                val aa = withTimeout(10_000) { a.pairing.filterNotNull().first() }
                val bb = withTimeout(10_000) { b.pairing.filterNotNull().first() }
                assertEquals(aa.code, bb.code)
                assertFalse(ap.contains(tv.id)); assertFalse(bp.contains(phone.id))
                a.approve(true); b.approve(true)
            }
            client.await() to server.await()
        }
        try {
            b.openPairing()
            val initial = withTimeout(25_000) { open(true) }
            initial.first.close(); initial.second.close()
            assertTrue(ap.contains(tv.id)); assertTrue(bp.contains(phone.id))
            val again = withTimeout(15_000) { open(false) }
            try {
                again.first.write(JSONObject().put("type", "command"))
                val first = again.second.read()!!
                assertEquals("command", first.getString("type"))
                // Simulate a duplicate application frame over the same authenticated TLS stream.
                again.first.socket.getOutputStream().write((first.toString() + "\n").toByteArray())
                again.first.socket.getOutputStream().flush()
                assertTrue(runCatching { again.second.read() }.exceptionOrNull() is IllegalArgumentException)
            } finally { again.first.close(); again.second.close() }
            bp.revoke(phone.id)
            assertFalse(bp.contains(phone.id))
            val rejectedServer = async(Dispatchers.IO) {
                val socket = listener.accept() as SSLSocket
                try { runCatching { b.authorize(socket, true) }.isFailure } finally { socket.close() }
            }
            val rejectedClient = async(Dispatchers.IO) {
                runCatching {
                    val socket = a.connect("127.0.0.1", listener.localPort, tv.id, false)
                    try { a.authorize(socket, false) } finally { socket.close() }
                }.isFailure
            }
            assertTrue(withTimeout(15_000) { rejectedClient.await() })
            assertTrue(withTimeout(15_000) { rejectedServer.await() })
        } finally { listener.close() }
    }

    @Test fun rejectionOnEitherScreenDoesNotCreateTrust() = runBlocking {
        val ap = Pins(); val bp = Pins()
        val a = ConnectTransport(phone, ap); val b = ConnectTransport(tv, bp)
        b.openPairing()
        val listener = b.server()
        try {
            val server = async(Dispatchers.IO) {
                val socket = listener.accept() as SSLSocket
                try { runCatching { b.authorize(socket, true) }.isFailure } finally { socket.close() }
            }
            val client = async(Dispatchers.IO) {
                val socket = a.connect("127.0.0.1", listener.localPort, tv.id, true)
                try { runCatching { a.authorize(socket, false) }.isFailure } finally { socket.close() }
            }
            withTimeout(10_000) { a.pairing.filterNotNull().first(); b.pairing.filterNotNull().first() }
            a.approve(false); b.approve(true)
            assertTrue(withTimeout(10_000) { client.await() })
            assertTrue(withTimeout(10_000) { server.await() })
            assertFalse(ap.contains(tv.id)); assertFalse(bp.contains(phone.id))
        } finally { listener.close() }
    }

    @Test fun changedNonceAfterCommitmentIsRejectedBeforeApproval() = runBlocking {
        val ap = Pins(); val bp = Pins()
        val a = ConnectTransport(phone, ap); val b = ConnectTransport(tv, bp)
        b.openPairing()
        val listener = b.server()
        try {
            val server = async(Dispatchers.IO) {
                val socket = listener.accept() as SSLSocket
                try { runCatching { b.authorize(socket, true) }.isFailure } finally { socket.close() }
            }
            val socket = a.connect("127.0.0.1", listener.localPort, tv.id, true)
            val channel = ConnectChannel(socket, tv.id)
            try {
                channel.read()!!
                val nonce = java.util.UUID.randomUUID().toString()
                channel.write(JSONObject().put("type", "identity").put("id", phone.id).put("name", "Phone")
                    .put("protocol", 3).put("commitment", ConnectPolicy.nonceCommitment(phone.id, nonce)))
                channel.read()!!
                channel.write(JSONObject().put("type", "nonce").put("nonce", java.util.UUID.randomUUID().toString()))
                assertTrue(withTimeout(15_000) { server.await() })
                assertNull(b.pairing.value)
                assertFalse(bp.contains(phone.id))
            } finally { channel.close() }
        } finally { listener.close() }
    }
}
