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
private data class Origin(val function: String, val key: String)
private val requestOrigin = createClientPlugin("VeluneDiagnosticOrigin") {
    onRequest { request, _ ->
        if (BandwidthLog.enabled) request.headers.append(ORIGIN_HEADER, BandwidthLog.caller("Ktor request"))
    }
}

fun HttpClientConfig<OkHttpConfig>.bandwidthDiagnostics(source: String, reason: String) {
    install(requestOrigin)
    engine { config { bandwidthDiagnostics(source, reason) } }
}

/**
 * The network interceptor runs only for the upstream, so an OkHttp cache hit never reaches it. The application
 * interceptor below sees the final response and records those hits as zero-byte rows instead.
 */
fun OkHttpClient.Builder.bandwidthDiagnostics(source: String, reason: String): OkHttpClient.Builder = apply {
    addInterceptor { chain ->
        val request = chain.request()
        val origin = request.header(ORIGIN_HEADER)
        if (!BandwidthLog.enabled && origin == null) return@addInterceptor chain.proceed(request)
        val clean = request.newBuilder().removeHeader(ORIGIN_HEADER)
        val logging = BandwidthLog.enabled
        val function = if (logging) origin ?: BandwidthLog.caller(source) else null
        // Only GET is hashed: for POST (InnerTube) the URL alone does not identify the resource.
        val key = if (logging && request.method == "GET") BandwidthLog.keyOf(request.url.toString()) else ""
        if (function != null) clean.tag(Origin::class.java, Origin(function, key))
        try {
            val response = chain.proceed(clean.build())
            if (function != null && response.networkResponse == null && response.cacheResponse != null) {
                BandwidthLog.cacheHit(source, function, reason, safeEndpoint(request.url.toString()), request.method, "HIT", key = key)
            }
            response
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
        val origin = request.tag(Origin::class.java)
        val transfer = BandwidthLog.begin(source, origin?.function ?: source, purpose, endpoint, request.method, origin?.key.orEmpty())
            ?: return chain.proceed(request)
        // OkHttp adds these validators itself when it holds a stale cached copy.
        val conditional = request.header("If-None-Match") != null || request.header("If-Modified-Since") != null
        transfer.cache(if (conditional) "REVALIDATE" else "NET")
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
