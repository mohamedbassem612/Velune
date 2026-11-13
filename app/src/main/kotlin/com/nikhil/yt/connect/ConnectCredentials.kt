package com.nikhil.yt.connect

import java.security.MessageDigest
import java.security.PrivateKey
import java.security.cert.X509Certificate

internal fun publicKeyId(certificate: X509Certificate): String =
    MessageDigest.getInstance("SHA-256").digest(certificate.publicKey.encoded).joinToString("") { "%02x".format(it) }

internal interface ConnectCredentials {
    val certificate: X509Certificate
    val privateKey: PrivateKey
    val id: String get() = publicKeyId(certificate)
}

internal interface ConnectPins {
    fun contains(id: String): Boolean
    fun trust(id: String, name: String)
    fun revoke(id: String)
}
