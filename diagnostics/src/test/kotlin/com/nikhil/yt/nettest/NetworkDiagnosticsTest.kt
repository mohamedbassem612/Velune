package com.nikhil.yt.nettest

import com.nikhil.yt.diagnostics.BandwidthLog
import com.nikhil.yt.diagnostics.bandwidthDiagnostics
import com.nikhil.yt.diagnostics.recordBandwidth
import com.nikhil.yt.diagnostics.safeEndpoint
import com.sun.net.httpserver.HttpServer
import io.ktor.client.HttpClient
import io.ktor.client.engine.okhttp.OkHttp
import io.ktor.client.request.get
import io.ktor.client.statement.bodyAsText
import io.ktor.websocket.Frame
import kotlinx.coroutines.runBlocking
import okhttp3.Cache
import okhttp3.OkHttpClient
import okhttp3.Request
import okhttp3.RequestBody.Companion.toRequestBody
import org.junit.After
import org.junit.Assert.*
import org.junit.Before
import org.junit.Test
import java.io.ByteArrayOutputStream
import java.net.InetSocketAddress
import java.nio.file.Files
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.zip.GZIPOutputStream

class NetworkDiagnosticsTest {
    private lateinit var server: HttpServer
    private lateinit var baseUrl: String
    private lateinit var client: OkHttpClient

    @Before fun setup() {
        BandwidthLog.rollback()
        server = HttpServer.create(InetSocketAddress("127.0.0.1", 0), 0)
        server.createContext("/api/search") { exchange ->
            assertNull(exchange.requestHeaders.getFirst("X-Velune-Local-Diagnostic-Origin"))
            exchange.requestBody.readBytes()
            val body = "a response with private content".toByteArray()
            exchange.responseHeaders.add("Cache-Control", "max-age=3600")
            exchange.sendResponseHeaders(200, body.size.toLong())
            exchange.responseBody.use { it.write(body) }
        }
        server.createContext("/redirect") { exchange ->
            exchange.responseHeaders.add("Location", "/api/search")
            exchange.sendResponseHeaders(302, -1)
            exchange.close()
        }
        server.createContext("/gzip") { exchange ->
            val bytes = ByteArrayOutputStream().also { out ->
                GZIPOutputStream(out).use { it.write("test".repeat(1000).toByteArray()) }
            }.toByteArray()
            exchange.responseHeaders.add("Content-Encoding", "gzip")
            exchange.sendResponseHeaders(200, bytes.size.toLong())
            exchange.responseBody.use { it.write(bytes) }
        }
        server.createContext("/failure") { exchange ->
            exchange.sendResponseHeaders(503, 3)
            exchange.responseBody.use { it.write("bad".toByteArray()) }
        }
        server.start()
        baseUrl = "http://127.0.0.1:${server.address.port}"
        client = OkHttpClient.Builder().bandwidthDiagnostics("TestClient", "Fetch test data").build()
    }

    @After fun cleanup() {
        server.stop(0)
        client.connectionPool.evictAll()
        client.dispatcher.executorService.shutdownNow()
        BandwidthLog.rollback()
    }

    @Test fun `disabled is a transparent passthrough and existing clients support toggling`() {
        val request = Request.Builder().url("$baseUrl/api/search").build()
        client.newCall(request).execute().use { assertEquals(31, it.body.bytes().size) }
        assertTrue(BandwidthLog.snapshot().entries.isEmpty())
        BandwidthLog.setEnabled(true)
        client.newCall(request).execute().use { it.body.bytes() }
        assertEquals(31, BandwidthLog.snapshot().received)
        BandwidthLog.setEnabled(false)
        client.newCall(request).execute().use { it.body.bytes() }
        assertEquals(31, BandwidthLog.snapshot().received)
    }

    @Test fun `records upload and response with metadata only`() {
        BandwidthLog.setEnabled(true)
        val request = Request.Builder().url("$baseUrl/api/search?q=private-query&token=secret")
            .header("Authorization", "Bearer private-token")
            .post("private-body".toRequestBody()).build()
        client.newCall(request).execute().use { assertEquals(31, it.body.bytes().size) }
        val snapshot = BandwidthLog.snapshot()
        assertEquals(31, snapshot.received)
        assertEquals(12, snapshot.sent)
        assertEquals(1, snapshot.entries.size)
        val entry = snapshot.entries.single()
        assertTrue(entry.status.startsWith("HTTP 200"))
        assertTrue(entry.function.contains("NetworkDiagnosticsTest"))
        assertTrue(entry.reason.contains("search results"))
        val exported = BandwidthLog.export()
        listOf("private-query", "secret", "private-token", "private-body", "private content").forEach {
            assertFalse("Export contains $it", exported.contains(it))
        }
    }

    @Test fun `redirects and compressed response count individual upstream body bytes`() {
        BandwidthLog.setEnabled(true)
        client.newCall(Request.Builder().url("$baseUrl/redirect").build()).execute().use { it.body.bytes() }
        assertEquals(2, BandwidthLog.snapshot().entries.size)
        assertEquals(31, BandwidthLog.snapshot().received)
        BandwidthLog.clear()
        client.newCall(Request.Builder().url("$baseUrl/gzip").build()).execute().use {
            assertEquals(4000, it.body.bytes().size)
        }
        assertTrue(BandwidthLog.snapshot().received in 1..100)
    }

    @Test fun `disk cache hits are logged as zero byte HIT rows`() {
        val directory = Files.createTempDirectory("bandwidth-cache").toFile()
        Cache(directory, 1024 * 1024).use { cache ->
            val cached = client.newBuilder().cache(cache).build()
            BandwidthLog.setEnabled(true)
            repeat(2) {
                cached.newCall(Request.Builder().url("$baseUrl/api/search").build()).execute().use { it.body.bytes() }
            }
            val entries = BandwidthLog.snapshot().entries
            assertEquals(2, entries.size)
            assertEquals("NET", entries[0].cache)
            assertEquals("HIT", entries[1].cache)
            assertEquals(0, entries[1].received)
            assertEquals(31, BandwidthLog.snapshot().received)
            assertTrue(entries[0].key.isNotEmpty())
            assertEquals(entries[0].key, entries[1].key)
        }
        directory.deleteRecursively()
    }

    @Test fun `same GET resource fetched twice from the network is reported as repeated`() {
        BandwidthLog.setEnabled(true)
        repeat(2) {
            client.newCall(Request.Builder().url("$baseUrl/api/search?x=1").build()).execute().use { it.body.bytes() }
        }
        val exported = BandwidthLog.export()
        assertTrue(exported.contains("repeatedNetworkKeys=1"))
        assertTrue(exported.contains("repeatedNetworkBytes=31"))
        assertFalse(exported.contains("x=1"))
    }

    @Test fun `rows are stamped with foreground or background state and notes mark transitions`() {
        BandwidthLog.setForeground(false)
        BandwidthLog.setEnabled(true)
        BandwidthLog.setForeground(true)
        BandwidthLog.begin("Test", "fg", "test", "redacted", "GET")!!.finish("HTTP 200")
        BandwidthLog.setForeground(false)
        BandwidthLog.begin("Test", "bg", "test", "redacted", "GET")!!.finish("HTTP 200")
        val entries = BandwidthLog.snapshot().entries
        val fg = entries.single { it.function == "fg" }
        val bg = entries.single { it.function == "bg" }
        assertEquals("FG", fg.appState)
        assertTrue(fg.sinceFgMs >= 0)
        assertEquals("BG", bg.appState)
        assertEquals(-1, bg.sinceFgMs)
        assertTrue(entries.count { it.method == "NOTE" && it.source == "App" } >= 2)
    }

    @Test fun `keys are stable within a process and counters are exported`() {
        assertEquals(BandwidthLog.keyOf("a"), BandwidthLog.keyOf("a"))
        assertNotEquals(BandwidthLog.keyOf("a"), BandwidthLog.keyOf("b"))
        assertEquals(8, BandwidthLog.keyOf("a").length)
        BandwidthLog.setEnabled(true)
        BandwidthLog.countCache("Coil.memory")
        BandwidthLog.countCache("Coil.memory")
        assertTrue(BandwidthLog.export().contains("Coil.memory=2"))
    }

    @Test fun `error responses retain bytes and HTTP status`() {
        BandwidthLog.setEnabled(true)
        client.newCall(Request.Builder().url("$baseUrl/failure").build()).execute().use { it.body.bytes() }
        assertEquals(3, BandwidthLog.snapshot().received)
        assertTrue(BandwidthLog.snapshot().entries.single().status.contains("503"))
    }

    @Test fun `Ktor preserves caller attribution and strips local header before transmission`() = runBlocking {
        BandwidthLog.setEnabled(true)
        val ktor = HttpClient(OkHttp) { bandwidthDiagnostics("KtorTest", "Test Ktor") }
        try {
            assertEquals(31, ktor.get("$baseUrl/api/search").bodyAsText().length)
            val entry = BandwidthLog.snapshot().entries.single()
            assertEquals("KtorTest", entry.source)
            assertTrue(entry.function.contains("NetworkDiagnosticsTest"))
            assertEquals(31, entry.received)
        } finally { ktor.close() }
    }

    @Test fun `rollback prevents late in-flight updates from resurrecting erased logs`() {
        BandwidthLog.setEnabled(true)
        val transfer = BandwidthLog.begin("Test", "test", "test", "redacted", "GET")!!
        transfer.bytes(received = 10)
        BandwidthLog.rollback()
        BandwidthLog.setEnabled(true)
        transfer.bytes(received = 100)
        transfer.finish("HTTP 200")
        assertTrue(BandwidthLog.snapshot().entries.isEmpty())
        assertEquals(0, BandwidthLog.snapshot().received)
    }

    @Test fun `concurrent transfers are bounded and totals include evicted rows`() {
        BandwidthLog.setEnabled(true)
        val pool = Executors.newFixedThreadPool(4)
        val completed = CountDownLatch(4)
        repeat(4) {
            pool.execute {
                try {
                    repeat(600) {
                        BandwidthLog.begin("Test", "test", "test", "redacted", "GET")!!.let { transfer ->
                            transfer.bytes(received = 2, sent = 1)
                            transfer.finish("HTTP 200")
                        }
                    }
                } finally { completed.countDown() }
            }
        }
        assertTrue(completed.await(10, java.util.concurrent.TimeUnit.SECONDS))
        pool.shutdownNow()
        val snapshot = BandwidthLog.snapshot()
        assertEquals(BandwidthLog.MAX_ENTRIES, snapshot.entries.size)
        assertEquals(400, snapshot.dropped)
        assertEquals(4800, snapshot.received)
        assertEquals(2400, snapshot.sent)
    }

    @Test fun `websocket payload counts UTF8 bytes without retaining message`() {
        BandwidthLog.setEnabled(true)
        val message = "مرحبا private-token"
        Frame.Text(message).recordBandwidth("Discord", "heartbeat", incoming = true)
        assertEquals(message.toByteArray().size.toLong(), BandwidthLog.snapshot().received)
        assertFalse(BandwidthLog.export().contains("private-token"))
    }

    @Test fun `endpoint redaction removes credentials path identifiers queries and fragments`() {
        assertEquals("https://example.org/api/:id/:id", safeEndpoint("https://user:pass@example.org/api/secret-room/token?key=secret#secret"))
        assertEquals("redacted-endpoint", safeEndpoint("not a URL"))
    }
}
