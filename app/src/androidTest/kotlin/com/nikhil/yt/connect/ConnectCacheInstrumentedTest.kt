package com.nikhil.yt.connect

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.IBinder
import androidx.lifecycle.viewModelScope
import androidx.media3.datasource.cache.ContentMetadataMutations
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import com.nikhil.yt.innertube.models.Thumbnail
import com.nikhil.yt.innertube.models.Thumbnails
import com.nikhil.yt.innertube.models.response.PlayerResponse
import com.nikhil.yt.playback.MusicService
import com.nikhil.yt.utils.YTPlayerUtils
import com.nikhil.yt.viewmodels.CachePlaylistViewModel
import java.util.UUID
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.first
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

@RunWith(AndroidJUnit4::class)
class ConnectCacheInstrumentedTest {
    @Test fun tvAutoplayCacheAppearsEvenBeforeThePhoneReceivesItsQueue() = runBlocking {
        val context = InstrumentationRegistry.getInstrumentation().targetContext
        val bound = CompletableDeferred<MusicService>()
        val connection = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
                bound.complete((binder as MusicService.MusicBinder).service)
            }
            override fun onServiceDisconnected(name: ComponentName?) = Unit
        }
        assertTrue(context.bindService(Intent(context, MusicService::class.java), connection, Context.BIND_AUTO_CREATE))
        var viewModel: CachePlaylistViewModel? = null
        var service: MusicService? = null
        val id = "cache-test-${UUID.randomUUID()}"
        try {
            val music = withTimeout(30_000) { bound.await() }
            service = music
            val details = PlayerResponse.VideoDetails(id, "TV autoplay regression", "Test artist", "artist-id",
                "180", null, "0", Thumbnails(listOf(Thumbnail("https://example.com/cover.jpg", 100, 100))))
            val format = PlayerResponse.StreamingData.Format(251, null, "audio/webm; codecs=\"opus\"", 128000,
                null, null, 1024, "tiny", null, null, null, null, "180000", 48000, 2, null, null, null, null)
            val playback = YTPlayerUtils.PlaybackData(null, details, null, format, "https://example.com/audio", 3600)
            withContext(Dispatchers.Main) {
                assertTrue((0 until music.player.mediaItemCount).none { music.player.getMediaItemAt(it).mediaId == id })
            }
            withContext(Dispatchers.IO) {
                // This is the same recovery path used while serving a Connect media request.
                music.recoverSong(id, playback, allowNetwork = false)
                val span = music.playerCache.startReadWrite(id, 0, 1024)
                try {
                    val file = music.playerCache.startFile(id, 0, 1024)
                    file.writeBytes(ByteArray(1024))
                    music.playerCache.commitFile(file, 1024)
                    music.playerCache.applyContentMetadataMutations(id,
                        ContentMetadataMutations().also { ContentMetadataMutations.setContentLength(it, 1024) })
                } finally { music.playerCache.releaseHoleSpan(span) }
            }
            val model = CachePlaylistViewModel(context, music.database, music.playerCache, music.downloadCache, music.mp3Library)
            viewModel = model
            val visible = withTimeout(10_000) { model.cachedSongs.first { songs -> songs.any { it.id == id } } }.first { it.id == id }
            assertEquals("TV autoplay regression", visible.title)
            assertEquals("Test artist", visible.artists.single().name)
            assertEquals(180, visible.song.duration)
            assertFalse(visible.song.liked)
        } finally {
            viewModel?.viewModelScope?.cancel()
            withContext(Dispatchers.IO) {
                service?.let { music ->
                    music.playerCache.removeResource(id)
                    music.database.getSongById(id)?.song?.let { music.database.delete(it) }
                }
            }
            context.unbindService(connection)
        }
    }
}
