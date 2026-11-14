package com.nikhil.yt.handover

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.SystemClock
import android.util.AtomicFile
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.exoplayer.source.ShuffleOrder
import com.nikhil.yt.connect.*
import com.nikhil.yt.extensions.mediaItems
import com.nikhil.yt.extensions.metadata
import com.nikhil.yt.extensions.toMediaItem
import com.nikhil.yt.models.MediaMetadata
import com.nikhil.yt.playback.MusicService
import com.nikhil.yt.playback.queues.Queue
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.first
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.util.UUID
import javax.net.ssl.SSLServerSocket
import javax.net.ssl.SSLSocket

/** Velune Connect: the TV coordinates one authenticated LAN session and one audio renderer. */
class HandoverController(private val service: MusicService) : Player.Listener {
    private val audio by lazy { service.getSystemService(Context.AUDIO_SERVICE) as android.media.AudioManager }
    val controlsTvVolume get() = !isTv && connected.value && playback.value?.rendererId == hostId
    private fun currentDeviceVolume(): HandoverVolume {
        val stream = android.media.AudioManager.STREAM_MUSIC
        return HandoverVolume(audio.getStreamVolume(stream),
            if (android.os.Build.VERSION.SDK_INT >= 28) audio.getStreamMinVolume(stream) else 0,
            audio.getStreamMaxVolume(stream), audio.isStreamMute(stream), audio.isVolumeFixed)
    }
    fun adjustTvVolume(direction: Int) {
        if (controlsTvVolume && direction in listOf(-1, 1))
            request(JSONObject().put("name", "deviceVolume").put("operation", "adjust").put("value", direction))
    }
    fun setTvVolume(level: Int) {
        if (controlsTvVolume) request(JSONObject().put("name", "deviceVolume").put("operation", "set").put("value", level))
    }
    fun muteTvVolume(muted: Boolean) {
        if (controlsTvVolume) request(JSONObject().put("name", "deviceVolume").put("operation", "mute").put("muted", muted))
    }
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val identity = ConnectIdentity()
    private val trust = ConnectTrustStore(service)
    private val deviceName = android.provider.Settings.Global.getString(service.contentResolver, "device_name")
        ?.takeIf { it.isNotBlank() } ?: android.os.Build.MODEL
    private val transport = ConnectTransport(identity, trust, deviceName, localNetworkAccess = true)
    val localId = identity.id
    private val isTv = service.packageName.startsWith("com.nikhil.yt.tv")
    private val preferences = service.getSharedPreferences("connect_v3", Context.MODE_PRIVATE)
    private val sessionFile = AtomicFile(File(service.noBackupFilesDir, "connect-session-v3.json"))
    val devices = MutableStateFlow<List<HandoverDevice>>(emptyList())
    val trustedDevices = trust.devices
    val pairing = transport.pairing
    val pairingOpen = transport.pairingOpen
    val selected = MutableStateFlow<HandoverDevice?>(null)
    val playback = MutableStateFlow<HandoverPlayback?>(null)
    val connected = MutableStateFlow(false)
    val coordinator = MutableStateFlow<HandoverDevice?>(null)
    val connectedDevices = MutableStateFlow<List<ConnectTrustedDevice>>(emptyList())
    val error = MutableStateFlow<String?>(null)
    val transferStatus = MutableStateFlow<String?>(null)
    val pendingDeviceId = MutableStateFlow<String?>(null)
    // Only a successfully imported library completes onboarding for this peer identity.
    val firstLibrarySync = MutableStateFlow<Map<String, String>>(emptyMap())
    val libraryStatus get() = service.localLibrarySync.status
    var applyingState = false
        private set
    private var receivedAt = 0L
    private var offlineCheckpoint: HandoverPlayback? = null
    private var attached = false
    private var closing = false
    private val mode = ConnectModeGate()
    private val isolated get() = !mode.connectAllowed
    private var reconnectEnabled = preferences.getBoolean("reconnect", false)
    private var authority: HandoverSessionAuthority? = null
    private val nsd = service.getSystemService(Context.NSD_SERVICE) as NsdManager
    private var server: SSLServerSocket? = null
    private val incomingSlots = kotlinx.coroutines.sync.Semaphore(8)
    private var registration: NsdManager.RegistrationListener? = null
    private var discovery: NsdManager.DiscoveryListener? = null
    private val found = mutableMapOf<String, HandoverDevice>()
    private val peers = mutableMapOf<String, Peer>()
    @Volatile private var libraryPeerId: String? = null
    private var clientPeer: Peer? = null
    private var clientJob: Job? = null
    private var target: HandoverDevice? = null
    private var hostId = ""
    private var rendererSequence = 0L
    private var lastPublishedAt = 0L
    private var lastPersistedAt = 0L
    private var transferJob: Job? = null
    private var releasedTransferId: String? = null
    private var releasedEpoch = -1L
    val controlsFrozen get() = preparedId != null || releasedTransferId != null || transferStatus.value != null
    private var preparedId: String? = null
    private var preparedBackup: HandoverPlayback? = null
    private var preparedJob: Job? = null
    private val responses = mutableMapOf<String, CompletableDeferred<JSONObject>>()
    private val pendingCommands = mutableMapOf<String, CompletableDeferred<JSONObject>>()
    private val commandWindow = ConnectCommandWindow()

    private inner class Peer(val channel: ConnectChannel) {
        val id get() = channel.id
        private val jobs = SupervisorJob(scope.coroutineContext[Job])
        val work = CoroutineScope(scope.coroutineContext + jobs)
        val outgoing = Channel<JSONObject>(64)
        var sentQueueRevision = -1L
        var sentSessionId = ""
        var mediaVersion = 0
        var assetVersion = 0
        val media = ConnectMedia(work, ::send, service::offlinePlayable,
            { service.createProgressiveConnectSource() }, service::exportConnectAssets, service::cacheConnectSong)
        val commands = ConnectCommandWindow()
        var rendererSequence = -1L
        val library = ConnectLibraryExchange(service.localLibrarySync, onReceived = {
            if (isTv && peers[id] === this) {
                firstLibrarySync.value = firstLibrarySync.value - id
            }
        }, tvPeer = id.takeIf { isTv }, enabled = { !isTv || libraryPeerId == id }) { trust.contains(channel.id) && !isolated }
        fun send(message: JSONObject) {
            if (!outgoing.trySend(message).isSuccess) close()
        }
        fun close() {
            media.close()
            scope.launch {
                if (peers[id] === this@Peer) firstLibrarySync.value = firstLibrarySync.value - id
            }
            jobs.cancel(); outgoing.close(); channel.close()
        }
        suspend fun writeLoop() = withContext(Dispatchers.IO) {
            try { for (message in outgoing) channel.write(message) }
            catch (e: CancellationException) { throw e }
            catch (e: Exception) { error.value = e.message ?: "Connect transport interrupted" }
            finally { channel.close() }
        }
    }

    private val assetMutex = kotlinx.coroutines.sync.Mutex()
    private val assetResults = mutableMapOf<String, Pair<Long, JSONObject>>()

    private suspend fun fetchAssets(id: String): JSONObject? {
        assetMutex.lock()
        try {
            val providers = withContext(Dispatchers.Main) {
                if (closing || isolated) emptyList() else peers.values.filter { it.assetVersion == 1 }.map { it.media }
            }
            if (providers.isEmpty()) return null
            val key = providers.joinToString { System.identityHashCode(it).toString() } + ":" + id
            assetResults[key]?.takeIf { SystemClock.elapsedRealtime() - it.first < 30_000 }?.let { return it.second }
            val missing = withContext(Dispatchers.IO) { service.missingConnectAssets(id) }
            if (missing.isEmpty()) return JSONObject()
            val combined = JSONObject()
            for (provider in providers) {
                try {
                    val payload = provider.request(JSONObject().put("operation", "assets").put("song", id)
                        .put("fields", JSONArray(missing.filterNot { combined.has(it) }))).getJSONObject("assets")
                    for (field in listOf("metadata", "lyrics", "cover")) {
                        if (!combined.has(field) && payload.has(field)) combined.put(field, payload.get(field))
                    }
                    if (missing.all { combined.has(it) }) break
                } catch (cancel: CancellationException) { throw cancel
                } catch (_: Exception) { /* Another provider or the normal resolver can supply missing assets. */ }
            }
            withContext(Dispatchers.IO) { service.importConnectAssets(id, combined) }
            combined.remove("cover") // Persisted on disk; do not retain Base64 artwork in TV memory.
            if (assetResults.size >= 32) assetResults.clear()
            assetResults[key] = SystemClock.elapsedRealtime() to combined
            return combined
        } finally { assetMutex.unlock() }
    }

    internal val hasMediaProvider get() = isTv && peers.values.any { it.mediaVersion == 1 }

    /** Snapshot peer references on Main; all byte I/O runs on the player loading thread. */
    internal fun mediaSources(spec: androidx.media3.datasource.DataSpec): List<androidx.media3.datasource.DataSource> {
        if (!isTv) return emptyList()
        return ConnectMedia.blocking {
            val providers = withContext(Dispatchers.Main) {
                if (closing || isolated) emptyList() else peers.values.filter { it.mediaVersion == 1 }.map { it.media }
            }
            if (providers.isEmpty() || spec.key == null) emptyList()
            else providers.map { it.source(spec, cachedOnly = true) } + providers.map { it.source(spec, cachedOnly = false) }
        }
    }

    fun openPairing() { transport.openPairing(); scope.launch { delay(120_000); transport.closePairing() } }
    fun approvePairing(approve: Boolean) = transport.approve(approve)
    fun closePairing() = transport.closePairing()
    fun revoke(id: String) {
        trust.revoke(id)
        peers[id]?.close()
        if (target?.id == id) disconnect()
    }

    /** Together and Connect never share a phone player concurrently. */
    fun isolateForTogether() {
        if (isTv) return
        mode.enterTogether()
        disconnect()
    }
    fun allowAfterTogether() { mode.leaveTogether() }
    private fun available(): Boolean {
        if (isolated) { error.value = "Leave Listen Together before using Velune Connect"; return false }
        return !closing
    }

    fun attach() {
        if (attached) return
        attached = true
        if (isTv) ConnectAssetAccess.fetcher = { fetchAssets(it) }
        service.player.addListener(this)
        scope.launch {
            service.awaitStartupRestore()
            if (isTv) restoreAuthority()
            while (isActive) { publishLocal(); delay(500) }
        }
        if (!isTv) {
            startDiscovery()
        }
    }
    override fun onEvents(player: Player, events: Player.Events) {
        if (releasedTransferId != null && !applyingState && service.player.playWhenReady) {
            applyingState = true
            try { service.player.pause() } finally { applyingState = false }
            return
        }
        if (!applyingState && SystemClock.elapsedRealtime() - lastPublishedAt > 80) publishLocal()
    }
    fun currentPosition(): Long {
        val state = playback.value ?: return 0
        val elapsed = if (connected.value && state.playing && state.playbackState == Player.STATE_READY)
            ((SystemClock.elapsedRealtime() - receivedAt).coerceIn(0, 2000) * state.speed).toLong() else 0L
        return (state.positionMs + elapsed).coerceAtMost(state.durationMs.takeIf { it > 0 } ?: Long.MAX_VALUE)
    }
    private fun capture(): HandoverPlayback {
        val player = service.player
        val queue = player.mediaItems.map { item ->
            (item.metadata ?: MediaMetadata(item.mediaId, item.mediaMetadata.title?.toString().orEmpty(),
                emptyList(), -1, item.mediaMetadata.artworkUri?.toString()))
                .copy(liked = false, likedDate = null, inLibrary = null, setVideoId = null)
        }
        val order = playbackOrder(player)
        return HandoverPlayback(queue, player.currentMediaItemIndex,
            player.currentPosition.coerceAtLeast(0), player.duration.takeIf { it != C.TIME_UNSET && it > 0 } ?: 0,
            player.playWhenReady, service.queueTitle, player.repeatMode, player.shuffleModeEnabled,
            order.takeIf { it.size == queue.size && it.toSet() == queue.indices.toSet() } ?: queue.indices.toList(),
            player.playbackParameters.speed, rendererId = localId, playbackState = player.playbackState, deviceVolume = currentDeviceVolume())
    }

    private fun publishLocal() {
        if (applyingState || closing || isolated || controlsFrozen) return
        lastPublishedAt = SystemClock.elapsedRealtime()
        if (isTv) {
            val owner = authority ?: return
            if (owner.state.rendererId != localId) return
            owner.update(localId, owner.state.epoch, capture())
            acceptState(owner.state); broadcast(); persistAuthority()
        } else {
            val peer = clientPeer ?: return
            val state = playback.value ?: return
            if (!connected.value) return
            if (state.rendererId != localId) { peer.send(JSONObject().put("type", "ping")); return }
            peer.send(JSONObject().put("type", "rendererState").put("rendererSequence", ++rendererSequence)
                .put("state", capture().copy(sessionId = state.sessionId, epoch = state.epoch).toSessionJson()))
        }
    }
    private fun acceptState(state: HandoverPlayback) {
        val previous = playback.value
        val remote = state.rendererId != localId
        selected.value = if (!remote) null else if (isTv)
            HandoverDevice(trust.devices.value.firstOrNull { it.id == state.rendererId }?.name ?: "Phone", "", 0, state.rendererId) else target
        playback.value = state; receivedAt = SystemClock.elapsedRealtime()
        val takeover = !remote && (previous == null || previous.rendererId != localId || previous.epoch != state.epoch)
        if ((remote || takeover) && preparedId == null) {
            applyingState = true
            try { service.applyHandoverState(state, renderLocally = !remote) }
            finally { applyingState = false }
        }
        service.sessionPlayer.onSessionStateChanged(previous, state)
    }
    private fun broadcast() {
        val state = authority?.state ?: return
        peers.values.forEach { peer ->
            val full = peer.sentQueueRevision != state.queueRevision || peer.sentSessionId != state.sessionId
            peer.send(JSONObject().put("type", "state").put("hostId", localId).put("state", state.toSessionJson(full)))
            peer.sentQueueRevision = state.queueRevision; peer.sentSessionId = state.sessionId
        }
    }
    private fun persistAuthority(force: Boolean = false) {
        if (!isTv || (!force && SystemClock.elapsedRealtime() - lastPersistedAt < 2000)) return
        val state = authority?.state ?: return
        lastPersistedAt = SystemClock.elapsedRealtime()
        val raw = state.toSessionJson().put("coordinatorId", localId).toString()
        // Small serialized disk writes; transfer commits call this before announcing ownership.
        val output = sessionFile.startWrite()
        try { output.write(raw.toByteArray()); sessionFile.finishWrite(output) }
        catch (failure: Exception) { sessionFile.failWrite(output); error.value = "Cannot persist Connect session"; throw failure }
    }
    private fun restoreAuthority() {
        if (authority != null) return
        if (!sessionFile.baseFile.exists() && !File(sessionFile.baseFile.path + ".bak").exists()) return
        runCatching {
            val saved = sessionFile.openRead().bufferedReader().use { JSONObject(it.readText()) }
            require(saved.getString("coordinatorId") == localId) { "Coordinator identity changed" }
            val state = saved.sessionState()
            // No unsolicited restart audio; user may resume after Android starts the service.
            val restored = recoverHandoverSession(state)
            authority = HandoverSessionAuthority(restored)
            acceptState(restored); persistAuthority(true)
        }.onFailure { error.value = "Stored Connect session cannot be restored; existing library is retained" }
    }

    fun startTvHost() {
        attach()
        if (server != null) return
        scope.launch(Dispatchers.IO) {
            try {
                val listener = transport.server(); server = listener
                val info = NsdServiceInfo().apply {
                    serviceName = deviceName; serviceType = HANDOVER_SERVICE_TYPE; port = listener.localPort
                    setAttribute("id", localId); setAttribute("protocol", "4")
                }
                registration = object : NsdManager.RegistrationListener {
                    override fun onServiceRegistered(info: NsdServiceInfo) = Unit
                    override fun onRegistrationFailed(info: NsdServiceInfo, code: Int) { error.value = "TV discovery unavailable ($code)" }
                    override fun onServiceUnregistered(info: NsdServiceInfo) = Unit
                    override fun onUnregistrationFailed(info: NsdServiceInfo, code: Int) = Unit
                }.also { nsd.registerService(info, NsdManager.PROTOCOL_DNS_SD, it) }
                while (isActive) {
                    val socket = listener.accept() as SSLSocket
                    if (!incomingSlots.tryAcquire()) { socket.close(); continue }
                    launch { try { handle(socket) } finally { incomingSlots.release() } }
                }
            } catch (e: Exception) { if (!closing) error.value = e.message ?: "TV connection failed" }
        }
    }
    fun resumeFromUi() {
        if (isTv || closing || !preferences.getBoolean("reconnect", false)) return
        reconnectEnabled = true
        startDiscovery()
        found[preferences.getString("tvId", null)]?.let { connect(it, pair = false) }
    }
    fun startDiscovery() {
        if (isTv || discovery != null || closing) return
        discovery = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(type: String) = Unit
            override fun onDiscoveryStopped(type: String) = Unit
            override fun onStartDiscoveryFailed(type: String, code: Int) { discovery = null; error.value = "Discovery unavailable ($code)" }
            override fun onStopDiscoveryFailed(type: String, code: Int) = Unit
            override fun onServiceFound(info: NsdServiceInfo) {
                nsd.resolveService(info, object : NsdManager.ResolveListener {
                    override fun onResolveFailed(info: NsdServiceInfo, code: Int) = Unit
                    override fun onServiceResolved(resolved: NsdServiceInfo) {
                        val address = resolved.host?.hostAddress ?: return
                        if (resolved.attributes["protocol"]?.toString(Charsets.UTF_8) != "4") return
                        val id = resolved.attributes["id"]?.toString(Charsets.UTF_8) ?: return
                        if (!id.matches(Regex("[0-9a-f]{64}"))) return
                        scope.launch {
                            val device = HandoverDevice(resolved.serviceName, address, resolved.port, id)
                            found[id] = device; devices.value = found.values.toList()
                            if (reconnectEnabled && !isolated && trust.contains(id) && preferences.getString("tvId", null) == id)
                                connect(device, pair = false)
                        }
                    }
                })
            }
            override fun onServiceLost(info: NsdServiceInfo) {
                // Keep identity-address hints for reconnect; trust is independent of NSD lifetime.
                scope.launch { devices.value = devices.value.filterNot { it.name == info.serviceName } }
            }
        }.also { nsd.discoverServices(HANDOVER_SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, it) }
    }
    /** A user selection transfers playback; background reconnect only attaches to current state. */
    fun select(device: HandoverDevice) {
        if (!available()) return
        if (connected.value && target?.id == device.id) { transferToTv(); return }
        disconnect(); error.value = null; pendingDeviceId.value = device.id; reconnectEnabled = true
        preferences.edit().putBoolean("reconnect", true).putString("tvId", device.id).apply()
        connect(device, pair = false, startPlayback = true)
    }
    fun transferToTv() {
        if (!available()) return
        val state = playback.value ?: return
        if (state.rendererId == hostId) return
        error.value = null; pendingDeviceId.value = hostId
        clientPeer?.send(JSONObject().put("type", "transfer").put("target", hostId)
            .put("sessionId", state.sessionId).put("epoch", state.epoch))
    }
    fun choosePhone() {
        if (!available()) return
        val state = playback.value ?: return
        if (state.rendererId == localId) return
        if (!connected.value) {
            val checkpoint = offlineCheckpoint ?: state
            disconnect()
            error.value = null
            applyingState = true
            try { service.applyHandoverState(checkpoint, renderLocally = true) }
            finally { applyingState = false }
            return
        }
        error.value = null; pendingDeviceId.value = localId
        clientPeer?.send(JSONObject().put("type", "transfer").put("target", localId)
            .put("sessionId", state.sessionId).put("epoch", state.epoch))
    }
    fun disconnect() {
        pendingDeviceId.value = null; transferStatus.value = null
        offlineCheckpoint = null
        reconnectEnabled = false
        preferences.edit().putBoolean("reconnect", false).apply()
        clientPeer?.close(); clientPeer = null
        clientJob?.cancel(); clientJob = null
        connected.value = false
        preparedJob?.cancel(); preparedJob = null
        if (preparedId != null) abortPreparation(preparedId!!)
        // Never resume a remote mirror merely because a connection disappears.
        if (!isTv && selected.value != null) {
            applyingState = true
            try { service.player.pause() } finally { applyingState = false }
        }
        if (!isTv) {
            releasedTransferId = null; releasedEpoch = -1
            coordinator.value = null; selected.value = null; playback.value = null
            service.sessionPlayer.notifyDeviceVolumeChanged()
        }
    }

    private fun connect(device: HandoverDevice, pair: Boolean, startPlayback: Boolean = false) {
        if (!available() || clientJob?.isActive == true) return
        target = device
        clientJob = scope.launch(Dispatchers.IO) {
            var attempt = 0
            var needsPairing = pair
            var requestPlayback = startPlayback
            while (isActive && reconnectEnabled && !isolated && !closing) {
                var peer: Peer? = null
                try {
                    val address = withContext(Dispatchers.Main) { found[device.id] ?: device }
                    val socket = transport.connect(address.host, address.port, device.id, needsPairing)
                    val channel = try { transport.authorize(socket, false) } catch (e: Exception) { socket.close(); throw e }
                    check(channel.id == device.id)
                    needsPairing = false
                    val connection = Peer(channel); peer = connection
                    val writer = launch { connection.writeLoop() }
                    val libraryJob = launch {
                        while (isActive) {
                            if (connected.value) runCatching { connection.library.publish { connection.send(it) } }
                                .onFailure { service.localLibrarySync.status.value = com.nikhil.yt.localsync.LibrarySyncStatus.Retrying }
                            delay(15_000)
                        }
                    }
                    var heartbeat: Job? = null
                    try {
                        val hello = withContext(Dispatchers.Main) {
                            service.awaitStartupRestore()
                            clientPeer = connection; rendererSequence = 0
                            JSONObject().put("type", "connect").put("state", capture().toSessionJson())
                                .put("startPlayback", requestPlayback).put("mediaVersion", 1).put("assetVersion", 1)
                        }
                        connection.send(hello)
                        heartbeat = launch { while (isActive) { delay(5000); connection.send(JSONObject().put("type", "ping")) } }
                        // A lost reply must not replay the selection and overwrite the TV session.
                        requestPlayback = false
                        while (isActive) {
                            val message = channel.read() ?: break
                            check(trust.contains(channel.id))
                            withContext(Dispatchers.Main) { handleClientMessage(connection, message) }
                            attempt = 0
                        }
                    } finally { heartbeat?.cancel(); writer.cancel(); libraryJob.cancel() }
                } catch (e: CancellationException) { throw e }
                catch (e: Exception) { if (!closing) error.value = e.message ?: "TV is unavailable" }
                finally {
                    peer?.close()
                    withContext(NonCancellable + Dispatchers.Main) {
                        if (clientPeer === peer) {
                            clientPeer = null; connected.value = false
                            // Cancel preparation and release locks, but never resume an uncertain source.
                            preparedJob?.cancel()
                            preparedId?.let { abortPreparation(it) }
                            if (releasedTransferId != null) {
                                applyingState = true
                                try { service.player.pause() } finally { applyingState = false }
                                releasedTransferId = null; releasedEpoch = -1
                            }
                            if (selected.value != null) {
                                val checkpoint = playback.value
                                if (offlineCheckpoint == null) offlineCheckpoint = checkpoint
                                checkpoint?.let { acceptState(it.offlineMirror()) }
                            }
                            pendingDeviceId.value = null; transferStatus.value = null
                            pendingCommands.values.forEach { it.completeExceptionally(IllegalStateException("Disconnected; command outcome may be unknown")) }
                            pendingCommands.clear()
                        }
                    }
                }
                if (needsPairing) break
                delay(ConnectPolicy.retryDelay(attempt++, kotlin.random.Random.nextLong(1000)))
            }
        }
    }
    private fun handleClientMessage(peer: Peer, message: JSONObject) {
        if (isolated || !trust.contains(peer.id) || clientPeer !== peer) return
        when (message.optString("type")) {
            "mediaRequest", "mediaReply", "mediaCancel" -> peer.media.receive(message)
            "state" -> {
                val state = message.getJSONObject("state").sessionState(playback.value)
                val old = playback.value
                require(message.getString("hostId") == peer.id)
                if (old == null || old.sessionId != state.sessionId || state.revision > old.revision || !connected.value) {
                    hostId = peer.id; coordinator.value = target; connected.value = true; error.value = null
                    offlineCheckpoint = null
                    if (preparedId != null && state.rendererId == localId) finishPreparation()
                    acceptState(state)
                    if (state.rendererId == pendingDeviceId.value) { pendingDeviceId.value = null; transferStatus.value = null }
                    if (releasedTransferId != null && (state.epoch > releasedEpoch || !state.playing)) {
                        releasedTransferId = null; releasedEpoch = -1
                    }
                }
            }
            "command" -> {
                val id = message.getString("commandId")
                val valid = validSession(message) && playback.value?.rendererId == localId
                if (valid && commandWindow.accept(id)) { execute(message.getJSONObject("command")); publishLocal() }
                peer.send(JSONObject().put("type", "ack").put("commandId", id).put("ok", valid))
            }
            "ack" -> pendingCommands.remove(message.getString("commandId"))?.complete(message)
            "prepare" -> prepareDestination(peer, message)
            "release" -> {
                if (validSession(message) && playback.value?.rendererId == localId) {
                    val snapshot = capture()
                    releasedTransferId = message.getString("transferId")
                    releasedEpoch = message.getLong("epoch")
                    applyingState = true
                    try { service.player.pause() } finally { applyingState = false }
                    peer.send(playback.value!!.releaseAcknowledgement(message.getString("transferId"), snapshot))
                }
            }
            "abort" -> abortPreparation(message.getString("transferId"))
            "library" -> peer.work.launch { runCatching { peer.library.receive(message) { peer.send(it) } }
                .onFailure { service.localLibrarySync.status.value = com.nikhil.yt.localsync.LibrarySyncStatus.Retrying } }
            "libraryAck" -> scope.launch { peer.library.acknowledge(message) }
            "transferProgress" -> {
                if (validSession(message)) {
                    transferStatus.value = message.getString("stage")
                    pendingDeviceId.value = message.getString("target")
                }
            }
            "transferFailed" -> {
                if (validSession(message)) {
                    if (message.optBoolean("pauseSource") && playback.value?.rendererId == localId) {
                        applyingState = true
                        try { service.player.pause() } finally { applyingState = false }
                    }
                    transferStatus.value = null; pendingDeviceId.value = null
                    error.value = service.getString(com.nikhil.yt.R.string.connect_transfer_failed)
                }
            }
            "pong" -> Unit
        }
    }
    private fun validSession(message: JSONObject): Boolean {
        val state = if (isTv) authority?.state else playback.value
        return state?.acceptsEnvelope(message) == true
    }
    private fun sessionEnvelope(type: String): JSONObject {
        val state = if (isTv) authority?.state else playback.value
        return JSONObject().put("type", type).put("sessionId", state?.sessionId).put("epoch", state?.epoch)
    }
    fun command(name: String, positionMs: Long? = null) {
        request(JSONObject().put("name", name).apply { if (positionMs != null) put("position", positionMs) })
    }
    fun request(command: JSONObject) {
        if (!available()) return
        if (isTv) dispatch(command)
        else {
            val peer = clientPeer
            if (peer == null || !connected.value) { error.value = "Connect device is offline"; return }
            val id = UUID.randomUUID().toString()
            val ack = CompletableDeferred<JSONObject>(); pendingCommands[id] = ack
            peer.send(sessionEnvelope("command").put("commandId", id).put("command", command))
            scope.launch {
                try { check(withTimeout(8000) { ack.await() }.getBoolean("ok")) { "Command was rejected" } }
                catch (e: Exception) { error.value = "Command not confirmed; refresh device state before retrying" }
                finally { pendingCommands.remove(id) }
            }
        }
    }
    fun sendQueue(status: Queue.Status, playing: Boolean) {
        request(JSONObject().put("name", "replace").put("queue", JSONArray(status.items.mapNotNull { it.metadata }.map { it.toHandoverJson() }))
            .put("index", status.mediaItemIndex).put("position", status.position).put("queueTitle", status.title).put("playing", playing))
    }
    fun addTracks(items: List<MediaItem>, next: Boolean) {
        request(JSONObject().put("name", if (next) "playNext" else "append")
            .put("queue", JSONArray(items.mapNotNull { it.metadata }.map { it.toHandoverJson() })))
    }
    private fun dispatch(command: JSONObject) {
        if (transferJob?.isActive == true) { error.value = "Wait for playback transfer"; return }
        val owner = authority
        if (owner == null || owner.state.rendererId == localId) { execute(command); publishLocal() }
        else {
            val peer = peers[owner.state.rendererId]
            if (peer == null && command.optString("name") in setOf("resume", "replace", "next", "previous")) {
                // A remote-control Play is an explicit request to use this TV after the phone vanished.
                val checkpoint = owner.claimOfflinePlayback(localId)
                persistAuthority(true)
                acceptState(checkpoint)
                error.value = null
                execute(command); publishLocal(); broadcast()
            } else if (peer == null) error.value = service.getString(com.nikhil.yt.R.string.connect_device_offline)
            else peer.send(sessionEnvelope("command").put("commandId", UUID.randomUUID().toString()).put("command", command))
        }
    }
    private fun execute(command: JSONObject) {
        val player = service.player
        fun tracks(): List<MediaItem> {
            val array = command.getJSONArray("queue")
            return (0 until array.length()).map { array.getJSONObject(it).handoverSong().toMediaItem() }
        }
        when (command.getString("name")) {
            "deviceVolume" -> {
                if (!isTv || authority?.state?.rendererId != localId) return
                val volume = currentDeviceVolume()
                if (!volume.adjustable) return
                val stream = android.media.AudioManager.STREAM_MUSIC
                val flags = android.media.AudioManager.FLAG_SHOW_UI
                when (command.optString("operation")) {
                    "adjust" -> command.optInt("value").takeIf { it == -1 || it == 1 }?.let {
                        audio.adjustStreamVolume(stream, it, flags)
                    }
                    "set" -> audio.setStreamVolume(stream, volume.clamp(command.getInt("value")), flags)
                    "mute" -> audio.adjustStreamVolume(stream,
                        if (command.getBoolean("muted")) android.media.AudioManager.ADJUST_MUTE else android.media.AudioManager.ADJUST_UNMUTE, flags)
                }
            }
            "pause" -> player.pause()
            "resume" -> { player.prepare(); player.play() }
            "next" -> { player.seekToNext(); player.prepare(); player.play() }
            "previous" -> { player.seekToPrevious(); player.prepare(); player.play() }
            "seek" -> {
                val index = command.optInt("index", player.currentMediaItemIndex)
                if (index in 0 until player.mediaItemCount) player.seekTo(index, command.optLong("position").coerceAtLeast(0))
            }
            "replace" -> {
                val items = tracks().mapNotNull { it.metadata }
                val state = capture().copy(queue = items, index = command.optInt("index").coerceIn(0, (items.size - 1).coerceAtLeast(0)),
                    positionMs = command.optLong("position"), playing = command.optBoolean("playing", true),
                    queueTitle = command.optString("queueTitle").takeIf { it.isNotBlank() && it != "null" },
                    order = items.indices.toList(), shuffle = false)
                applyingState = true
                try { service.applyHandoverState(state, renderLocally = true) } finally { applyingState = false }
            }
            "append" -> player.addMediaItems(tracks())
            "playNext" -> player.addMediaItems((player.currentMediaItemIndex + 1).coerceAtLeast(0), tracks())
            "insert" -> player.addMediaItems(command.getInt("index").coerceIn(0, player.mediaItemCount), tracks())
            "remove" -> {
                val from = command.getInt("from").coerceIn(0, player.mediaItemCount)
                val to = command.getInt("to").coerceIn(from, player.mediaItemCount)
                player.removeMediaItems(from, to)
            }
            "move" -> if (player.mediaItemCount > 0) player.moveMediaItem(
                command.getInt("from").coerceIn(0, player.mediaItemCount - 1), command.getInt("to").coerceIn(0, player.mediaItemCount - 1))
            "moveRange" -> {
                val from = command.getInt("from").coerceIn(0, player.mediaItemCount)
                val to = command.getInt("to").coerceIn(from, player.mediaItemCount)
                player.moveMediaItems(from, to, command.getInt("index").coerceIn(0, player.mediaItemCount - (to - from)))
            }
            "replaceItems" -> {
                val from = command.getInt("from").coerceIn(0, player.mediaItemCount)
                val to = command.getInt("to").coerceIn(from, player.mediaItemCount)
                player.replaceMediaItems(from, to, tracks())
            }
            "shuffle" -> player.shuffleModeEnabled = command.getBoolean("enabled")
            "repeat" -> player.repeatMode = command.getInt("mode").coerceIn(0, 2)
            "order" -> {
                val order = command.getJSONArray("order"); val values = (0 until order.length()).map { order.getInt(it) }
                if (values.size == player.mediaItemCount && values.toSet() == (0 until player.mediaItemCount).toSet())
                    player.setShuffleOrder(ShuffleOrder.DefaultShuffleOrder(values.toIntArray(), System.currentTimeMillis()))
            }
            "speed" -> player.setPlaybackSpeed(command.getDouble("speed").toFloat())
            "stop" -> { player.pause(); player.stop() }
            "clear" -> service.stopAndClearPlayback()
        }
    }

    private suspend fun buffer(state: HandoverPlayback) {
        check(state.queue.isNotEmpty()) { "There is no queue to transfer" }
        applyingState = true
        try { service.applyHandoverState(state.copy(playing = false), renderLocally = true) }
        finally { applyingState = false }
        withTimeout(20_000) {
            while (service.player.playbackState != Player.STATE_READY) {
                service.player.playerError?.let { throw it }
                delay(100)
            }
        }
    }
    private fun prepareDestination(peer: Peer, message: JSONObject) {
        if (!validSession(message) || preparedId != null) return
        val id = message.getString("transferId")
        val state = message.getJSONObject("state").sessionState()
        preparedId = id; preparedBackup = capture()
        preparedJob = scope.launch {
            try {
                buffer(state)
                peer.send(sessionEnvelope("ready").put("transferId", id))
                delay(30_000)
                if (preparedId == id) abortPreparation(id)
            } catch (e: Exception) {
                if (e !is CancellationException) peer.send(sessionEnvelope("failed").put("transferId", id))
                if (preparedId == id) abortPreparation(id)
            }
        }
    }
    private fun finishPreparation() {
        preparedId = null; preparedBackup = null
        preparedJob?.cancel(); preparedJob = null
    }
    private fun abortPreparation(id: String) {
        if (preparedId != id) return
        val backup = preparedBackup
        finishPreparation()
        if (backup != null) {
            applyingState = true
            try { service.applyHandoverState(backup.copy(playing = false), renderLocally = true) }
            finally { applyingState = false }
        }
    }
    private fun transfer(targetId: String) {
        val owner = authority ?: return
        if (transferJob?.isActive == true || targetId == owner.state.rendererId ||
            (targetId != localId && targetId !in peers)) return
        transferJob = scope.launch {
            val id = UUID.randomUUID().toString()
            val sourceId = owner.state.rendererId
            val transaction = ConnectTransfer(sourceId, targetId, owner.state.epoch)
            val destination = peers[targetId]
            var released = false
            val before = if (sourceId == localId) capture().copy(sessionId = owner.state.sessionId, epoch = owner.state.epoch)
                else owner.state.copy(positionMs = currentPosition())
            error.value = null
            transferStatus.value = "Preparing destination"
            peers.values.forEach { it.send(sessionEnvelope("transferProgress").put("target", targetId).put("stage", "preparing")) }
            try {
                if (targetId == localId) { preparedId = id; preparedBackup = capture(); buffer(before) }
                else {
                    val ready = CompletableDeferred<JSONObject>(); responses["ready:$id"] = ready
                    destination!!.send(sessionEnvelope("prepare").put("transferId", id).put("state", before.toSessionJson()))
                    check(withTimeout(25_000) { ready.await() }.optString("type") == "ready") { "Destination could not prepare media" }
                }
                transaction.ready()
                transferStatus.value = "Releasing source"
                transaction.releasing()
                val finalState = if (sourceId == localId) {
                    val snapshot = capture()
                    applyingState = true
                    try { service.player.pause() } finally { applyingState = false }
                    released = true
                    snapshot
                } else {
                    val release = CompletableDeferred<JSONObject>(); responses["released:$id"] = release
                    val source = peers[sourceId] ?: error("Source is offline")
                    source.send(sessionEnvelope("release").put("transferId", id))
                    // Treat an ambiguous release conservatively; no auto-resume on timeout.
                    released = true
                    withTimeout(8000) { release.await() }.getJSONObject("state").sessionState()
                }
                transaction.released()
                check(finalState.queue == before.queue && finalState.index == before.index) { "Queue changed during preparation" }
                check(owner.state.sessionId == before.sessionId && owner.state.epoch == before.epoch)
                transaction.commit(owner.state.rendererId, owner.state.epoch)
                owner.update(sourceId, before.epoch, finalState.copy(sessionId = before.sessionId, epoch = before.epoch))
                owner.transfer(targetId)
                if (targetId == localId) owner.update(localId, owner.state.epoch,
                    owner.state.copy(deviceVolume = currentDeviceVolume()))
                persistAuthority(true)
                if (targetId == localId) finishPreparation()
                acceptState(owner.state); broadcast()
                transferStatus.value = null
            } catch (e: Exception) {
                android.util.Log.w("VeluneConnect", "Transfer failed at ${transaction.stage}", e)
                peers.values.forEach { it.send(sessionEnvelope("transferFailed").put("pauseSource", released)) }
                destination?.send(sessionEnvelope("abort").put("transferId", id))
                if (targetId == localId) abortPreparation(id)
                if (released) {
                    owner.update(sourceId, owner.state.epoch, owner.state.copy(playing = false))
                    acceptState(owner.state); broadcast(); persistAuthority(true)
                }
                error.value = "Transfer failed; ${if (released) "playback is paused; reconnect and resume" else "source playback is unchanged"}"
            } finally {
                responses.remove("ready:$id"); responses.remove("released:$id"); transferStatus.value = null
            }
        }
    }

    private suspend fun handle(socket: SSLSocket) = withContext(Dispatchers.IO) {
        var peer: Peer? = null
        var writer: Job? = null
        var libraryJob: Job? = null
        try {
            val channel = transport.authorize(socket, true)
            val first = channel.read() ?: return@withContext
            require(first.optString("type") == "connect")
            val connection = Peer(channel); peer = connection
            connection.mediaVersion = first.optInt("mediaVersion")
            connection.assetVersion = first.optInt("assetVersion")
            writer = launch { connection.writeLoop() }
            withContext(Dispatchers.Main) {
                service.awaitStartupRestore()
                restoreAuthority()
                peers.put(channel.id, connection)?.close()
                if (isTv) {
                    libraryPeerId = channel.id
                    firstLibrarySync.value = if (!service.localLibrarySync.ownsConnectLibrary(channel.id))
                        mapOf(channel.id to channel.name) else emptyMap()
                }
                val requestedPlayback = first.optBoolean("startPlayback")
                val phoneState = first.getJSONObject("state").sessionState()
                if (requestedPlayback && phoneState.queue.isNotEmpty() && transferJob?.isActive != true) {
                    authority = HandoverSessionAuthority(phoneState.copy(sessionId = UUID.randomUUID().toString(),
                        rendererId = channel.id, epoch = 1, revision = 1, queueRevision = 1))
                    persistAuthority(true)
                }
                if (authority == null) {
                    val tv = capture()
                    val phone = first.getJSONObject("state").sessionState()
                    val initial = if (tv.queue.isNotEmpty()) tv else phone
                    authority = HandoverSessionAuthority(initial.copy(sessionId = UUID.randomUUID().toString(),
                        rendererId = if (tv.queue.isNotEmpty()) localId else channel.id, epoch = 1, revision = 1, queueRevision = 1))
                    persistAuthority(true)
                }
                // Attaching a trusted phone never replaces an existing authority or queue.
                connected.value = true
                connectedDevices.value = peers.values.map { ConnectTrustedDevice(it.id, it.channel.name) }
                acceptState(authority!!.state); broadcast()
                if (requestedPlayback && authority!!.state.queue.isNotEmpty()) transfer(localId)
            }
            libraryJob = launch {
                while (isActive) {
                    runCatching { connection.library.publish { connection.send(it) } }
                        .onFailure { service.localLibrarySync.status.value = com.nikhil.yt.localsync.LibrarySyncStatus.Retrying }
                    delay(15_000)
                }
            }
            while (isActive) {
                val message = channel.read() ?: break
                check(trust.contains(channel.id))
                withContext(Dispatchers.Main) {
                    if (peers[channel.id] !== connection || !trust.contains(channel.id)) return@withContext
                    val owner = authority ?: return@withContext
                    when (message.optString("type")) {
                        "mediaReply", "mediaCancel" -> connection.media.receive(message)
                        "rendererState" -> {
                            val sequence = message.getLong("rendererSequence")
                            val frame = message.getJSONObject("state").sessionState(owner.state)
                            if (transferJob?.isActive != true && sequence > connection.rendererSequence && owner.update(channel.id, frame.epoch, frame)) {
                                connection.rendererSequence = sequence
                                acceptState(owner.state); broadcast(); persistAuthority()
                            }
                        }
                        "command" -> {
                            val id = message.getString("commandId")
                            val valid = validSession(message) && transferJob?.isActive != true
                            if (valid && connection.commands.accept(id)) dispatch(message.getJSONObject("command"))
                            connection.send(JSONObject().put("type", "ack").put("commandId", id).put("ok", valid))
                        }
                        "transfer" -> if (validSession(message)) {
                            val targetId = message.getString("target")
                            if (targetId == localId || targetId == channel.id) transfer(targetId)
                        }
                        "ready", "failed", "released" -> {
                            val id = message.getString("transferId")
                            if (validSession(message)) responses["${if (message.optString("type") == "failed") "ready" else message.optString("type")}:$id"]?.complete(message)
                        }
                        "library" -> connection.work.launch { runCatching { connection.library.receive(message) { connection.send(it) } }
                            .onFailure { service.localLibrarySync.status.value = com.nikhil.yt.localsync.LibrarySyncStatus.Retrying } }
                        "libraryAck" -> scope.launch { connection.library.acknowledge(message) }
                        "ping" -> connection.send(JSONObject().put("type", "pong"))
                        "ack" -> Unit
                    }
                }
            }
        } catch (e: CancellationException) { throw e }
        catch (_: Exception) { /* Unauthenticated sockets never reach a playback or library handler. */ }
        finally {
            peer?.close(); socket.close(); writer?.cancel(); libraryJob?.cancel()
            withContext(NonCancellable + Dispatchers.Main) {
                val p = peer
                if (p != null && peers[p.id] === p) {
                    peers.remove(p.id)
                    firstLibrarySync.value = firstLibrarySync.value - p.id
                    connectedDevices.value = peers.values.map { ConnectTrustedDevice(it.id, it.channel.name) }
                    connected.value = peers.isNotEmpty()
                    // Losing a phone never pauses a TV renderer or discards its queue.
                    val owner = authority
                    if (owner?.deviceDisconnected(p.id) == true) {
                        acceptState(owner.state); broadcast(); persistAuthority(true)
                    }
                }
            }
        }
    }
    /** Task cleanup must not publish an empty local queue into the retained session. */
    fun prepareForTaskRemoval() {
        if (isTv) close()
        else {
            val reconnectOnReturn = reconnectEnabled
            disconnect()
            preferences.edit().putBoolean("reconnect", reconnectOnReturn).apply()
        }
    }
    fun close() {
        if (closing) return
        closing = true; transport.closePairing()
        if (isTv) ConnectAssetAccess.fetcher = null
        if (attached) service.player.removeListener(this)
        runCatching { persistAuthority(true) }
        peers.values.toList().forEach { it.close() }; clientPeer?.close()
        runCatching { discovery?.let(nsd::stopServiceDiscovery) }
        runCatching { registration?.let(nsd::unregisterService) }
        runCatching { server?.close() }; scope.cancel()
    }
}
