package com.nikhil.yt.playback

import android.content.Context
import androidx.media3.datasource.cache.Cache
import com.nikhil.yt.db.MusicDatabase
import com.nikhil.yt.di.DownloadCache
import com.nikhil.yt.di.PlayerCache
import com.nikhil.yt.utils.onlineFlow
import dagger.hilt.android.qualifiers.ApplicationContext
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.flow
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.isActive
import timber.log.Timber

/** One snapshot shared by all song rows, cards and queue entries. */
data class OfflineSongState(
    val isOffline: Boolean = false,
    val playableIds: Set<String> = emptySet(),
) {
    fun isUnavailable(id: String): Boolean = isOffline && id !in playableIds
}

@OptIn(ExperimentalCoroutinesApi::class)
@Singleton
class OfflineSongAvailability @Inject constructor(
    @ApplicationContext context: Context,
    private val database: MusicDatabase,
    @PlayerCache private val playerCache: Cache,
    @DownloadCache private val downloadCache: Cache,
    private val mp3Library: Mp3Library,
) {
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.IO)

    // No storage scans online or while the UI is stopped. Polling also catches cache eviction,
    // conversion to MP3 and files removed outside the app, without a listener per song row.
    val state = context.onlineFlow().flatMapLatest { online ->
        if (online) flowOf(OfflineSongState()) else flow {
            while (currentCoroutineContext().isActive) {
                try {
                    emit(OfflineSongState(isOffline = true, playableIds = findPlayableIds()))
                } catch (e: CancellationException) {
                    throw e
                } catch (e: Exception) {
                    Timber.w(e, "Could not refresh offline song availability")
                }
                delay(3_000)
            }
        }
    }.stateIn(scope, SharingStarted.WhileSubscribed(stopTimeoutMillis = 0, replayExpirationMillis = 0), OfflineSongState())

    private suspend fun findPlayableIds(): Set<String> {
        val playable = mp3Library.ids().filterTo(mutableSetOf()) {
            mp3Library.playableUri(it) != null
        }
        val cachedIds = (playerCache.keys + downloadCache.keys) - playable
        // Room/SQLite has a limit on the number of parameters in an IN query.
        for (chunk in cachedIds.toList().chunked(500)) {
            val lengths = database.getSongsByIds(chunk).associate { it.id to it.format?.contentLength }
            for (id in chunk) {
                if (CacheAvailability.isCached(downloadCache, id, fallbackContentLength = lengths[id]) ||
                    CacheAvailability.isCached(playerCache, id, fallbackContentLength = lengths[id])
                ) {
                    playable += id
                }
            }
        }
        return playable
    }
}
