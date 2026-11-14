package com.nikhil.yt.connect

import android.net.Uri
import androidx.media3.common.C
import androidx.media3.datasource.DataSource
import androidx.media3.datasource.DataSpec
import androidx.media3.datasource.TransferListener
import kotlinx.coroutines.*
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import org.json.JSONObject
import java.io.IOException
import java.util.Base64
import java.util.UUID
import java.util.concurrent.ConcurrentHashMap

/** Versioned, platform-neutral media RPC over the authenticated Connect channel. */
internal class ConnectMedia(
    private val scope: CoroutineScope,
    private val send: (JSONObject) -> Unit,
    private val available: suspend (String) -> Boolean,
    private val factory: () -> DataSource,
    private val assets: suspend (String, Set<String>) -> JSONObject = { _, _ -> JSONObject() },
) {
    private val pending = ConcurrentHashMap<String, CompletableDeferred<JSONObject>>()
    private val readers = mutableMapOf<String, DataSource>()
    private val lock = Mutex()
    @Volatile private var closed = false

    suspend fun request(message: JSONObject): JSONObject {
        if (closed) throw IOException("Media peer disconnected")
        val id = UUID.randomUUID().toString()
        val reply = CompletableDeferred<JSONObject>()
        pending[id] = reply
        try {
            send(message.put("type", "mediaRequest").put("requestId", id))
            return withTimeout(30_000) { reply.await() }.also {
                if (!it.optBoolean("ok")) throw IOException("Peer media unavailable")
            }
        } finally { pending.remove(id) }
    }

    fun receive(message: JSONObject) {
        if (message.optString("type") == "mediaReply") {
            pending[message.optString("requestId")]?.complete(message)
            return
        }
        scope.launch(Dispatchers.IO) {
            val response = JSONObject().put("type", "mediaReply").put("requestId", message.optString("requestId"))
            try {
                lock.withLock {
                    check(!closed)
                    when (message.getString("operation")) {
                        "assets" -> {
                            val song = message.getString("song")
                            require(song.matches(Regex("[A-Za-z0-9_-]{1,256}")))
                            val requested = message.optJSONArray("fields")
                            val fields = if (requested == null) setOf("metadata", "lyrics", "cover") else
                                (0 until requested.length()).map { requested.getString(it) }.toSet().intersect(setOf("metadata", "lyrics", "cover"))
                            response.put("assets", assets(song, fields))
                        }
                        "open" -> {
                            val song = message.getString("song")
                            require(song.isNotBlank() && song.length <= 256)
                            require(readers.size < 4)
                            if (message.optBoolean("cachedOnly")) check(available(song))
                            val offset = message.getLong("position")
                            val length = message.getLong("length")
                            require(offset >= 0 && (length == -1L || length > 0))
                            val source = factory()
                            try {
                                val size = source.open(DataSpec.Builder().setUri(Uri.parse(song)).setKey(song)
                                    .setPosition(offset).setLength(length).build())
                                val handle = UUID.randomUUID().toString()
                                readers[handle] = source
                                response.put("handle", handle).put("length", size)
                                timber.log.Timber.tag("ConnectMedia").i("Serving %s at %d (cachedOnly=%s)", song, offset, message.optBoolean("cachedOnly"))
                            } catch (e: Exception) { source.close(); throw e }
                        }
                        "read" -> {
                            val source = readers[message.getString("handle")] ?: error("Unknown media handle")
                            val bytes = ByteArray(BLOCK_SIZE)
                            val count = source.read(bytes, 0, bytes.size)
                            response.put("eof", count == C.RESULT_END_OF_INPUT)
                            response.put("data", if (count > 0) Base64.getEncoder().encodeToString(bytes.copyOf(count)) else "")
                        }
                        "close" -> readers.remove(message.getString("handle"))?.close()
                        else -> error("Unknown media operation")
                    }
                    response.put("ok", true)
                }
            } catch (e: CancellationException) { throw e }
            catch (_: Exception) { response.put("ok", false) }
            send(response)
        }
    }

    fun close() {
        closed = true
        pending.values.forEach { it.completeExceptionally(IOException("Media peer disconnected")) }
        pending.clear()
        // Cleanup survives cancellation of the peer's work scope.
        cleanup.launch { lock.withLock { readers.values.forEach { runCatching { it.close() } }; readers.clear() } }
    }

    fun source(spec: DataSpec, cachedOnly: Boolean): DataSource = object : DataSource {
        private var handle: String? = null
        private var buffer = ByteArray(0)
        private var cursor = 0
        override fun open(dataSpec: DataSpec): Long = blocking {
            val reply = request(JSONObject().put("operation", "open").put("song", spec.key)
                .put("position", spec.position).put("length", spec.length).put("cachedOnly", cachedOnly))
            handle = reply.getString("handle")
            reply.getLong("length")
        }
        override fun read(target: ByteArray, offset: Int, length: Int): Int {
            if (length == 0) return 0
            if (cursor == buffer.size) {
                val reply = blocking { request(JSONObject().put("operation", "read").put("handle", handle)) }
                if (reply.getBoolean("eof")) return C.RESULT_END_OF_INPUT
                buffer = Base64.getDecoder().decode(reply.getString("data"))
                require(buffer.size <= BLOCK_SIZE)
                cursor = 0
            }
            val count = minOf(length, buffer.size - cursor)
            buffer.copyInto(target, offset, cursor, cursor + count); cursor += count
            return count
        }
        override fun getUri(): Uri = spec.uri
        override fun addTransferListener(transferListener: TransferListener) = Unit
        override fun close() {
            handle?.let { send(JSONObject().put("type", "mediaRequest").put("requestId", UUID.randomUUID().toString())
                .put("operation", "close").put("handle", it)) }
            handle = null; buffer = ByteArray(0); cursor = 0
        }
    }

    companion object {
        const val BLOCK_SIZE = 64 * 1024
        private val cleanup = CoroutineScope(SupervisorJob() + Dispatchers.IO)
        fun <T> blocking(block: suspend () -> T): T = try { runBlocking(Dispatchers.IO) { block() } }
            catch (e: Exception) { throw IOException("Connect media transfer failed", e) }
    }
}

/** Select once per open: never splice different encodings after a read failure. */
internal class PeerMediaDataSource(
    private val local: DataSource,
    private val candidates: (DataSpec) -> List<DataSource>,
) : DataSource {
    private var active: DataSource? = null
    override fun open(dataSpec: DataSpec): Long {
        for (source in candidates(dataSpec)) {
            try {
                val length = source.open(dataSpec)
                active = source
                timber.log.Timber.tag("ConnectMedia").i("Playing %s from connected device", dataSpec.key)
                return length
            }
            catch (_: IOException) { runCatching { source.close() } }
        }
        active = local
        return local.open(dataSpec)
    }
    override fun read(buffer: ByteArray, offset: Int, length: Int) = checkNotNull(active).read(buffer, offset, length)
    override fun getUri(): Uri? = active?.uri
    override fun getResponseHeaders(): Map<String, List<String>> = active?.responseHeaders ?: emptyMap()
    override fun addTransferListener(transferListener: TransferListener) = local.addTransferListener(transferListener)
    override fun close() { try { active?.close() } finally { active = null } }
}
