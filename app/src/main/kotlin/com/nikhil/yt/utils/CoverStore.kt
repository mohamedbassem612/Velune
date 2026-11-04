/*
 * Velune - covers that survive Android clearing the image cache.
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.utils

import android.content.Context
import android.graphics.Bitmap
import android.media.MediaMetadataRetriever
import android.net.Uri
import android.util.LruCache
import coil3.imageLoader
import coil3.request.ImageRequest
import coil3.request.SuccessResult
import coil3.request.allowHardware
import coil3.size.Size
import coil3.toBitmap
import com.nikhil.yt.innertube.toHighResThumbnail
import com.nikhil.yt.playback.Mp3Library
import timber.log.Timber
import java.io.File
import java.util.concurrent.ConcurrentHashMap
import java.util.concurrent.CopyOnWriteArrayList

/** Raw cover bytes (read from inside an mp3) handed to Coil. */
class CoverBytes(val bytes: ByteArray)

/**
 * Where a song cover comes from, in this order:
 * 1. downloaded song: the cover stored inside the mp3 (ID3)
 * 2. song in the player cache: a permanent copy in filesDir/covers named by videoId
 * 3. anything else: nothing here, Coil loads the normal thumbnail url
 *
 * A small index (videoId -> thumbnail url) lets the image loader find the local cover when the
 * UI only knows the url.
 */
object CoverStore {
    private const val TAG = "CoverStore"
    private const val DIR = "covers"
    private const val INDEX_PREFS = "cover_index"
    private const val MAX_SIDE = 544

    private val embeddedCache = object : LruCache<String, CoverBytes>(8 * 1024 * 1024) {
        override fun sizeOf(key: String, value: CoverBytes): Int = value.bytes.size
    }
    private val noEmbedded = ConcurrentHashMap.newKeySet<String>()
    private val saving = ConcurrentHashMap.newKeySet<String>()

    private val idToUrl = ConcurrentHashMap<String, String>()
    private val urlToIds = ConcurrentHashMap<String, CopyOnWriteArrayList<String>>()

    @Volatile
    private var loaded = false

    private fun prefs(context: Context) =
        context.applicationContext.getSharedPreferences(INDEX_PREFS, Context.MODE_PRIVATE)

    @Synchronized
    private fun ensureLoaded(context: Context) {
        if (loaded) return
        prefs(context).all.forEach { (id, url) ->
            if (url is String) link(id, url)
        }
        loaded = true
    }

    /** The url and its high resolution version (the Home carousel asks for that one). */
    private fun urlsOf(url: String): List<String> =
        listOf(url, runCatching { url.toHighResThumbnail() }.getOrDefault(url)).distinct()

    private fun link(id: String, url: String) {
        val old = idToUrl.put(id, url)
        if (old != null && old != url) removeFromUrls(id, old)
        urlsOf(url).forEach { u ->
            val list = urlToIds.getOrPut(u) { CopyOnWriteArrayList() }
            if (!list.contains(id)) list.add(id)
        }
    }

    private fun removeFromUrls(id: String, url: String) {
        urlsOf(url).forEach { u ->
            urlToIds[u]?.let {
                it.remove(id)
                if (it.isEmpty()) urlToIds.remove(u)
            }
        }
    }

    private fun unlink(id: String) {
        val url = idToUrl.remove(id) ?: return
        removeFromUrls(id, url)
    }

    fun idsForUrl(context: Context, url: String): List<String> {
        ensureLoaded(context)
        return urlToIds[url]?.toList().orEmpty()
    }

    fun register(context: Context, id: String, url: String) {
        ensureLoaded(context)
        if (idToUrl[id] == url) return
        link(id, url)
        prefs(context).edit().putString(id, url).apply()
    }

    private fun dir(context: Context) = File(context.filesDir, DIR)

    fun coverFile(context: Context, id: String) = File(dir(context), "$id.jpg")

    fun hasSavedCover(context: Context, id: String) = coverFile(context, id).length() > 0L

    /** Local source for this song, or null when Coil should use the normal url. Call off the main thread. */
    fun localSource(context: Context, id: String, mp3: Mp3Library?): Any? {
        if (mp3 != null && mp3.has(id)) {
            embeddedCache.get(id)?.let { return it }
            if (!noEmbedded.contains(id)) {
                val uri = mp3.playableUri(id)
                val bytes = uri?.let { readEmbedded(context, it) }
                if (bytes != null) {
                    val cover = CoverBytes(bytes)
                    embeddedCache.put(id, cover)
                    return cover
                }
                if (uri != null) noEmbedded.add(id)
            }
        }
        val file = coverFile(context, id)
        return if (file.length() > 0L) file else null
    }

    private fun readEmbedded(context: Context, uri: Uri): ByteArray? {
        val retriever = MediaMetadataRetriever()
        return try {
            retriever.setDataSource(context, uri)
            retriever.embeddedPicture
        } catch (e: Exception) {
            Timber.tag(TAG).w(e, "Could not read the cover inside the mp3")
            null
        } finally {
            runCatching { retriever.release() }
        }
    }

    /** Saves a permanent copy of the cover (about 544px JPEG) and remembers its url. */
    suspend fun saveFromUrl(context: Context, id: String, url: String): Boolean {
        val file = coverFile(context, id)
        if (file.length() > 0L) {
            register(context, id, url)
            return true
        }
        if (!saving.add(id)) return false
        return try {
            val request = ImageRequest.Builder(context)
                .data(url)
                .size(Size(MAX_SIDE, MAX_SIDE))
                .allowHardware(false)
                .build()
            val result = context.imageLoader.execute(request) as? SuccessResult ?: return false
            val bitmap = result.image.toBitmap()
            file.parentFile?.mkdirs()
            val tmp = File(file.parentFile, "$id.tmp")
            tmp.outputStream().use { bitmap.compress(Bitmap.CompressFormat.JPEG, 90, it) }
            if (tmp.length() > 0L && tmp.renameTo(file)) {
                register(context, id, url)
                true
            } else {
                tmp.delete()
                false
            }
        } catch (e: Exception) {
            Timber.tag(TAG).w(e, "Could not save cover for $id")
            false
        } finally {
            saving.remove(id)
        }
    }

    /**
     * Deletes saved covers of songs that left the player cache, and forgets index entries of songs
     * that are neither cached nor downloaded.
     */
    fun sweep(context: Context, cachedIds: Set<String>, knownIds: Set<String>) {
        ensureLoaded(context)
        dir(context).listFiles()?.forEach { f ->
            val id = f.name.substringBeforeLast('.')
            if (f.name.endsWith(".tmp") || id !in cachedIds) f.delete()
        }
        val stale = idToUrl.keys.filter { it !in knownIds }
        if (stale.isNotEmpty()) {
            val editor = prefs(context).edit()
            stale.forEach {
                unlink(it)
                editor.remove(it)
                embeddedCache.remove(it)
                noEmbedded.remove(it)
            }
            editor.apply()
        }
    }
}
