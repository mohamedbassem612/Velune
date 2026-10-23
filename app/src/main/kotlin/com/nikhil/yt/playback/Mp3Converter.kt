/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.playback

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.core.net.toUri
import androidx.media3.common.util.UnstableApi
import androidx.media3.datasource.DataSourceInputStream
import androidx.media3.datasource.DataSpec
import androidx.media3.datasource.cache.Cache
import androidx.media3.datasource.cache.CacheDataSource
import com.arthenica.ffmpegkit.FFmpegKit
import com.arthenica.ffmpegkit.ReturnCode
import com.nikhil.yt.db.MusicDatabase
import com.nikhil.yt.di.DownloadCache
import dagger.hilt.android.qualifiers.ApplicationContext
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.asCoroutineDispatcher
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.launch
import kotlinx.coroutines.runBlocking
import okhttp3.OkHttpClient
import okhttp3.Request
import timber.log.Timber
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.Executors
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Converts a finished download (sitting in the download cache) to mp3 in the background,
 * saves it in Music/Velune, then removes the old copy from the download cache.
 * Songs are converted one at a time.
 */
@UnstableApi
@Singleton
class Mp3Converter
@Inject
constructor(
    @ApplicationContext private val context: Context,
    private val database: MusicDatabase,
    @DownloadCache private val downloadCache: Cache,
    private val mp3Library: Mp3Library,
) {
    private val scope = CoroutineScope(
        SupervisorJob() + Executors.newSingleThreadExecutor { r ->
            Thread(r, "Mp3Converter").apply { priority = Thread.MIN_PRIORITY }
        }.asCoroutineDispatcher()
    )
    private val queued = ConcurrentHashMap.newKeySet<String>()
    private val httpClient by lazy { OkHttpClient() }

    fun enqueue(id: String) {
        if (mp3Library.has(id) || !queued.add(id)) return
        scope.launch {
            try {
                convert(id)
            } catch (e: CancellationException) {
                throw e
            } catch (e: Throwable) {
                Timber.tag(TAG).e(e, "Conversion failed for $id")
            } finally {
                queued.remove(id)
            }
        }
    }

    private fun convert(id: String) {
        val hasData = runCatching { downloadCache.getCachedSpans(id).isNotEmpty() }.getOrDefault(false)
        if (!hasData) return

        val workDir = File(context.cacheDir, "mp3_convert").apply { mkdirs() }
        val input = File(workDir, "$id.src")
        val output = File(workDir, "$id.mp3")
        val cover = File(workDir, "$id.cover")
        try {
            copyFromCache(id, input)

            val song = database.getSongByIdBlocking(id)
            val title = song?.song?.title?.takeIf { it.isNotBlank() } ?: id
            val artist = song?.artists?.joinToString(", ") { it.name }.orEmpty()
            val album = song?.song?.albumName.orEmpty()
            val albumId = song?.song?.albumId ?: song?.album?.id
            val albumInfo = albumId?.let { runCatching { runBlocking { database.album(it).first() } }.getOrNull() }
            val albumArtist = (albumInfo?.artists ?: song?.artists)?.firstOrNull()?.name.orEmpty()
            val year = song?.song?.year ?: albumInfo?.album?.year ?: song?.album?.year
            val coverUrl = song?.song?.thumbnailUrl ?: albumInfo?.album?.thumbnailUrl
            val hasCover = coverUrl?.let { downloadCover(it, cover) } == true
            val info = TrackInfo(title, artist, album, albumArtist, year)

            var ok = runFfmpeg(input, if (hasCover) cover else null, output, info)
            if (!ok && hasCover) {
                output.delete()
                ok = runFfmpeg(input, null, output, info)
            }
            if (!ok || output.length() <= 0L) {
                Timber.tag(TAG).e("ffmpeg failed for $id, keeping the original download")
                return
            }

            // Keeps the video id inside the file so the app can recognize it again after a reinstall
            mp3Library.writeVideoId(output, id)

            val displayName = if (artist.isNotBlank()) "$artist - $title" else title
            val saved = mp3Library.save(id, displayName, output)
            if (saved == null) {
                Timber.tag(TAG).e("Could not save mp3 for $id, keeping the original download")
                return
            }

            runCatching { downloadCache.removeResource(id) }
            Timber.tag(TAG).i("Converted $id to mp3 and removed it from the download cache")
        } finally {
            input.delete()
            output.delete()
            cover.delete()
        }
    }

    private fun copyFromCache(id: String, target: File) {
        // No upstream: reads only what is already in the download cache and fails if something is missing.
        val dataSource = CacheDataSource
            .Factory()
            .setCache(downloadCache)
            .setCacheWriteDataSinkFactory(null)
            .createDataSource()
        val spec = DataSpec.Builder()
            .setUri("velune://song/$id".toUri())
            .setKey(id)
            .build()
        DataSourceInputStream(dataSource, spec).use { source ->
            target.outputStream().use { out -> source.copyTo(out) }
        }
    }

    /**
     * Downloads the cover, makes it a square JPEG (any source format, including webp)
     * and saves it in [target]. Tries a bigger version of the image first.
     */
    private fun downloadCover(url: String, target: File): Boolean {
        val candidates = listOf(biggerCoverUrl(url), url).distinct()
        for (candidate in candidates) {
            val bytes = runCatching {
                httpClient.newCall(Request.Builder().url(candidate).build()).execute().use { response ->
                    if (response.isSuccessful) response.body?.bytes() else null
                }
            }.getOrNull() ?: continue

            val bitmap = runCatching { BitmapFactory.decodeByteArray(bytes, 0, bytes.size) }.getOrNull() ?: continue
            val square = toSquare(bitmap)
            val saved = runCatching {
                target.outputStream().use { square.compress(Bitmap.CompressFormat.JPEG, COVER_QUALITY, it) }
            }.getOrDefault(false)
            if (square !== bitmap) bitmap.recycle()
            square.recycle()
            if (saved && target.length() > 0L) return true
        }
        return false
    }

    // Google image links end with the size, for example "=w544-h544-l90-rj"
    private fun biggerCoverUrl(url: String): String =
        COVER_SIZE_REGEX.replace(url, "=w$COVER_SIZE-h$COVER_SIZE-l90-rj")

    private fun toSquare(source: Bitmap): Bitmap {
        val side = minOf(source.width, source.height)
        val x = (source.width - side) / 2
        val y = (source.height - side) / 2
        val cropped = if (side == source.width && side == source.height) source else Bitmap.createBitmap(source, x, y, side, side)
        if (cropped.width <= COVER_SIZE) return cropped
        val scaled = Bitmap.createScaledBitmap(cropped, COVER_SIZE, COVER_SIZE, true)
        if (cropped !== source) cropped.recycle()
        return scaled
    }

    private data class TrackInfo(
        val title: String,
        val artist: String,
        val album: String,
        val albumArtist: String,
        val year: Int?,
    )

    private fun runFfmpeg(
        input: File,
        cover: File?,
        output: File,
        info: TrackInfo,
    ): Boolean {
        val args = mutableListOf("-y", "-i", input.absolutePath)
        if (cover != null) {
            args += listOf("-i", cover.absolutePath, "-map", "0:a:0", "-map", "1:v:0", "-c:v", "copy")
            args += listOf("-metadata:s:v", "title=Album cover", "-metadata:s:v", "comment=Cover (front)")
        } else {
            args += listOf("-vn", "-map", "0:a:0")
        }
        args += listOf("-c:a", "libmp3lame", "-q:a", "2", "-id3v2_version", "3")
        args += listOf("-metadata", "title=${info.title}")
        if (info.artist.isNotBlank()) args += listOf("-metadata", "artist=${info.artist}")
        if (info.album.isNotBlank()) args += listOf("-metadata", "album=${info.album}")
        if (info.albumArtist.isNotBlank()) args += listOf("-metadata", "album_artist=${info.albumArtist}")
        if (info.year != null && info.year > 0) args += listOf("-metadata", "date=${info.year}")
        args += "-f"
        args += "mp3"
        args += output.absolutePath

        val session = FFmpegKit.executeWithArguments(args.toTypedArray())
        val success = ReturnCode.isSuccess(session.returnCode)
        if (!success) {
            Timber.tag(TAG).w("ffmpeg exit=${session.returnCode} ${session.failStackTrace.orEmpty()}")
        }
        return success
    }

    companion object {
        private const val TAG = "Mp3Converter"
        private const val COVER_SIZE = 1000
        private const val COVER_QUALITY = 92
        private val COVER_SIZE_REGEX = Regex("=[ws]\\d+(-h\\d+)?(-[A-Za-z0-9-]+)*$")
    }
}
