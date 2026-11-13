package com.nikhil.yt.connect

import java.net.Socket
import java.security.Principal
import java.security.SecureRandom
import java.security.cert.CertificateException
import java.security.cert.X509Certificate
import java.util.UUID
import javax.net.ssl.*
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch
import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.sync.Semaphore
import kotlinx.coroutines.sync.withPermit
import kotlinx.coroutines.withContext
import kotlinx.coroutines.withTimeout
import org.json.JSONObject

data class ConnectPairingRequest(val deviceId: String, val name: String, val code: String)

/** TLS proves key possession. LAN mode admits local peers without a pairing ceremony. */
internal class ConnectTransport(
    private val identity: ConnectCredentials, private val trust: ConnectPins, private val deviceName: String = "Velune",
    private val localNetworkAccess: Boolean = false,
) {
    val pairing = MutableStateFlow<ConnectPairingRequest?>(null)
    val pairingOpen = MutableStateFlow(false)
    private fun now() = System.nanoTime() / 1_000_000
    private var pairingUntil = 0L
    private var decision: CompletableDeferred<Boolean>? = null
    private val approvalSlot = Semaphore(1)
    fun openPairing() { pairingUntil = now() + 120_000; pairingOpen.value = true }
    fun closePairing() { pairingUntil = 0; pairingOpen.value = false; decision?.complete(false) }
    fun approve(allowed: Boolean) { decision?.complete(allowed) }
    private fun mayPair(): Boolean {
        val open = now() < pairingUntil
        if (!open) pairingOpen.value = false
        return open
    }

    private fun context(expected: String?, allowPair: Boolean): SSLContext {
        val keys = object : X509ExtendedKeyManager() {
            override fun getClientAliases(keyType: String?, issuers: Array<out Principal>?) = arrayOf("connect")
            override fun chooseClientAlias(keyType: Array<out String>?, issuers: Array<out Principal>?, socket: Socket?) = "connect"
            override fun getServerAliases(keyType: String?, issuers: Array<out Principal>?) = if (keyType == "RSA") arrayOf("connect") else null
            override fun chooseServerAlias(keyType: String?, issuers: Array<out Principal>?, socket: Socket?) = if (keyType == "RSA") "connect" else null
            override fun getCertificateChain(alias: String?) = arrayOf(identity.certificate)
            override fun getPrivateKey(alias: String?) = identity.privateKey
        }
        val pins = object : X509TrustManager {
            override fun getAcceptedIssuers(): Array<X509Certificate> = emptyArray()
            private fun check(chain: Array<out X509Certificate>?) {
                val cert = chain?.singleOrNull() ?: throw CertificateException("Missing device identity")
                cert.checkValidity()
                val id = publicKeyId(cert)
                if (!ConnectPolicy.mayAuthenticate(trust.contains(id), localNetworkAccess || (allowPair && mayPair()), expected, id))
                    throw CertificateException("Device is not paired or its identity changed")
            }
            override fun checkClientTrusted(chain: Array<out X509Certificate>?, authType: String?) = check(chain)
            override fun checkServerTrusted(chain: Array<out X509Certificate>?, authType: String?) = check(chain)
        }
        return SSLContext.getInstance("TLS").apply { init(arrayOf(keys), arrayOf(pins), SecureRandom()) }
    }

    private fun requireLocalPeer(socket: Socket) {
        val address = socket.inetAddress
        require(address.isSiteLocalAddress || address.isLinkLocalAddress || address.isLoopbackAddress) {
            "Velune Connect requires a local network connection"
        }
    }

    fun server(): SSLServerSocket = (context(null, true).serverSocketFactory.createServerSocket(0) as SSLServerSocket).apply {
        needClientAuth = true
        enabledProtocols = supportedProtocols.filter { it == "TLSv1.3" || it == "TLSv1.2" }.toTypedArray()
    }

    suspend fun connect(host: String, port: Int, expected: String, pair: Boolean): SSLSocket = withContext(Dispatchers.IO) {
        if (pair && !localNetworkAccess) openPairing()
        val raw = Socket()
        try {
            raw.connect(java.net.InetSocketAddress(host, port), 5000)
            if (localNetworkAccess) requireLocalPeer(raw)
            (context(expected, pair).socketFactory.createSocket(raw, host, port, true) as SSLSocket).apply {
                enabledProtocols = supportedProtocols.filter { it == "TLSv1.3" || it == "TLSv1.2" }.toTypedArray()
                soTimeout = 10_000; startHandshake()
            }
        } catch (failure: Exception) { raw.close(); throw failure }
    }

    /** No queue, commands or library records are sent before this returns. */
    suspend fun authorize(socket: SSLSocket, server: Boolean): ConnectChannel = withContext(Dispatchers.IO) {
        socket.soTimeout = 10_000
        if (localNetworkAccess) requireLocalPeer(socket)
        if (server) socket.startHandshake()
        val id = publicKeyId(socket.session.peerCertificates.single() as X509Certificate)
        val channel = ConnectChannel(socket, id)
        if (localNetworkAccess) {
            channel.write(JSONObject().put("type", "identity").put("id", identity.id)
                .put("name", deviceName.take(100)).put("protocol", 4))
            val hello = channel.read() ?: error("Peer disconnected")
            require(hello.getString("type") == "identity" && hello.getInt("protocol") == 4 && hello.getString("id") == id)
            channel.name = hello.getString("name").take(100)
            trust.trust(id, channel.name)
            socket.soTimeout = 15_000
            return@withContext channel
        }
        val nonce = UUID.randomUUID().toString()
        channel.write(JSONObject().put("type", "identity").put("id", identity.id)
            .put("name", if (server) "Velune TV" else deviceName.take(100))
            .put("commitment", ConnectPolicy.nonceCommitment(identity.id, nonce)).put("protocol", 3))
        val hello = channel.read() ?: error("Peer disconnected")
        require(hello.getString("type") == "identity" && hello.getInt("protocol") == 3 && hello.getString("id") == id)
        // Commit before revealing either nonce: a MITM cannot adapt its nonce after seeing ours.
        val commitment = hello.getString("commitment")
        require(commitment.matches(Regex("[0-9a-f]{64}")))
        channel.write(JSONObject().put("type", "nonce").put("nonce", nonce))
        val reveal = channel.read() ?: error("Peer disconnected")
        require(reveal.getString("type") == "nonce")
        val peerNonce = reveal.getString("nonce")
        require(peerNonce.length == 36 && UUID.fromString(peerNonce).toString() == peerNonce)
        require(ConnectPolicy.nonceCommitment(id, peerNonce) == commitment) { "Pairing commitment mismatch" }
        val name = hello.getString("name").take(100)
        channel.name = name
        val known = trust.contains(id)
        channel.write(JSONObject().put("type", "trust").put("known", known))
        val remoteTrust = channel.read() ?: error("Peer disconnected")
        require(remoteTrust.getString("type") == "trust")
        if (!known || !remoteTrust.getBoolean("known")) {
            check(mayPair()) { "Open pairing on both devices" }
            approvalSlot.withPermit {
                check(mayPair()) { "Pairing window expired" }
                check(pairing.value == null)
                val approval = CompletableDeferred<Boolean>()
                decision = approval
                pairing.value = ConnectPairingRequest(id, name,
                    ConnectPolicy.pairingCode(identity.id, id, nonce, peerNonce))
                try {
                    val allowed = withTimeout(90_000) { approval.await() }
                    channel.write(JSONObject().put("type", "approval").put("allowed", allowed))
                    check(allowed) { "Pairing rejected" }
                    socket.soTimeout = 90_000
                    val remote = channel.read() ?: error("Peer disconnected")
                    check(remote.getString("type") == "approval" && remote.getBoolean("allowed")) { "Pairing rejected by peer" }
                    check(mayPair()) { "Pairing window expired" }
                    trust.trust(id, name)
                } finally { decision = null; pairing.value = null; closePairing() }
            }
        }
        check(trust.contains(id))
        socket.soTimeout = 15_000
        channel
    }
}

/** TLS records provide confidentiality/integrity; channel sequence numbers reject application replay. */
internal class ConnectChannel(val socket: SSLSocket, val id: String) {
    var name: String = "Velune"
    private val reader = socket.getInputStream().bufferedReader(Charsets.UTF_8)
    private val writer = socket.getOutputStream().bufferedWriter(Charsets.UTF_8)
    private var outgoing = 0L
    private var incoming = 0L
    @Synchronized fun write(message: JSONObject) {
        message.put("seq", ++outgoing)
        val raw = message.toString()
        require(raw.toByteArray(Charsets.UTF_8).size <= MAX_BYTES)
        writer.write(raw); writer.newLine(); writer.flush()
    }
    fun read(): JSONObject? {
        val text = StringBuilder()
        while (true) {
            val next = reader.read()
            if (next < 0) return null
            if (next == 10) break
            require(text.length < MAX_BYTES)
            text.append(next.toChar())
        }
        require(text.toString().toByteArray(Charsets.UTF_8).size <= MAX_BYTES)
        return JSONObject(text.toString()).also { require(it.getLong("seq") == ++incoming) { "Invalid sequence" } }
    }
    private val closing = java.util.concurrent.atomic.AtomicBoolean(false)
    fun close() {
        if (!closing.compareAndSet(false, true)) return
        // TLS close_notify can wait for the peer. Never block the UI/revocation caller.
        closer.launch {
            runCatching { socket.soTimeout = 200; socket.shutdownInput() }
            runCatching { socket.close() }
        }
    }
    companion object {
        const val MAX_BYTES = 12 * 1024 * 1024
        private val closer = CoroutineScope(SupervisorJob() + Dispatchers.IO)
    }
}
