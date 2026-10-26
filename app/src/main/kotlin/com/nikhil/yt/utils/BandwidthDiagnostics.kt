package com.nikhil.yt.utils

import android.content.Context
import android.net.TrafficStats
import android.os.Process
import android.os.SystemClock
import androidx.lifecycle.DefaultLifecycleObserver
import androidx.lifecycle.LifecycleOwner
import androidx.lifecycle.ProcessLifecycleOwner
import androidx.media3.datasource.cache.CacheDataSource
import coil3.EventListener
import coil3.decode.DataSource as CoilDataSource
import coil3.request.ImageRequest
import coil3.request.SuccessResult
import com.nikhil.yt.diagnostics.BandwidthLog
import com.nikhil.yt.diagnostics.bandwidthDiagnostics
import com.nikhil.yt.diagnostics.safeEndpoint
import okhttp3.HttpUrl.Companion.toHttpUrlOrNull
import okhttp3.OkHttpClient

/** The only persisted state is the opt-in. Logs stay in a bounded in-memory session. */
object BandwidthDiagnostics {
    private const val PREFS = "experimental_bandwidth"
    private const val ENABLED = "enabled"
    private var baselineRx = -1L
    private var baselineTx = -1L
    private var pausedTotals: Pair<Long, Long>? = null

    fun initialize(context: Context) {
        // Lets an export tell a warm return from background apart from a process that was killed and restarted.
        BandwidthLog.headerExtras = {
            val processStartMs = System.currentTimeMillis() - (SystemClock.uptimeMillis() - Process.getStartUptimeMillis())
            "# processStartMs=$processStartMs\tpid=${Process.myPid()}"
        }
        ProcessLifecycleOwner.get().lifecycle.addObserver(object : DefaultLifecycleObserver {
            override fun onStart(owner: LifecycleOwner) = BandwidthLog.setForeground(true)
            override fun onStop(owner: LifecycleOwner) = BandwidthLog.setForeground(false)
        })
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

    /** Records Coil results that never reach the network: disk hits as rows, memory hits as a counter. */
    val coilEvents = EventListener.Factory {
        object : EventListener() {
            override fun onSuccess(request: ImageRequest, result: SuccessResult) {
                if (!BandwidthLog.enabled) return
                val raw = request.data.toString()
                if (!raw.startsWith("http")) return
                when (result.dataSource) {
                    CoilDataSource.MEMORY_CACHE, CoilDataSource.MEMORY -> BandwidthLog.countCache("Coil.memory")
                    CoilDataSource.DISK -> {
                        // Same normalisation OkHttp applies, so the key matches the network row for this URL.
                        val url = raw.toHttpUrlOrNull()?.toString() ?: raw
                        BandwidthLog.cacheHit(
                            "Coil.ImageLoader", BandwidthLog.caller("Coil.ImageLoader"), "Artwork served from Coil disk cache",
                            safeEndpoint(url), "GET", "HIT-DISK", key = BandwidthLog.keyOf(url),
                        )
                    }
                    else -> Unit // NETWORK is already a row from imageClient.
                }
            }
        }
    }

    /** Media3 reports how many bytes a cache layer served and when it was bypassed. */
    fun cacheListener(label: String) = object : CacheDataSource.EventListener {
        override fun onCachedBytesRead(cacheSizeBytes: Long, cachedBytesRead: Long) {
            BandwidthLog.cacheHit(
                "MusicService.$label", BandwidthLog.caller("MusicService.$label"), "Audio bytes read from $label",
                "media3-cache", "CACHE READ", "HIT", cachedBytes = cachedBytesRead,
            )
        }

        override fun onCacheIgnored(reason: Int) {
            val why = if (reason == CacheDataSource.CACHE_IGNORED_REASON_ERROR) "ERROR" else "UNSET_LENGTH"
            BandwidthLog.note(
                "MusicService.$label", BandwidthLog.caller("MusicService.$label"), "Cache bypassed for this read",
                "media3-cache", "cache ignored: $why",
            )
        }
    }

    /** Explains, per song, why audio did or did not need the network. The id is stored only as a salted hash. */
    fun resolverNote(mediaId: String, status: String) {
        if (!BandwidthLog.enabled) return
        BandwidthLog.note(
            "MusicService.resolver", BandwidthLog.caller("MusicService.resolver"), "Audio cache check before network",
            "media3-cache", status, key = BandwidthLog.keyOf(mediaId),
        )
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
