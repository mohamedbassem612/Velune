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
import java.time.LocalDateTime
import java.util.concurrent.CountDownLatch
import java.util.concurrent.TimeUnit
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.flow.first
import org.junit.Assert.*
import org.junit.Test
import org.junit.runner.RunWith

/** Run on a disposable emulator. A fully cached silent MP3 keeps this test independent of YouTube. */
@RunWith(AndroidJUnit4::class)
class MusicDesignInstrumentedTest {
    private val instrumentation = InstrumentationRegistry.getInstrumentation()
    private val context = instrumentation.targetContext
    private val automation get() = instrumentation.uiAutomation
    private val title = "ليل هادئ — A quiet evening with a very long song title"

    @Test fun sharedPlaybackSurvivesPlayerSearchThemeAndRotation() = exercisePlayer(true)

    @Test fun metroControlsAndLyricsKeepTheirOwnScreen() = exercisePlayer(false)

    @Test fun defaultFooterMatchesReference() = exercisePlayer(true, footerOnly = true)

    @Test fun metroFooterMatchesReference() = exercisePlayer(false, footerOnly = true)

    @Test fun newMiniPlayerSwipeHasMotionAndSettles() = exercisePlayer(true, miniOnly = true)

    @Test fun classicMiniPlayerSwipeHasMotionAndSettles() = exercisePlayer(true, miniOnly = true, newMiniDesign = false)

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

    private fun exercisePlayer(useMusicPlayer: Boolean, footerOnly: Boolean = false, miniOnly: Boolean = false, newMiniDesign: Boolean = true) {
        val permission = if (android.os.Build.VERSION.SDK_INT >= 33)
            "android.permission.READ_MEDIA_AUDIO" else "android.permission.READ_EXTERNAL_STORAGE"
        automation.grantRuntimePermission(context.packageName, permission)
        if (android.os.Build.VERSION.SDK_INT >= 33)
            automation.grantRuntimePermission(context.packageName, "android.permission.POST_NOTIFICATIONS")
        runBlocking { context.dataStore.edit {
            it[DarkModeKey] = DarkMode.ON.name
            it[PureBlackKey] = true
            it[DynamicThemeKey] = false
            it[UseMusicPlayerKey] = useMusicPlayer
            it[UseNewMiniPlayerDesignKey] = newMiniDesign
            it[SwipeThumbnailKey] = true
            it[SwipeSensitivityKey] = 0.73f
            it[PlayerDesignStyleKey] = PlayerDesignStyle.V5.name
            it[PlayerBackgroundStyleKey] = PlayerBackgroundStyle.DEFAULT.name
            it[DefaultOpenTabKey] = if (miniOnly) NavigationTab.HOME.name else NavigationTab.LIBRARY.name
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
        val audio = silentMp3()
        try {
            tracks.forEach { track ->
                service.database.insert(track)
                service.database.upsert(FormatEntity(track.id, 0, "audio/mpeg", "mp3", 128_000, 44_100, audio.size.toLong(), null, playbackUrl = null))
                service.database.upsert(LyricsEntity(track.id,
                    "[00:00.00]First lyric line\n[00:15.00]Second lyric line\n[00:30.00]Third lyric line"))
                val hole = service.downloadCache.startReadWrite(track.id, 0, audio.size.toLong())
                try {
                    if (!hole.isCached) {
                        val file = service.downloadCache.startFile(track.id, 0, audio.size.toLong())
                        file.writeBytes(audio)
                        service.downloadCache.commitFile(file, audio.size.toLong())
                        service.downloadCache.applyContentMetadataMutations(track.id,
                            ContentMetadataMutations().apply { ContentMetadataMutations.setContentLength(this, audio.size.toLong()) })
                    }
                } finally { if (!hole.isCached) service.downloadCache.releaseHoleSpan(hole) }
            }
            instrumentation.runOnMainSync {
                service.playQueue(ListQueue("UI test", tracks.map { it.toMediaItem() }, position = 17_000))
            }
            await("Cached fixture did not become ready") { main { service.sessionPlayer.playbackState == Player.STATE_READY } }
            instrumentation.runOnMainSync { service.sessionPlayer.pause(); service.sessionPlayer.seekTo(17_000) }
            await("Mini player did not appear") { find(title) != null }
            if (miniOnly) {
                assertMiniPlayerMotion(activity, service, tracks, newMiniDesign)
                return
            }
            if (useMusicPlayer && !footerOnly) {
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
            assertControlPositions(activity)
            SystemClock.sleep(600)
            capture("02-player-amoled")
            if (footerOnly) return
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

    private fun assertMiniPlayerMotion(activity: MainActivity, service: MusicService, tracks: List<MediaMetadata>, newDesign: Boolean) {
        fun labelBounds(label: String, minTop: Int = Int.MIN_VALUE) = Rect().also { rect ->
            find(label, minTop)!!.let { node -> node.refresh(); node.getBoundsInScreen(rect) }
        }
        fun drag(label: String, distanceDp: Float, screenshot: String, expectMotion: Boolean = true) {
            SystemClock.sleep(600)
            val before = labelBounds(label)
            await("Paused mini player control must be visible", 5_000) { find(activity.getString(R.string.play), before.top - 32) != null }
            val playBefore = labelBounds(activity.getString(R.string.play), before.top - 32)
            val distance = distanceDp * context.resources.displayMetrics.density
            val downTime = SystemClock.uptimeMillis()
            fun event(action: Int, x: Float) {
                MotionEvent.obtain(downTime, SystemClock.uptimeMillis(), action, x, before.exactCenterY(), 0).let {
                    it.source = InputDevice.SOURCE_TOUCHSCREEN
                    automation.injectInputEvent(it, true)
                    it.recycle()
                }
            }
            event(MotionEvent.ACTION_DOWN, before.exactCenterX())
            try {
                for (step in 1..8) {
                    event(MotionEvent.ACTION_MOVE, before.exactCenterX() + distance * step / 8)
                    SystemClock.sleep(20)
                }
                android.util.Log.i("MiniGeometry", "new=$newDesign before=$before held=${labelBounds(label)}")
                if (expectMotion) {
                    await("Mini player must visibly follow a horizontal drag", 5_000) {
                        val moved = labelBounds(label).left - before.left
                        kotlin.math.abs(moved) > 4 && moved * distance > 0
                    }
                } else {
                    assertEquals("Disabled swipe gestures must not move the mini player", before.left, labelBounds(label).left)
                }
                val titleOffset = labelBounds(label).left - before.left
                val playOffset = labelBounds(activity.getString(R.string.play), before.top - 32).left - playBefore.left
                assertTrue("Title and controls must move together", kotlin.math.abs(titleOffset - playOffset) <= 2)
                assertTrue("Swipe motion must stay light and bounded", kotlin.math.abs(titleOffset) < 48 * context.resources.displayMetrics.density)
                assertNull("Dragging must not expand the player", find(activity.getString(R.string.music_options)))
                capture("$screenshot-${if (newDesign) "new" else "classic"}")
            } finally {
                event(MotionEvent.ACTION_UP, before.exactCenterX() + distance)
            }
        }
        val restingLeft = labelBounds(title).left
        if (newDesign) assertNull(find(activity.getString(R.string.connect_devices)))
        else assertNotNull("Classic mini player must retain its device control", find(activity.getString(R.string.connect_devices)))
        drag(title, 28f, "09-mini-short-drag")
        await("Short drag must return to its resting position", 5_000) {
            kotlin.math.abs(labelBounds(title).left - restingLeft) <= 2
        }
        android.util.Log.i("MiniGeometry", "new=$newDesign resting=$restingLeft settled=${labelBounds(title)}")
        capture("09-mini-short-settled-${if (newDesign) "new" else "classic"}")
        assertEquals("A short drag must not skip tracks", tracks[0].id, main { service.sessionPlayer.currentMediaItem?.mediaId })
        assertTrue("Short drag must return to its resting position", kotlin.math.abs(labelBounds(title).left - restingLeft) <= 2)
        drag(title, -110f, "10-mini-drag-left")
        await("Left drag must select the next track") { main { service.sessionPlayer.currentMediaItem?.mediaId } == tracks[1].id && find(tracks[1].title) != null }
        instrumentation.runOnMainSync { service.sessionPlayer.pause(); service.sessionPlayer.seekTo(0) }
        await("Next track must settle at the original position", 5_000) {
            kotlin.math.abs(labelBounds(tracks[1].title).left - restingLeft) <= 2
        }
        drag(tracks[1].title, 110f, "11-mini-drag-right")
        await("Right drag must select the previous track") { main { service.sessionPlayer.currentMediaItem?.mediaId } == tracks[0].id && find(title) != null }
        instrumentation.runOnMainSync { service.sessionPlayer.pause() }
        await("Previous track must settle at the original position", 5_000) {
            kotlin.math.abs(labelBounds(title).left - restingLeft) <= 2
        }
        capture("12-mini-settled-${if (newDesign) "new" else "classic"}")
        runBlocking { context.dataStore.edit { it[SwipeThumbnailKey] = false } }
        drag(title, -110f, "13-mini-disabled", expectMotion = false)
        SystemClock.sleep(600)
        assertNull("Disabled horizontal swipes must not become expand taps", find(activity.getString(R.string.music_options)))
        assertEquals("Disabled swipe gestures must not skip tracks", tracks[0].id, main { service.sessionPlayer.currentMediaItem?.mediaId })
        runBlocking { context.dataStore.edit { it[SwipeThumbnailKey] = true } }
        runBlocking { context.dataStore.edit { it[UseNewMiniPlayerDesignKey] = !newDesign } }
        await("Appearance mini player switch must apply without changing the full player design") {
            (find(activity.getString(R.string.connect_devices)) != null) == newDesign
        }
        assertEquals(3, main { service.sessionPlayer.mediaItemCount })
        assertEquals(tracks[0].id, main { service.sessionPlayer.currentMediaItem?.mediaId })
        tap(title)
        await("Tapping either mini player style must open the player") { find(activity.getString(R.string.music_options)) != null }
        assertControlPositions(activity)
    }

    private fun assertControlPositions(activity: MainActivity) {
        fun bounds(resource: Int, minTop: Int = Int.MIN_VALUE, maxTop: Int = Int.MAX_VALUE): Rect {
            val label = activity.getString(resource)
            await("Missing control $label") { find(label, minTop, maxTop) != null }
            var node = find(label, minTop, maxTop)!!
            // A text label can be a child of the button's merged accessibility node.
            while (!node.isClickable) {
                node = node.parent ?: error("No clickable control for $label")
            }
            return Rect().also(node::getBoundsInScreen)
        }
        val devices = bounds(R.string.connect_devices)
        val like = bounds(R.string.action_like)
        assertTrue("Track actions must be devices then like", devices.centerX() < like.centerX())
        assertNull("Share belongs in the options menu", find(activity.getString(R.string.share)))
        val shuffle = bounds(R.string.music_shuffle_off)
        val previous = bounds(R.string.music_previous)
        val play = bounds(R.string.play, previous.top - 64, previous.bottom)
        val next = bounds(R.string.music_next)
        val repeat = bounds(R.string.repeat_mode_off)
        assertTrue("Transport order must match the reference",
            shuffle.centerX() < previous.centerX() && previous.centerX() < play.centerX() &&
                play.centerX() < next.centerX() && next.centerX() < repeat.centerX())
        assertTrue("Track actions must be above transport controls", like.centerY() < previous.top)
        val queue = bounds(R.string.queue)
        val sleep = bounds(R.string.sleep_timer)
        val menu = bounds(R.string.music_options)
        val lyrics = bounds(R.string.lyrics)
        val queueLabel = Rect().also { find(activity.getString(R.string.queue))!!.getBoundsInScreen(it) }
        val lyricsLabel = Rect().also { find(activity.getString(R.string.lyrics))!!.getBoundsInScreen(it) }
        val viewport = Rect().also { automation.rootInActiveWindow!!.getBoundsInScreen(it) }
        android.util.Log.i("PlayerGeometry", "play=${play.centerX()} queueLabel=${queueLabel.left} sleep=${sleep.centerX()} lyricsLabel=${lyricsLabel.left} menu=${menu.centerX()}")
        assertTrue("Footer order must match the supplied reference",
            queue.centerX() < sleep.centerX() && sleep.centerX() < lyrics.centerX() && lyrics.centerX() < menu.centerX())
        assertTrue("Sleep must align with the play button", kotlin.math.abs(sleep.centerX() - play.centerX()) <= 1)
        assertTrue("Queue title must retain its reference position",
            kotlin.math.abs(queueLabel.left - viewport.width() * (281f / 1080f)) <= 2)
        assertTrue("Lyrics title must retain its reference position",
            kotlin.math.abs(lyricsLabel.left - viewport.width() * (763f / 1080f)) <= 2)
        assertTrue("Footer touch targets must not overlap",
            queue.right <= sleep.left && sleep.right <= lyrics.left && lyrics.right <= menu.left)
        assertTrue("Footer controls must share a vertical center",
            listOf(queue, sleep, lyrics, menu).all { kotlin.math.abs(it.centerY() - sleep.centerY()) <= 1 })
        assertTrue("Queue and lyrics must be below transport controls", queue.centerY() > play.bottom)
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

    private fun find(label: String, minTop: Int = Int.MIN_VALUE, maxTop: Int = Int.MAX_VALUE): AccessibilityNodeInfo? {
        val root = automation.rootInActiveWindow ?: return null
        fun walk(node: AccessibilityNodeInfo): AccessibilityNodeInfo? {
            if (!node.refresh()) return null
            if (node.isVisibleToUser && (node.text?.toString() == label || node.contentDescription?.toString() == label)) {
                val bounds = Rect().also(node::getBoundsInScreen)
                if (bounds.top in minTop..maxTop) return node
            }
            // Prefer the player or sheet drawn above Home's own playback buttons.
            for (index in node.childCount - 1 downTo 0) node.getChild(index)?.let { walk(it)?.let { result -> return result } }
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

    private fun await(message: String, timeoutMs: Long = 90_000, condition: () -> Boolean) {
        val deadline = SystemClock.elapsedRealtime() + timeoutMs
        while (SystemClock.elapsedRealtime() < deadline) {
            if (condition()) return
            SystemClock.sleep(250)
        }
        fail(message)
    }

    /** 120 seconds of valid CBR MPEG-1 Layer III silence, supported by the production extractors. */
    private fun silentMp3(): ByteArray {
        val frameSize = 417 // 128 kbps, 44.1 kHz, unpadded MPEG-1 Layer III.
        val frameCount = 4_594 // 1,152 samples per frame.
        return ByteArray(frameSize * frameCount).apply {
            repeat(frameCount) { frame ->
                val offset = frame * frameSize
                this[offset] = 0xFF.toByte()
                this[offset + 1] = 0xFB.toByte()
                this[offset + 2] = 0x90.toByte()
                this[offset + 3] = 0xC0.toByte() // Mono; zeroed side information encodes silent granules.
            }
        }
    }
}
