package com.nikhil.yt.handover

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import android.os.SystemClock
import androidx.media3.common.C
import androidx.media3.common.MediaItem
import androidx.media3.common.Player
import androidx.media3.exoplayer.source.ShuffleOrder
import com.nikhil.yt.extensions.getQueueWindows
import com.nikhil.yt.extensions.mediaItems
import com.nikhil.yt.extensions.metadata
import com.nikhil.yt.extensions.toMediaItem
import com.nikhil.yt.models.MediaMetadata
import com.nikhil.yt.playback.MusicService
import com.nikhil.yt.playback.queues.Queue
import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import kotlinx.coroutines.flow.MutableStateFlow
import org.json.JSONArray
import org.json.JSONObject
import java.io.BufferedReader
import java.net.ServerSocket
import java.net.Socket
import java.util.UUID

/** A persistent LAN session. Exactly one renderer publishes state; all other players mirror it paused. */
class HandoverController(private val service: MusicService) : Player.Listener {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    private val preferences = service.getSharedPreferences("handover_session", Context.MODE_PRIVATE)
    val localId: String = preferences.getString("deviceId", null) ?: UUID.randomUUID().toString().also {
        preferences.edit().putString("deviceId", it).apply()
    }
    private val isTv = service.packageName.startsWith("com.nikhil.yt.tv")
    val devices = MutableStateFlow<List<HandoverDevice>>(emptyList())
    // On the phone this is null when the phone renders, although the LAN session remains connected.
    val selected = MutableStateFlow<HandoverDevice?>(null)
    val playback = MutableStateFlow<HandoverPlayback?>(null)
    val connected = MutableStateFlow(false)
    val error = MutableStateFlow<String?>(null)
    var applyingState = false
        private set
    private var receivedAt = 0L
    private var attached = false
    private var closing = false
    private var authority: HandoverSessionAuthority? = null
    private val nsd = service.getSystemService(Context.NSD_SERVICE) as NsdManager
    private val token = UUID.randomUUID().toString()
    private var server: ServerSocket? = null
    private var registration: NsdManager.RegistrationListener? = null
    private var discovery: NsdManager.DiscoveryListener? = null
    private val found = mutableMapOf<String, HandoverDevice>()
    private val peers = mutableMapOf<String, Peer>()
    private val peerInstances = mutableMapOf<String, String>()
    private val instanceId = UUID.randomUUID().toString()
    private var clientPeer: Peer? = null
    private var clientJob: Job? = null
    private var target: HandoverDevice? = null
    private var hostId = ""
    private var clientPublishedQueue: List<MediaMetadata>? = null
    private var clientPublishedOrder: List<Int>? = null
    private var lastPublishedAt = 0L

    private inner class Peer(val id: String, val socket: Socket) {
        val outgoing = Channel<JSONObject>(32)
        var sentQueueRevision = -1L
        var sentSessionId = ""
        fun send(message: JSONObject) {
            if (!outgoing.trySend(message).isSuccess) close()
        }
        fun close() {
            outgoing.close()
            runCatching { socket.close() }
        }
        suspend fun writeLoop() = withContext(Dispatchers.IO) {
            val writer = socket.getOutputStream().bufferedWriter()
            for (message in outgoing) { writer.write(message.toString()); writer.newLine(); writer.flush() }
        }
    }

    fun attach() {
        if (attached) return
        attached = true
        service.player.addListener(this)
        scope.launch {
            while (isActive) { publishLocal(); delay(500) }
        }
        if (!isTv && preferences.getBoolean("linked", false)) startDiscovery()
    }

    override fun onEvents(player: Player, events: Player.Events) {
        if (!applyingState && SystemClock.elapsedRealtime() - lastPublishedAt > 80) publishLocal()
    }

    fun currentPosition(): Long {
        val state = playback.value ?: return 0L
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
            player.playbackParameters.speed, rendererId = localId, playbackState = player.playbackState)
    }

    private fun publishLocal() {
        if (applyingState || closing) return
        lastPublishedAt = SystemClock.elapsedRealtime()
        if (isTv) {
            val owner = authority ?: return
            if (owner.state.rendererId != localId) return
            owner.update(localId, owner.state.epoch, capture())
            acceptState(owner.state)
            broadcast()
        } else {
            val peer = clientPeer ?: return
            val state = playback.value ?: return
            if (!connected.value) return
            if (state.rendererId != localId) { peer.send(JSONObject().put("type", "ping")); return }
            val snapshot = capture().copy(sessionId = state.sessionId, epoch = state.epoch)
            val full = clientPublishedQueue != snapshot.queue || clientPublishedOrder != snapshot.order
            peer.send(JSONObject().put("type", "rendererState").put("state", snapshot.toSessionJson(full)))
            if (full) { clientPublishedQueue = snapshot.queue; clientPublishedOrder = snapshot.order }
        }
    }

    private fun acceptState(state: HandoverPlayback) {
        val previous = playback.value
        val remote = state.rendererId != localId
        selected.value = if (!remote) null else if (isTv) HandoverDevice("Phone", "", 0, "") else target
        playback.value = state
        receivedAt = SystemClock.elapsedRealtime()
        val takeover = !remote && (previous == null || previous.rendererId != localId || previous.epoch != state.epoch)
        if (remote || takeover) {
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
            peer.sentQueueRevision = state.queueRevision
            peer.sentSessionId = state.sessionId
        }
    }

    fun startTvHost() {
        attach()
        if (server != null) return
        scope.launch(Dispatchers.IO) {
            try {
                val listener = ServerSocket(0); server = listener
                val info = NsdServiceInfo().apply {
                    serviceName = "Velune TV"; serviceType = HANDOVER_SERVICE_TYPE; port = listener.localPort
                    setAttribute("token", token); setAttribute("protocol", "2")
                }
                registration = object : NsdManager.RegistrationListener {
                    override fun onServiceRegistered(info: NsdServiceInfo) = Unit
                    override fun onRegistrationFailed(info: NsdServiceInfo, code: Int) { error.value = "TV discovery unavailable ($code)" }
                    override fun onServiceUnregistered(info: NsdServiceInfo) = Unit
                    override fun onUnregistrationFailed(info: NsdServiceInfo, code: Int) = Unit
                }.also { nsd.registerService(info, NsdManager.PROTOCOL_DNS_SD, it) }
                while (isActive) { val socket = listener.accept(); launch { handle(socket) } }
            } catch (e: Exception) { if (!closing) error.value = e.message ?: "TV connection failed" }
        }
    }

    fun startDiscovery() {
        if (discovery != null) return
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
                        val secret = resolved.attributes["token"]?.toString(Charsets.UTF_8) ?: return
                        scope.launch {
                            val device = HandoverDevice(resolved.serviceName, address, resolved.port, secret)
                            found[resolved.serviceName] = device; devices.value = found.values.toList()
                            if (preferences.getBoolean("linked", false) && clientJob?.isActive != true &&
                                preferences.getString("tvName", null) == device.name) connect(device, transfer = false)
                        }
                    }
                })
            }
            override fun onServiceLost(info: NsdServiceInfo) {
                scope.launch { found.remove(info.serviceName); devices.value = found.values.toList() }
            }
        }.also { nsd.discoverServices(HANDOVER_SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, it) }
    }

    fun select(device: HandoverDevice) {
        error.value = null
        if (clientPeer != null && connected.value && target?.name == device.name) {
            if (playback.value?.rendererId == hostId) return
            val state = capture(); applyingState = true
            try { service.player.pause() } finally { applyingState = false }
            clientPeer?.send(JSONObject().put("type", "transfer").put("target", hostId)
                .put("epoch", playback.value?.epoch).put("state", state.toSessionJson()))
        } else connect(device, transfer = true)
    }

    fun choosePhone() {
        if (playback.value?.rendererId == localId) return
        clientPeer?.send(JSONObject().put("type", "transfer").put("target", localId)
            .put("epoch", playback.value?.epoch))
    }

    private fun connect(device: HandoverDevice, transfer: Boolean) {
        if (clientJob?.isActive == true) return
        target = device
        clientJob = scope.launch(Dispatchers.IO) {
            var peer: Peer? = null
            try {
                val socket = Socket(); socket.connect(java.net.InetSocketAddress(device.host, device.port), 3000)
                socket.soTimeout = 12000; val connection = Peer(localId, socket); peer = connection
                val hello = withContext(Dispatchers.Main) {
                    clientPeer = peer; clientPublishedQueue = null; clientPublishedOrder = null
                    val snapshot = capture()
                    if (transfer) {
                        applyingState = true
                        try { service.player.pause() } finally { applyingState = false }
                    }
                    JSONObject().put("type", "connect").put("token", device.token).put("clientId", localId)
                        .put("instanceId", instanceId).put("wasRenderer", preferences.getBoolean("wasRenderer", true))
                        .put("sessionId", preferences.getString("sessionId", "")).put("transfer", transfer)
                        .put("state", snapshot.toSessionJson())
                }
                val writerJob = launch { connection.writeLoop() }
                connection.send(hello)
                val reader = socket.getInputStream().bufferedReader()
                try {
                    while (isActive) {
                        val message = readFrame(reader) ?: break
                        withContext(Dispatchers.Main) {
                            when (message.optString("type")) {
                                "state" -> {
                                    val state = message.getJSONObject("state").sessionState(playback.value)
                                    val old = playback.value
                                    if (old == null || old.sessionId != state.sessionId || state.revision >= old.revision) {
                                        hostId = message.getString("hostId")
                                        if (!connected.value || old?.sessionId != state.sessionId || old?.rendererId != state.rendererId) {
                                            preferences.edit().putBoolean("linked", true).putString("tvName", device.name)
                                                .putString("sessionId", state.sessionId).putBoolean("wasRenderer", state.rendererId == localId).apply()
                                        }
                                        connected.value = true; error.value = null
                                        acceptState(state)
                                    }
                                }
                                "command" -> {
                                    if (playback.value?.rendererId == localId && message.optLong("epoch") == playback.value?.epoch) {
                                        execute(message.getJSONObject("command")); publishLocal()
                                    }
                                }
                            }
                        }
                    }
                } finally { writerJob.cancel() }
            } catch (e: Exception) { if (!closing) error.value = e.message ?: "TV is unavailable" }
            finally {
                peer?.close()
                withContext(NonCancellable + Dispatchers.Main) {
                    if (clientPeer === peer) clientPeer = null
                    connected.value = false
                    if (selected.value != null) playback.value?.let { acceptState(it.copy(playing = false)) }
                }
                if (!closing && preferences.getBoolean("linked", false)) {
                    scope.launch { delay(2000); connect(found[device.name] ?: device, transfer = false) }
                }
            }
        }
    }

    fun command(name: String, positionMs: Long? = null) {
        request(JSONObject().put("name", name).apply { if (positionMs != null) put("position", positionMs) })
    }

    fun request(command: JSONObject) {
        if (isTv) dispatch(command)
        else clientPeer?.send(JSONObject().put("type", "command").put("epoch", playback.value?.epoch).put("command", command))
    }

    fun sendQueue(status: Queue.Status, playing: Boolean) {
        val tracks = status.items.mapNotNull { it.metadata }
        request(JSONObject().put("name", "replace").put("queue", JSONArray(tracks.map { it.toHandoverJson() }))
            .put("index", status.mediaItemIndex).put("position", status.position)
            .put("queueTitle", status.title).put("playing", playing))
    }

    fun addTracks(items: List<MediaItem>, next: Boolean) {
        request(JSONObject().put("name", if (next) "playNext" else "append")
            .put("queue", JSONArray(items.mapNotNull { it.metadata }.map { it.toHandoverJson() })))
    }

    private fun dispatch(command: JSONObject) {
        val owner = authority
        if (owner == null || owner.state.rendererId == localId) { execute(command); publishLocal() }
        else {
            val peer = peers[owner.state.rendererId]
            if (peer == null) error.value = "Playback device is offline"
            else peer.send(JSONObject().put("type", "command").put("epoch", owner.state.epoch).put("command", command))
        }
    }

    private fun execute(command: JSONObject) {
        val player = service.player
        fun tracks(): List<MediaItem> {
            val array = command.getJSONArray("queue")
            return (0 until array.length()).map { array.getJSONObject(it).handoverSong().toMediaItem() }
        }
        when (command.getString("name")) {
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

    private fun transfer(targetId: String, frame: HandoverPlayback? = null, fromId: String? = null) {
        val owner = authority ?: return
        if (targetId == owner.state.rendererId) return
        if (targetId != localId && targetId !in peers) return
        if (owner.state.rendererId == localId) owner.update(localId, owner.state.epoch, capture())
        else if (frame != null && fromId == owner.state.rendererId) owner.update(fromId, owner.state.epoch, frame)
        applyingState = true
        try { service.player.pause() } finally { applyingState = false }
        owner.transfer(targetId)
        acceptState(owner.state); broadcast()
    }

    private suspend fun handle(socket: Socket) = withContext(Dispatchers.IO) {
        socket.use {
            var peer: Peer? = null
            var writerJob: Job? = null
            try {
                socket.soTimeout = 12000
                val reader = socket.getInputStream().bufferedReader()
                val first = readFrame(reader) ?: return@use
                if (first.optString("token") != token) return@use
                if (first.optString("type") != "connect") {
                    val response = withContext(Dispatchers.Main) {
                        val cmd = first.optString("cmd", "state")
                        if (cmd == "transfer") transfer(if (first.optString("target") == "tv") localId else first.getString("target"))
                        else if (cmd != "state") dispatch(JSONObject().put("name", cmd).apply {
                            if (first.has("position")) put("position", first.getLong("position"))
                        })
                        (authority?.state ?: capture()).toSessionJson().put("ok", true)
                    }
                    socket.getOutputStream().bufferedWriter().apply { write(response.toString()); newLine(); flush() }
                    return@use
                }
                val id = first.getString("clientId"); require(id.isNotBlank())
                val connection = Peer(id, socket); peer = connection
                writerJob = launch { connection.writeLoop() }
                withContext(Dispatchers.Main) {
                    peers.put(id, connection)?.close()
                    val oldInstance = peerInstances.put(id, first.optString("instanceId"))
                    val incoming = first.getJSONObject("state").sessionState()
                    val sameSession = authority?.state?.sessionId == first.optString("sessionId")
                    if (authority == null) {
                        val phoneSource = incoming.queue.isNotEmpty() && (first.optBoolean("transfer") || first.optBoolean("wasRenderer", true))
                        val initial = if (phoneSource) incoming else capture()
                        authority = HandoverSessionAuthority(initial.copy(sessionId = UUID.randomUUID().toString(),
                            rendererId = if (!first.optBoolean("transfer") && phoneSource) id else localId,
                            epoch = 1, revision = 1, queueRevision = 1))
                    } else if (first.optBoolean("transfer") && !sameSession) {
                        val initial = if (incoming.queue.isNotEmpty()) incoming else capture()
                        authority = HandoverSessionAuthority(initial.copy(sessionId = UUID.randomUUID().toString(), rendererId = localId,
                            epoch = 1, revision = 1, queueRevision = 1))
                    } else if (authority?.state?.rendererId == id) {
                        if (oldInstance == first.optString("instanceId")) {
                            authority?.update(id, authority!!.state.epoch, incoming)
                        } else {
                            // A restarted app restores the coordinator's queue, not an older disk queue.
                            authority!!.transfer(id)
                        }
                    }
                    connected.value = true
                    // A new source session must be installed even if the renderer was already this TV.
                    if (playback.value?.sessionId != authority!!.state.sessionId) playback.value = null
                    acceptState(authority!!.state)
                    if (first.optBoolean("transfer")) transfer(localId, incoming, id)
                    broadcast()
                }
                while (isActive) {
                    val message = readFrame(reader) ?: break
                    withContext(Dispatchers.Main) {
                        val owner = authority ?: return@withContext
                        when (message.optString("type")) {
                            "rendererState" -> {
                                val frame = message.getJSONObject("state").sessionState(owner.state)
                                if (frame.sessionId == owner.state.sessionId && owner.update(id, frame.epoch, frame)) { acceptState(owner.state); broadcast() }
                            }
                            "command" -> if (message.optLong("epoch") == owner.state.epoch) dispatch(message.getJSONObject("command"))
                            "transfer" -> if (message.optLong("epoch") == owner.state.epoch) transfer(
                                message.getString("target"), message.optJSONObject("state")?.sessionState(owner.state), id)
                        }
                    }
                }
            } catch (_: Exception) { /* Closing a link leaves its last queue intact. */ }
            finally {
                peer?.close(); writerJob?.cancel()
                withContext(NonCancellable + Dispatchers.Main) {
                    val p = peer
                    if (p != null && peers[p.id] === p) {
                        peers.remove(p.id)
                        if (authority?.state?.rendererId == p.id) {
                            val current = authority!!.state
                            authority!!.update(p.id, current.epoch, current.copy(playing = false))
                            acceptState(authority!!.state)
                        }
                        connected.value = peers.isNotEmpty()
                    }
                }
            }
        }
    }

    private fun readFrame(reader: BufferedReader): JSONObject? {
        val text = StringBuilder()
        while (true) {
            val next = reader.read()
            if (next < 0) return null
            if (next == 10) break
            require(text.length < 8 * 1024 * 1024)
            text.append(next.toChar())
        }
        return JSONObject(text.toString())
    }

    fun close() {
        closing = true
        if (attached) service.player.removeListener(this)
        peers.values.toList().forEach { it.close() }; clientPeer?.close()
        runCatching { discovery?.let(nsd::stopServiceDiscovery) }
        runCatching { registration?.let(nsd::unregisterService) }
        runCatching { server?.close() }
        scope.cancel()
    }
}
