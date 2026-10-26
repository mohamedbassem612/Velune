package com.nikhil.yt.diagnostics

import io.ktor.client.HttpClientConfig
import io.ktor.client.engine.okhttp.OkHttpConfig
import io.ktor.client.plugins.api.createClientPlugin
import io.ktor.websocket.Frame
import io.ktor.websocket.WebSocketSession
import okhttp3.Interceptor
import okhttp3.OkHttpClient
import okhttp3.RequestBody
import okhttp3.Response
import okhttp3.ResponseBody
import okio.Buffer
import okio.BufferedSink
import okio.BufferedSource
import okio.ForwardingSink
import okio.ForwardingSource
import okio.buffer
import java.io.IOException
import java.net.URI

private const val ORIGIN_HEADER = "X-Velune-Local-Diagnostic-Origin"
private data class Origin(val function: String)
private val requestOrigin = createClientPlugin("VeluneDiagnosticOrigin") {
    onRequest { request, _ ->
        if (BandwidthLog.enabled) request.headers.append(ORIGIN_HEADER, BandwidthLog.caller("Ktor request"))
    }
}

fun HttpClientConfig<OkHttpConfig>.bandwidthDiagnostics(source: String, reason: String) {
    install(requestOrigin)
    engine { config { bandwidthDiagnostics(source, reason) } }
}

/** Network interceptor runs only for the upstream: disk/memory cache hits never count. */
fun OkHttpClient.Builder.bandwidthDiagnostics(source: String, reason: String): OkHttpClient.Builder = apply {
    addInterceptor { chain ->
        val request = chain.request()
        val origin = request.header(ORIGIN_HEADER)
        if (!BandwidthLog.enabled && origin == null) return@addInterceptor chain.proceed(request)
        val clean = request.newBuilder().removeHeader(ORIGIN_HEADER)
        if (BandwidthLog.enabled) clean.tag(Origin::class.java, Origin(origin ?: BandwidthLog.caller(source)))
        try {
            chain.proceed(clean.build())
        } catch (error: IOException) {
            // Includes DNS/connect/TLS failures that never reach a network interceptor.
            BandwidthLog.begin(source, origin ?: source, reason, safeEndpoint(request.url.toString()), "CALL")
                ?.finish("failed: ${error.javaClass.simpleName}")
            throw error
        }
    }
    addNetworkInterceptor(BodyCounter(source, reason))
}

private class BodyCounter(private val source: String, private val reason: String) : Interceptor {
    override fun intercept(chain: Interceptor.Chain): Response {
        val request = chain.request()
        if (!BandwidthLog.enabled) return chain.proceed(request)
        val endpoint = safeEndpoint(request.url.toString())
        val operation = request.url.pathSegments.lastOrNull().orEmpty()
        val purpose = when (operation) {
            "search" -> "$reason: search results"
            "browse" -> "$reason: home, library, album, artist or playlist data"
            "next" -> "$reason: queue, recommendations or lyrics metadata"
            "player" -> "$reason: stream URL and playback metadata"
            "get_search_suggestions" -> "$reason: search suggestions"
            else -> reason
        }
        val transfer = BandwidthLog.begin(source, request.tag(Origin::class.java)?.function ?: source, purpose, endpoint, request.method)
            ?: return chain.proceed(request)
        val body = request.body
        val counted = if (body == null) request else request.newBuilder().method(request.method, object : RequestBody() {
            override fun contentType() = body.contentType()
            override fun contentLength() = body.contentLength()
            override fun isDuplex() = body.isDuplex()
            override fun isOneShot() = body.isOneShot()
            override fun writeTo(sink: BufferedSink) {
                val counter = object : ForwardingSink(sink) {
                    override fun write(source: Buffer, byteCount: Long) {
                        super.write(source, byteCount)
                        transfer.bytes(sent = byteCount)
                    }
                }.buffer()
                body.writeTo(counter)
                counter.flush()
            }
        }).build()
        try {
            val response = chain.proceed(counted)
            if (response.code == 101) {
                transfer.finish("HTTP 101; WebSocket payloads logged separately")
                return response
            }
            val responseBody = response.body
            val counter = object : ForwardingSource(responseBody.source()) {
                override fun read(sink: Buffer, byteCount: Long): Long = try {
                    super.read(sink, byteCount).also {
                        if (it > 0) transfer.bytes(received = it)
                        if (it == -1L) transfer.finish("HTTP ${response.code}")
                    }
                } catch (error: IOException) {
                    transfer.finish("failed: ${error.javaClass.simpleName}")
                    throw error
                }
                override fun close() {
                    try { super.close() } finally { transfer.finish("HTTP ${response.code}; closed") }
                }
            }.buffer()
            return response.newBuilder().body(object : ResponseBody() {
                override fun contentType() = responseBody.contentType()
                override fun contentLength() = responseBody.contentLength()
                override fun source(): BufferedSource = counter
            }).build()
        } catch (error: IOException) {
            transfer.finish("failed: ${error.javaClass.simpleName}")
            throw error
        }
    }
}

// Allow only fixed route words. Tokens, room IDs, song IDs and filenames become :id.
private val safeSegments = setOf("api", "v1", "v2", "v3", "v4", "youtubei", "search", "browse", "next", "player",
    "get_search_suggestions", "videoplayback", "releases", "latest", "repos", "commits", "image", "images", "lyrics",
    "get", "submit-listens", "validate-token", "download", "assets", "together", "gateway", "ws", "socket", "rooms")

fun safeEndpoint(url: String): String = runCatching {
    val uri = URI(url)
    val host = uri.host ?: return@runCatching "redacted-endpoint"
    val path = uri.path.orEmpty().split('/').filter { it.isNotEmpty() }
        .joinToString("/") { if (it in safeSegments) it else ":id" }
    "${uri.scheme}://$host${if (uri.port >= 0) ":${uri.port}" else ""}/$path"
}.getOrDefault("redacted-endpoint")

/** Payload sizes only: never retain the frame itself, presence tokens or room messages. */
fun Frame.recordBandwidth(source: String, function: String, incoming: Boolean): Frame = apply {
    BandwidthLog.begin(source, function, "Realtime messages / heartbeat (application payload only)",
        "WebSocket", if (incoming) "WS RECEIVE" else "WS SEND")?.let {
        it.bytes(received = if (incoming) data.size.toLong() else 0, sent = if (incoming) 0 else data.size.toLong())
        it.finish("payload")
    }
}

suspend fun WebSocketSession.sendWithBandwidth(frame: Frame, source: String, function: String) {
    // Successful enqueue, rather than a delivery acknowledgement.
    send(frame)
    frame.recordBandwidth(source, function, incoming = false)
}

/** An opaque library can be attributed, but its internal transfer size is unavailable. */
inline fun <T> observeBandwidth(source: String, function: String, reason: String, block: () -> T): T {
    val transfer = BandwidthLog.begin(source, function, reason, "External library", "OBSERVED CALL")
    try {
        return block().also { transfer?.finish("completed; bytes unavailable") }
    } catch (error: Throwable) {
        transfer?.finish("failed: ${error.javaClass.simpleName}; bytes unavailable")
        throw error
    }
}
