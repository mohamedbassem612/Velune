package com.nikhil.yt.connect

import android.content.Context
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.AtomicFile
import org.json.JSONArray
import org.json.JSONObject
import java.io.File
import java.math.BigInteger
import java.security.KeyPairGenerator
import java.security.KeyStore
import java.security.PrivateKey
import java.security.SecureRandom
import java.security.cert.X509Certificate
import java.util.Date
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec
import javax.security.auth.x500.X500Principal
import kotlinx.coroutines.flow.MutableStateFlow

/** No exportable private keys or account credentials are stored in the application files. */
internal class ConnectIdentity(private val alias: String = "velune.connect.identity.v2") : ConnectCredentials {
    private val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    init {
        if (!store.containsAlias(alias)) {
            KeyPairGenerator.getInstance(KeyProperties.KEY_ALGORITHM_RSA, "AndroidKeyStore").apply {
                initialize(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_SIGN or KeyProperties.PURPOSE_VERIFY)
                    // Conscrypt can hash/pad TLS signatures itself before invoking Keystore.
                    // These raw-operation authorizations do not export the key or remove TLS padding.
                    .setKeySize(2048).setDigests(KeyProperties.DIGEST_NONE, KeyProperties.DIGEST_SHA256,
                        KeyProperties.DIGEST_SHA384, KeyProperties.DIGEST_SHA512)
                    .setSignaturePaddings(KeyProperties.SIGNATURE_PADDING_RSA_PKCS1, KeyProperties.SIGNATURE_PADDING_RSA_PSS)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setCertificateSubject(X500Principal("CN=Velune Connect"))
                    .setCertificateSerialNumber(BigInteger(128, SecureRandom()))
                    .setCertificateNotBefore(Date(1577836800000L))
                    .setCertificateNotAfter(Date(4102444800000L)).build())
            }.generateKeyPair()
        }
    }
    override val certificate get() = store.getCertificate(alias) as X509Certificate
    override val privateKey get() = store.getKey(alias, null) as PrivateKey
    override val id get() = publicKeyId(certificate)
}

data class ConnectTrustedDevice(val id: String, val name: String)

/** Pins are integrity protected with AES-GCM and are deliberately excluded from backup. */
internal class ConnectTrustStore(context: Context, private val alias: String = "velune.connect.trust.v1") : ConnectPins {
    private val file = AtomicFile(File(context.noBackupFilesDir, "connect-trust-v1"))
    private val store = KeyStore.getInstance("AndroidKeyStore").apply { load(null) }
    private val key: SecretKey
    val devices = MutableStateFlow<List<ConnectTrustedDevice>>(emptyList())
    init {
        val existed = store.containsAlias(alias)
        if (!existed) KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, "AndroidKeyStore").apply {
            init(KeyGenParameterSpec.Builder(alias, KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT)
                .setBlockModes(KeyProperties.BLOCK_MODE_GCM).setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE).build())
        }.generateKey()
        key = store.getKey(alias, null) as SecretKey
        if (file.baseFile.exists() || File(file.baseFile.path + ".bak").exists()) {
            // Key loss/corruption fails closed; never turn a name into replacement trust.
            devices.value = runCatching {
                check(existed)
                val bytes = file.openRead().use { it.readBytes() }
                val cipher = Cipher.getInstance("AES/GCM/NoPadding")
                cipher.init(Cipher.DECRYPT_MODE, key, GCMParameterSpec(128, bytes.copyOfRange(0, 12)))
                val items = JSONArray(String(cipher.doFinal(bytes.copyOfRange(12, bytes.size)), Charsets.UTF_8))
                (0 until items.length()).map { items.getJSONObject(it) }.map {
                    ConnectTrustedDevice(it.getString("id"), it.getString("name"))
                }.also { list -> require(list.all { it.id.matches(Regex("[0-9a-f]{64}")) }) }
            }.getOrDefault(emptyList())
        }
    }
    @Synchronized override fun contains(id: String) = devices.value.any { it.id == id }
    @Synchronized override fun trust(id: String, name: String) {
        require(id.matches(Regex("[0-9a-f]{64}")))
        save(devices.value.filterNot { it.id == id } + ConnectTrustedDevice(id, name.take(100)))
    }
    @Synchronized override fun revoke(id: String) = save(devices.value.filterNot { it.id == id })
    private fun save(items: List<ConnectTrustedDevice>) {
        val text = JSONArray(items.map { JSONObject().put("id", it.id).put("name", it.name) }).toString()
        val cipher = Cipher.getInstance("AES/GCM/NoPadding").apply { init(Cipher.ENCRYPT_MODE, key) }
        val bytes = cipher.iv + cipher.doFinal(text.toByteArray(Charsets.UTF_8))
        val output = file.startWrite()
        try { output.write(bytes); file.finishWrite(output); devices.value = items }
        catch (failure: Exception) { file.failWrite(output); throw failure }
    }
}
