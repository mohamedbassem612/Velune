package com.nikhil.yt.ui

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.graphics.Bitmap
import android.graphics.Rect
import android.os.IBinder
import android.os.SystemClock
import android.view.InputDevice
import android.view.MotionEvent
import android.view.accessibility.AccessibilityNodeInfo
import androidx.datastore.preferences.core.edit
import androidx.activity.compose.setContent
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.*
import androidx.media3.common.Player
import androidx.media3.datasource.cache.ContentMetadataMutations
import androidx.test.ext.junit.runners.AndroidJUnit4
import androidx.test.platform.app.InstrumentationRegistry
import androidx.test.runner.lifecycle.ActivityLifecycleMonitorRegistry
import androidx.test.runner.lifecycle.Stage
import com.nikhil.yt.MainActivity
import com.nikhil.yt.R
import com.nikhil.yt.constants.*
import com.nikhil.yt.db.entities.FormatEntity
import com.nikhil.yt.db.entities.LyricsEntity
import com.nikhil.yt.extensions.toMediaItem
import com.nikhil.yt.models.MediaMetadata
import com.nikhil.yt.playback.MusicService
import com.nikhil.yt.playback.queues.ListQueue
import com.nikhil.yt.ui.screens.settings.DarkMode
import com.nikhil.yt.ui.screens.settings.NavigationTab
import com.nikhil.yt.utils.dataStore
import com.nikhil.yt.utils.ReleaseInfo
import com.nikhil.yt.ui.component.UpdateAvailableDialog
import java.io.File
import java.nio.ByteBuffer
import java.nio.ByteOrder
import java.time.LocalDateTime
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.flow.first
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Run on a disposable emulator. A fully cached silent WAV keeps this test independent of YouTube. */
@RunWith(AndroidJUnit4::class)
class MusicDesignInstrumentedTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val automation get() = instrumentation.uiAutomation
    private val title = "ليل هادئ — A quiet evening with a very long song title"

    @Test fun sharedPlaybackSurvivesPlayerSearchThemeAndRotation() = exercisePlayer(true)

    @Test fun metroControlsAndLyricsKeepTheirOwnScreen() = exercisePlayer(false)

    @Test fun updateDialogShowsSizeBeforeDownload() {
        runBlocking { context.dataStore.edit { it[DefaultOpenTabKey] = NavigationTab.LIBRARY.name } }
        val activity = launchActivity()
        val size = 73400320L
        val label = context.getString(R.string.update_dialog_size,
            android.text.format.Formatter.formatShortFileSize(context, size))
        try {
            instrumentation.runOnMainSync {
                activity.setContent {
                    MaterialTheme {
                        UpdateAvailableDialog(ReleaseInfo("v9.9.9", "UI fixture", "Test release notes", "", "",
                            "https://example.com/fixture.apk", size), onDismiss = {})
                    }
                }
            }
            await("Update size must be visible before download") { find(label) != null }
            capture("08-update-size")
        } finally {
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    private fun exercisePlayer(useMusicPlayer: Boolean) {
        runBlocking { context.dataStore.edit {
            it[DarkModeKey] = DarkMode.ON.name
            it[PureBlackKey] = true
            it[DynamicThemeKey] = false
            it[UseMusicPlayerKey] = useMusicPlayer
            it[PlayerDesignStyleKey] = PlayerDesignStyle.V5.name
            it[PlayerBackgroundStyleKey] = PlayerBackgroundStyle.DEFAULT.name
            it[DefaultOpenTabKey] = NavigationTab.LIBRARY.name
            it[AppLanguageKey] = InstrumentationRegistry.getArguments().getString("language", "en")
        } }
        val activity = launchActivity()
        val bound = CountDownLatch(1)
        lateinit var service: MusicService
        val binding = object : ServiceConnection {
            override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
                service = (binder as MusicService.MusicBinder).service
                bound.countDown()
            }
            override fun onServiceDisconnected(name: ComponentName?) = Unit
        }
        assertTrue(context.bindService(Intent(context, MusicService::class.java), binding, Context.BIND_AUTO_CREATE))
        assertTrue("Music service did not bind", bound.await(90, TimeUnit.SECONDS))
        val tracks = listOf(
            MediaMetadata("music-ui-fixture-1", title, listOf(MediaMetadata.Artist(null, "فيروز · Fairuz")), 120, inLibrary = LocalDateTime.now()),
            MediaMetadata("music-ui-fixture-2", "Evening light", emptyList(), 120, inLibrary = LocalDateTime.now()),
            MediaMetadata("music-ui-fixture-3", "موسيقى بلا غلاف", listOf(MediaMetadata.Artist(null, "فنان تجريبي")), 120, inLibrary = LocalDateTime.now()),
        )
        val wav = silentWav()
        try {
            tracks.forEach { track ->
                service.database.insert(track)
                service.database.upsert(FormatEntity(track.id, 0, "audio/wav", "pcm_s16le", 128_000, 8_000, wav.size.toLong(), null, playbackUrl = null))
                service.database.upsert(LyricsEntity(track.id,
                    "[00:00.00]First lyric line\n[00:15.00]Second lyric line\n[00:30.00]Third lyric line"))
                val hole = service.downloadCache.startReadWrite(track.id, 0, wav.size.toLong())
                try {
                    if (!hole.isCached) {
                        val file = service.downloadCache.startFile(track.id, 0, wav.size.toLong())
                        file.writeBytes(wav)
                        service.downloadCache.commitFile(file, wav.size.toLong())
                        service.downloadCache.applyContentMetadataMutations(track.id,
                            ContentMetadataMutations().apply { ContentMetadataMutations.setContentLength(this, wav.size.toLong()) })
                    }
                } finally { if (!hole.isCached) service.downloadCache.releaseHoleSpan(hole) }
            }
            instrumentation.runOnMainSync {
                service.playQueue(ListQueue("UI test", tracks.map { it.toMediaItem() }, position = 17_000))
            }
            await("Cached fixture did not become ready") { main { service.sessionPlayer.playbackState == Player.STATE_READY } }
            instrumentation.runOnMainSync { service.sessionPlayer.pause(); service.sessionPlayer.seekTo(17_000) }
            await("Mini player did not appear") { find(title) != null }
            if (useMusicPlayer) {
                swipe(title, toLeft = true)
                await("Left swipe did not select next song") { main { service.sessionPlayer.currentMediaItem?.mediaId } == tracks[1].id }
                instrumentation.runOnMainSync { service.sessionPlayer.pause(); service.sessionPlayer.seekTo(0) }
                await("Next track title did not update") { find(tracks[1].title) != null }
                assertNull("Horizontal swipe must not expand the player", find(activity.getString(R.string.queue)))
                capture("00-mini-swipe-next")
                swipe(tracks[1].title, toLeft = false)
                await("Right swipe did not select previous song") { main { service.sessionPlayer.currentMediaItem?.mediaId } == tracks[0].id }
                assertNull("Reverse swipe must not expand the player", find(activity.getString(R.string.queue)))
                instrumentation.runOnMainSync { service.sessionPlayer.pause(); service.sessionPlayer.seekTo(17_000) }
                await("Previous track did not return to the mini player") { find(title) != null }
            }
            capture("01-home-amoled")
            tap(title)
            await("Full player did not open") { find(activity.getString(R.string.music_options)) != null }
            capture("02-player-amoled")
            assertControlPositions(activity)
            assertPosition(service)
            tap(activity.getString(R.string.music_options))
            capture("03-options")
            pressBack()
            assertPosition(service)
            tap(activity.getString(R.string.queue))
            capture("04-queue")
            pressBack()
            assertPosition(service)
            tap(activity.getString(R.string.lyrics))
            await("Lyrics screen did not open") { find(activity.getString(R.string.close)) != null }
            capture("04b-lyrics-opaque")
            assertPosition(service)
            pressBack()
            await("Closing lyrics did not restore the player") { find(activity.getString(R.string.queue)) != null }
            assertControlPositions(activity)
            pressBack()
            tap(activity.getString(R.string.search))
            capture("05-search-empty")
            assertPosition(service)
            runBlocking { context.dataStore.edit { it[DarkModeKey] = DarkMode.OFF.name } }
            SystemClock.sleep(1_000)
            capture("06-search-light")
            assertPosition(service)
            instrumentation.runOnMainSync {
                activity.requestedOrientation = android.content.pm.ActivityInfo.SCREEN_ORIENTATION_LANDSCAPE
            }
            SystemClock.sleep(2_000)
            capture("07-landscape-light")
            assertPosition(service)
            assertEquals("Opening surfaces must preserve the queue", 3, main { service.sessionPlayer.mediaItemCount })
        } finally {
            instrumentation.runOnMainSync { service.stopAndClearPlayback() }
            context.unbindService(binding)
            instrumentation.runOnMainSync { activity.finish() }
        }
    }

    private fun assertPosition(service: MusicService) {
        assertEquals("UI navigation replaced the current track", "music-ui-fixture-1", main { service.sessionPlayer.currentMediaItem?.mediaId })
        assertTrue("UI navigation reset playback progress", main { service.sessionPlayer.currentPosition } in 16_500L..17_500L)
    }

    private fun assertControlPositions(activity: MainActivity) {
        fun bounds(resource: Int): Rect {
            val label = activity.getString(resource)
            await("Missing control $label") { find(label) != null }
            return Rect().also { find(label)!!.getBoundsInScreen(it) }
        }
        val devices = bounds(R.string.connect_devices)
        val share = bounds(R.string.share)
        val like = bounds(R.string.action_like)
        assertTrue("Track actions must be devices, share, like", devices.centerX() < share.centerX() && share.centerX() < like.centerX())
        val shuffle = bounds(R.string.music_shuffle_off)
        val previous = bounds(R.string.music_previous)
        val play = bounds(R.string.play)
        val next = bounds(R.string.music_next)
        val repeat = bounds(R.string.repeat_mode_off)
        assertTrue("Transport order must match the reference",
            shuffle.centerX() < previous.centerX() && previous.centerX() < play.centerX() &&
                play.centerX() < next.centerX() && next.centerX() < repeat.centerX())
        assertTrue("Track actions must be above transport controls", share.centerY() < previous.top)
        assertTrue("Queue and lyrics must be below transport controls", bounds(R.string.queue).centerY() > play.bottom)
        assertTrue("Play button must be larger than previous/next", play.height() > previous.height())
    }

    private fun launchActivity(): MainActivity {
        context.startActivity(Intent(context, MainActivity::class.java)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK or Intent.FLAG_ACTIVITY_CLEAR_TASK))
        var activity: MainActivity? = null
        await("Activity did not resume") {
            instrumentation.runOnMainSync {
                activity = ActivityLifecycleMonitorRegistry.getInstance().getActivitiesInStage(Stage.RESUMED)
                    .filterIsInstance<MainActivity>().firstOrNull()
            }
            activity != null
        }
        return activity!!
    }

    private fun <T> main(block: () -> T): T {
        var result: T? = null
        instrumentation.runOnMainSync { result = block() }
        @Suppress("UNCHECKED_CAST") return result as T
    }

    private fun find(label: String): AccessibilityNodeInfo? {
        val root = automation.rootInActiveWindow ?: return null
        fun walk(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
            if (node.isVisibleToUser && (node.text?.toString() == label || node.contentDescription?.toString() == label)) return node
            for (index in 0 until node.childCount) node.getChild(index)?.let { walk(it)?.let { result -> return result } }
            return null
        }
        return walk(root)
    }

    private fun tap(label: String) {
        await("Missing UI element: $label") { find(label) != null }
        val node = find(label)!!
        val bounds = Rect().also(node::getBoundsInScreen)
        val now = SystemClock.uptimeMillis()
        listOf(MotionEvent.ACTION_DOWN, MotionEvent.ACTION_UP).forEach { action ->
            val event = MotionEvent.obtain(now, SystemClock.uptimeMillis(), action, bounds.exactCenterX(), bounds.exactCenterY(), 0)
            event.source = InputDevice.SOURCE_TOUCHSCREEN
            automation.injectInputEvent(event, true)
            event.recycle()
        }
        SystemClock.sleep(600)
    }

    private fun swipe(label: String, toLeft: Boolean) {
        val bounds = Rect().also { find(label)!!.getBoundsInScreen(it) }
        val startX = bounds.left + bounds.width() * if (toLeft) 0.9f else 0.1f
        val endX = bounds.left + bounds.width() * if (toLeft) 0.1f else 0.9f
        val downTime = SystemClock.uptimeMillis()
        for (step in 0..12) {
            val action = when (step) {
                0 -> MotionEvent.ACTION_DOWN
                12 -> MotionEvent.ACTION_UP
                else -> MotionEvent.ACTION_MOVE
            }
            val x = startX + (endX - startX) * step / 12f
            val event = MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action, x, bounds.exactCenterY(), 0)
            event.source = InputDevice.SOURCE_TOUCHSCREEN
            automation.injectInputEvent(event, true)
            event.recycle()
            SystemClock.sleep(25)
        }
        SystemClock.sleep(600)
    }

    private fun pressBack() {
        automation.performGlobalAction(android.accessibilityservice.AccessibilityService.GLOBAL_ACTION_BACK)
        SystemClock.sleep(600)
    }

    private fun capture(name: String) {
        val language = InstrumentationRegistry.getArguments().getString("language", "en")
        val player = runBlocking { context.dataStore.data.first()[UseMusicPlayerKey] ?: true }
        val file = File(context.getExternalFilesDir(null), "music-ui/$language-${if (player) "default" else "metro"}-$name.png")
        file.parentFile!!.mkdirs()
        automation.takeScreenshot()!!.let { bitmap ->
            file.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            bitmap.recycle()
        }
    }

    private fun await(message: String, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + 90_000
        while (SystemClock.elapsedRealtime() < deadline) {
            if (condition()) return
            SystemClock.sleep(250)
        }
        fail(message)
    }

    private fun silentWav(): ByteArray {
        val dataSize = 120 * 8_000 * 2
        return ByteBuffer.allocate(44 + dataSize).order(ByteOrder.LITTLE_ENDIAN).apply {
            put("RIFF".toByteArray()); putInt(36 + dataSize); put("WAVEfmt ".toByteArray())
            putInt(16); putShort(1); putShort(1); putInt(8_000); putInt(16_000); putShort(2); putShort(16)
            put("data".toByteArray()); putInt(dataSize)
        }.array()
    }
}
