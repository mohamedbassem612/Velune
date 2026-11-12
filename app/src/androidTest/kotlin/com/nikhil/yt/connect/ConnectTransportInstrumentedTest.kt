package com.nikhil.yt.connect

import android.content.Context
import android.content.ContextWrapper
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.security.KeyStore
import java.util.UUID
import javax.net.ssl.SSLSocket
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.filterNotNull
import kotlinx.coroutines.flow.first
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Requires Android Keystore and real TLS sockets. Never represented as a JVM-only test. */
@RunWith(AndroidJUnit4::class)
class ConnectTransportInstrumentedTest {
    @Test fun explicitDualApprovalTrustedReconnectAndRevocation() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val prefix = "connect.test.${UUID.randomUUID()}"
        fun separate(name: String): Context = object : ContextWrapper(context) {
            override fun getNoBackupFilesDir(): File = File(context.noBackupFilesDir, "$prefix-$name").apply { mkdirs() }
        }
        val phoneContext = separate("phone"); val tvContext = separate("tv")
        val phone = ConnectIdentity("$prefix.phone")
        val tv = ConnectIdentity("$prefix.tv")
        val phoneTrust = ConnectTrustStore(phoneContext, "$prefix.phoneTrust")
        val tvTrust = ConnectTrustStore(tvContext, "$prefix.tvTrust")
        val phoneTransport = ConnectTransport(phone, phoneTrust)
        val tvTransport = ConnectTransport(tv, tvTrust)
        val listener = tvTransport.server()
        suspend fun connection(pair: Boolean): Pair<ConnectChannel, ConnectChannel> = coroutineScope {
            val server = async(Dispatchers.IO) {
                val socket = listener.accept() as SSLSocket
                try { tvTransport.authorize(socket, true) } catch (e: Exception) { socket.close(); throw e }
            }
            val client = async(Dispatchers.IO) {
                val socket = phoneTransport.connect("127.0.0.1", listener.localPort, tv.id, pair)
                try { phoneTransport.authorize(socket, false) } catch (e: Exception) { socket.close(); throw e }
            }
            if (pair) {
                val a = withTimeout(10_000) { phoneTransport.pairing.filterNotNull().first() }
                val b = withTimeout(10_000) { tvTransport.pairing.filterNotNull().first() }
                assertEquals(a.code, b.code)
                assertFalse(phoneTrust.contains(tv.id)); assertFalse(tvTrust.contains(phone.id))
                phoneTransport.approve(true); tvTransport.approve(true)
            }
            client.await() to server.await()
        }
        try {
            tvTransport.openPairing()
            val paired = withTimeout(30_000) { connection(true) }
            assertTrue(phoneTrust.contains(tv.id)); assertTrue(tvTrust.contains(phone.id))
            paired.first.close(); paired.second.close()
            assertEquals(phone.id, ConnectIdentity("$prefix.phone").id)
            assertTrue(ConnectTrustStore(phoneContext, "$prefix.phoneTrust").contains(tv.id))
            val reconnected = withTimeout(10_000) { connection(false) }
            reconnected.first.write(JSONObject().put("type", "test"))
            assertEquals("test", reconnected.second.read()!!.getString("type"))
            reconnected.first.close(); reconnected.second.close()
            tvTrust.revoke(phone.id)
            assertFalse(tvTrust.contains(phone.id))
            // Unknown client is rejected by the trust decision with pairing closed.
            assertFalse(ConnectPolicy.mayAuthenticate(tvTrust.contains(phone.id), false, null, phone.id))
        } finally {
            listener.close()
            phoneContext.noBackupFilesDir.deleteRecursively(); tvContext.noBackupFilesDir.deleteRecursively()
            val keys = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
            listOf("phone", "tv", "phoneTrust", "tvTrust").forEach { keys.deleteEntry("$prefix.$it") }
        }
    }
}
