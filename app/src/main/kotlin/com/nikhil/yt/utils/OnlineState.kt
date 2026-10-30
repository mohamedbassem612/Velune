package com.nikhil.yt.utils

import android.content.Context
import android.net.ConnectivityManager
import android.net.Network
import android.net.NetworkCapabilities
import androidx.core.content.getSystemService
import kotlinx.coroutines.channels.awaitClose
import kotlinx.coroutines.flow.Flow
import kotlinx.coroutines.flow.callbackFlow
import kotlinx.coroutines.flow.distinctUntilChanged

private fun NetworkCapabilities?.hasWorkingInternet(): Boolean =
    this != null &&
        hasCapability(NetworkCapabilities.NET_CAPABILITY_INTERNET) &&
        hasCapability(NetworkCapabilities.NET_CAPABILITY_VALIDATED)

/** True when the phone has a network that really reaches the internet (Wi-Fi without internet counts as offline). */
fun Context.isOnlineNow(): Boolean {
    val manager = getSystemService<ConnectivityManager>() ?: return true
    return try {
        manager.getNetworkCapabilities(manager.activeNetwork).hasWorkingInternet()
    } catch (_: Exception) {
        true
    }
}

/** Emits true/false every time the phone goes online or offline. The first value is the current state. */
fun Context.onlineFlow(): Flow<Boolean> = callbackFlow {
    val manager = getSystemService<ConnectivityManager>()
    if (manager == null) {
        trySend(true)
        awaitClose { }
        return@callbackFlow
    }
    val callback = object : ConnectivityManager.NetworkCallback() {
        override fun onCapabilitiesChanged(network: Network, capabilities: NetworkCapabilities) {
            trySend(capabilities.hasWorkingInternet())
        }

        override fun onLost(network: Network) {
            trySend(false)
        }
    }
    trySend(isOnlineNow())
    try {
        manager.registerDefaultNetworkCallback(callback)
    } catch (_: Exception) {
    }
    awaitClose {
        try {
            manager.unregisterNetworkCallback(callback)
        } catch (_: Exception) {
        }
    }
}.distinctUntilChanged()
