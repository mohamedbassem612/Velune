package com.nikhil.yt.utils

import android.content.Context
import android.net.TrafficStats
import android.os.Process
import com.nikhil.yt.diagnostics.BandwidthLog
import com.nikhil.yt.diagnostics.bandwidthDiagnostics
import okhttp3.OkHttpClient

/** The only persisted state is the opt-in. Logs stay in a bounded in-memory session. */
object BandwidthDiagnostics {
    private const val PREFS = "experimental_bandwidth"
    private const val ENABLED = "enabled"
    private var baselineRx = -1L
    private var baselineTx = -1L
    private var pausedTotals: Pair<Long, Long>? = null

    fun initialize(context: Context) {
        if (context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(ENABLED, false)) {
            setEnabled(context, true)
        }
    }

    fun setEnabled(context: Context, enabled: Boolean) {
        if (enabled) {
            if (baselineRx < 0) {
                baselineRx = TrafficStats.getUidRxBytes(Process.myUid())
                baselineTx = TrafficStats.getUidTxBytes(Process.myUid())
            } else if (pausedTotals != null) {
                val totals = pausedTotals!!
                baselineRx = TrafficStats.getUidRxBytes(Process.myUid()).takeIf { it >= 0 && totals.first >= 0 }
                    ?.minus(totals.first) ?: -1
                baselineTx = TrafficStats.getUidTxBytes(Process.myUid()).takeIf { it >= 0 && totals.second >= 0 }
                    ?.minus(totals.second) ?: -1
            }
            pausedTotals = null
        } else {
            pausedTotals = uidTotals()
        }
        BandwidthLog.setEnabled(enabled)
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(ENABLED, enabled).apply()
    }

    fun uidTotals(): Pair<Long, Long> {
        pausedTotals?.let { return it }
        val rx = TrafficStats.getUidRxBytes(Process.myUid())
        val tx = TrafficStats.getUidTxBytes(Process.myUid())
        return (if (baselineRx >= 0 && rx >= baselineRx) rx - baselineRx else -1L) to
            (if (baselineTx >= 0 && tx >= baselineTx) tx - baselineTx else -1L)
    }

    fun clear() {
        BandwidthLog.clear()
        baselineRx = if (BandwidthLog.enabled) TrafficStats.getUidRxBytes(Process.myUid()) else -1
        baselineTx = if (BandwidthLog.enabled) TrafficStats.getUidTxBytes(Process.myUid()) else -1
        pausedTotals = if (BandwidthLog.enabled) null else (-1L to -1L)
    }

    fun rollback(context: Context) {
        BandwidthLog.rollback()
        baselineRx = -1
        baselineTx = -1
        pausedTotals = null
        context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().clear().apply()
    }

    fun webViewRequest(request: android.webkit.WebResourceRequest, source: String) {
        if (!BandwidthLog.enabled) return
        BandwidthLog.begin(source, "$source.shouldInterceptRequest", "Login / token page and its resources; may use WebView cache",
            com.nikhil.yt.diagnostics.safeEndpoint(request.url.toString()), "OBSERVED ${request.method}")
            ?.finish("requested; bytes unavailable")
    }

    val imageClient by lazy {
        OkHttpClient.Builder().bandwidthDiagnostics("Coil.ImageLoader", "Download artwork / thumbnails on image cache miss").build()
    }
    val widgetClient by lazy {
        OkHttpClient.Builder().bandwidthDiagnostics("updateVeluneWidgetState", "Download widget artwork after track change").build()
    }
    val updateClient by lazy {
        OkHttpClient.Builder()
            .connectTimeout(15, java.util.concurrent.TimeUnit.SECONDS)
            .readTimeout(30, java.util.concurrent.TimeUnit.SECONDS)
            .bandwidthDiagnostics("ApkInstaller.download", "Download application update APK").build()
    }
    val releaseNotesClient by lazy {
        OkHttpClient.Builder().bandwidthDiagnostics("fetchReleaseNotesText", "Download release notes HTML").build()
    }
}
