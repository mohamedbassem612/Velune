package com.nikhil.yt.connect

import android.content.Context
import android.content.ContextWrapper
import android.security.keystore.KeyInfo
import android.security.keystore.KeyProperties
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import java.io.File
import java.security.KeyStore
import java.security.KeyFactory
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
    @Test fun directLanConnectionAndReconnectWithoutPairing() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val prefix = "connect.test.${UUID.randomUUID()}"
        fun separate(name: String): Context = object : ContextWrapper(context) {
            override fun getNoBackupFilesDir(): File = File(context.noBackupFilesDir, "$prefix-$name").apply { mkdirs() }
        }
        val phoneContext = separate("phone"); val tvContext = separate("tv")
        val phone = ConnectIdentity("$prefix.phone")
        val tv = ConnectIdentity("$prefix.tv")
        for (identity in listOf(phone, tv)) {
            val info = KeyFactory.getInstance(KeyProperties.KEY_ALGORITHM_RSA, "AndroidKeyStore")
                .getKeySpec(identity.privateKey, KeyInfo::class.java)
            assertTrue(info.digests.contains(KeyProperties.DIGEST_NONE))
            assertTrue(info.encryptionPaddings.contains(KeyProperties.ENCRYPTION_PADDING_NONE))
            assertNull(identity.privateKey.encoded)
        }
        assertEquals(phone.id, ConnectIdentity("$prefix.phone").id)
        val phoneTrust = ConnectTrustStore(phoneContext, "$prefix.phoneTrust")
        val tvTrust = ConnectTrustStore(tvContext, "$prefix.tvTrust")
        val phoneTransport = ConnectTransport(phone, phoneTrust, localNetworkAccess = true)
        val tvTransport = ConnectTransport(tv, tvTrust, localNetworkAccess = true)
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
            client.await() to server.await()
        }
        try {
            val paired = withTimeout(30_000) { connection(false) }
            assertNull(phoneTransport.pairing.value); assertNull(tvTransport.pairing.value)
            assertFalse(phoneTransport.pairingOpen.value); assertFalse(tvTransport.pairingOpen.value)
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
            // LAN discovery does not depend on a remembered device or a pairing window.
            val fresh = withTimeout(15_000) { connection(false) }
            assertTrue(tvTrust.contains(phone.id))
            assertNull(tvTransport.pairing.value)
            fresh.first.close(); fresh.second.close()
        } finally {
            listener.close()
            phoneContext.noBackupFilesDir.deleteRecursively(); tvContext.noBackupFilesDir.deleteRecursively()
            val keys = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
            listOf("phone", "tv", "phoneTrust", "tvTrust").forEach { keys.deleteEntry("$prefix.$it") }
        }
    }
}
