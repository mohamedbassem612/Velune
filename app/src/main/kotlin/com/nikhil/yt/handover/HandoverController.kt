package com.nikhil.yt.handover

import android.content.Context
import android.net.nsd.NsdManager
import android.net.nsd.NsdServiceInfo
import androidx.media3.common.C
import com.nikhil.yt.extensions.currentMetadata
import com.nikhil.yt.playback.MusicService
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.cancel
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.isActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import org.json.JSONObject
import java.net.ServerSocket
import java.net.Socket
import java.util.UUID

/** Local playback target. It deliberately has no dependency on Music Together. */
class HandoverController(private val service: MusicService) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main)
    val devices = MutableStateFlow<List<HandoverDevice>>(emptyList())
    val selected = MutableStateFlow<HandoverDevice?>(null)
    val playback = MutableStateFlow<HandoverPlayback?>(null)
    val error = MutableStateFlow<String?>(null)

    private val nsd = service.getSystemService(Context.NSD_SERVICE) as NsdManager
    private val token = UUID.randomUUID().toString()
    private var server: ServerSocket? = null
    private var serverJob: Job? = null
    private var pollJob: Job? = null
    private var registration: NsdManager.RegistrationListener? = null
    private var discovery: NsdManager.DiscoveryListener? = null
    private val found = mutableMapOf<String, HandoverDevice>()

    fun startTvHost() {
        if (server != null) return
        serverJob = scope.launch(Dispatchers.IO) {
            try {
                val listener = ServerSocket(0)
                server = listener
                val info = NsdServiceInfo().apply {
                    serviceName = "Velune TV"
                    serviceType = HANDOVER_SERVICE_TYPE
                    port = listener.localPort
                    setAttribute("token", token)
                }
                registration = object : NsdManager.RegistrationListener {
                    override fun onServiceRegistered(info: NsdServiceInfo) = Unit
                    override fun onRegistrationFailed(info: NsdServiceInfo, errorCode: Int) {
                        error.value = "TV discovery unavailable ($errorCode)"
                    }
                    override fun onServiceUnregistered(info: NsdServiceInfo) = Unit
                    override fun onUnregistrationFailed(info: NsdServiceInfo, errorCode: Int) = Unit
                }.also { nsd.registerService(info, NsdManager.PROTOCOL_DNS_SD, it) }
                while (isActive) {
                    val socket = listener.accept()
                    launch { handle(socket) }
                }
            } catch (e: Exception) {
                if (isActive) error.value = e.message ?: "TV connection failed"
            }
        }
    }

    fun startDiscovery() {
        if (discovery != null) return
        discovery = object : NsdManager.DiscoveryListener {
            override fun onDiscoveryStarted(type: String) = Unit
            override fun onDiscoveryStopped(type: String) = Unit
            override fun onStartDiscoveryFailed(type: String, code: Int) { error.value = "Discovery unavailable ($code)" }
            override fun onStopDiscoveryFailed(type: String, code: Int) = Unit
            override fun onServiceFound(info: NsdServiceInfo) {
                nsd.resolveService(info, object : NsdManager.ResolveListener {
                    override fun onResolveFailed(info: NsdServiceInfo, code: Int) = Unit
                    override fun onServiceResolved(resolved: NsdServiceInfo) {
                        val address = resolved.host?.hostAddress ?: return
                        val secret = resolved.attributes["token"]?.toString(Charsets.UTF_8) ?: return
                        synchronized(found) {
                            found[resolved.serviceName] = HandoverDevice(resolved.serviceName, address, resolved.port, secret)
                            devices.value = found.values.toList()
                        }
                    }
                })
            }
            override fun onServiceLost(info: NsdServiceInfo) {
                synchronized(found) {
                    found.remove(info.serviceName)
                    devices.value = found.values.toList()
                }
            }
        }.also { nsd.discoverServices(HANDOVER_SERVICE_TYPE, NsdManager.PROTOCOL_DNS_SD, it) }
    }

    fun select(device: HandoverDevice) {
        scope.launch {
            error.value = null
            val song = if (selected.value != null) playback.value?.song else service.player.currentMetadata
            val request = JSONObject().put("cmd", if (song == null) "state" else "play")
            if (song != null) {
                request.put("song", song.toHandoverJson())
                request.put("position", if (selected.value != null) playback.value?.positionMs ?: 0L else service.player.currentPosition.coerceAtLeast(0))
                request.put("playing", if (selected.value != null) playback.value?.playing == true else service.player.playWhenReady)
            }
            val result = send(device, request)
            if (result == null) return@launch
            selected.value?.takeIf { it != device }?.let { oldDevice ->
                send(oldDevice, JSONObject().put("cmd", "pause"))
            }
            service.player.pause()
            selected.value = device
            applyState(result)
            pollJob?.cancel()
            pollJob = scope.launch {
                while (isActive && selected.value == device) {
                    delay(1200)
                    val state = send(device, JSONObject().put("cmd", "state"))
                    if (state == null) {
                        selected.value = null
                        playback.value = null
                        break
                    }
                    applyState(state)
                }
            }
        }
    }

    fun choosePhone() {
        scope.launch {
            val device = selected.value
            val wasPlaying = playback.value?.playing == true
            val remote = if (device != null) {
                val paused = send(device, JSONObject().put("cmd", "pause")) ?: return@launch
                applyState(paused)
                playback.value
            } else null
            pollJob?.cancel()
            selected.value = null
            playback.value = null
            remote?.song?.let { song ->
                service.playQueue(HandoverQueue(song, remote.positionMs), wasPlaying)
            }
        }
    }

    fun command(name: String, positionMs: Long? = null) {
        val device = selected.value ?: return
        scope.launch {
            val request = JSONObject().put("cmd", name)
            if (positionMs != null) request.put("position", positionMs)
            send(device, request)?.let(::applyState)
        }
    }

    fun sendTrack(song: com.nikhil.yt.models.MediaMetadata, positionMs: Long = 0, playing: Boolean = true) {
        val device = selected.value ?: return
        scope.launch {
            val request = JSONObject().put("cmd", "play")
                .put("song", song.toHandoverJson()).put("position", positionMs).put("playing", playing)
            send(device, request)?.let(::applyState)
        }
    }

    private suspend fun send(device: HandoverDevice, request: JSONObject): JSONObject? = withContext(Dispatchers.IO) {
        try {
            request.put("token", device.token)
            Socket().use { socket ->
                socket.connect(java.net.InetSocketAddress(device.host, device.port), 1500)
                socket.soTimeout = 2000
                socket.getOutputStream().bufferedWriter().apply { write(request.toString()); newLine(); flush() }
                val reply = socket.getInputStream().bufferedReader().readLine() ?: throw IllegalStateException("No reply")
                JSONObject(reply).takeIf { it.optBoolean("ok") }?.also { error.value = null }
                    ?: throw IllegalStateException("TV refused command")
            }
        } catch (e: Exception) {
            error.value = e.message ?: "TV is unavailable"
            null
        }
    }

    private suspend fun handle(socket: Socket) = withContext(Dispatchers.IO) {
        socket.use {
            try {
                it.soTimeout = 2500
                val line = it.getInputStream().bufferedReader().readLine() ?: return@use
                if (line.length > 16384) return@use
                val request = JSONObject(line)
                if (request.optString("token") != token) return@use
                val result = withContext(Dispatchers.Main) {
                    val player = service.player
                    when (request.optString("cmd")) {
                        "play" -> {
                            val song = request.getJSONObject("song").handoverSong()
                            require(song.id.isNotBlank() && song.id.length <= 256)
                            service.playQueue(
                                HandoverQueue(song, request.optLong("position", 0)),
                                request.optBoolean("playing", true),
                            )
                        }
                        "pause" -> player.pause()
                        "resume" -> player.play()
                        "next" -> service.player.seekToNext()
                        "previous" -> service.player.seekToPrevious()
                        "seek" -> player.seekTo(request.optLong("position", 0).coerceAtLeast(0))
                        "state" -> Unit
                        else -> throw IllegalArgumentException("Unknown command")
                    }
                    stateJson()
                }
                it.getOutputStream().bufferedWriter().apply { write(result.toString()); newLine(); flush() }
            } catch (_: Exception) {
                runCatching { it.getOutputStream().bufferedWriter().apply { write("{\"ok\":false}\n"); flush() } }
            }
        }
    }

    private fun stateJson() = JSONObject().apply {
        val player = service.player
        put("ok", true)
        put("song", player.currentMetadata?.toHandoverJson())
        put("position", player.currentPosition.takeIf { it != C.TIME_UNSET } ?: 0L)
        put("duration", player.duration.takeIf { it != C.TIME_UNSET } ?: 0L)
        put("playing", player.playWhenReady)
    }

    private fun applyState(reply: JSONObject) {
        playback.value = HandoverPlayback(
            reply.optJSONObject("song")?.handoverSong(),
            reply.optLong("position"), reply.optLong("duration"), reply.optBoolean("playing")
        )
    }

    fun close() {
        pollJob?.cancel()
        runCatching { discovery?.let(nsd::stopServiceDiscovery) }
        runCatching { registration?.let(nsd::unregisterService) }
        runCatching { server?.close() }
        serverJob?.cancel()
        scope.cancel()
    }
}
