/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.playback

import android.Manifest
import android.content.ContentUris
import android.content.ContentValues
import android.content.Context
import android.content.pm.PackageManager
import android.net.Uri
import android.os.Build
import android.os.Environment
import android.provider.MediaStore
import androidx.core.content.ContextCompat
import androidx.core.net.toUri
import dagger.hilt.android.qualifiers.ApplicationContext
import org.jaudiotagger.kt.AudioFormat
import org.jaudiotagger.kt.AudioTagger
import org.jaudiotagger.kt.io.FileChannelIo
import org.jaudiotagger.kt.io.FileDescriptorIo
import org.jaudiotagger.kt.tag.FieldKey
import timber.log.Timber
import java.io.File
import java.io.IOException
import java.io.RandomAccessFile
import javax.inject.Inject
import javax.inject.Singleton

/**
 * Keeps track of the songs that were converted to mp3 and saved in the Music/Velune folder.
 * The index (videoId -> file uri) is stored in SharedPreferences.
 */
@Singleton
class Mp3Library
@Inject
constructor(
    @ApplicationContext private val context: Context,
) {
    private val prefs = context.getSharedPreferences("mp3_library", Context.MODE_PRIVATE)

    fun ids(): Set<String> = prefs.all.keys.toSet()

    fun has(id: String): Boolean = prefs.contains(id)

    /** Number of songs saved as mp3 and the space they take on the phone. */
    fun stats(): Pair<Int, Long> {
        var count = 0
        var bytes = 0L
        prefs.all.values.forEach { value ->
            val stored = value as? String ?: return@forEach
            count++
            bytes += sizeOf(stored.toUri())
        }
        return count to bytes
    }

    private fun sizeOf(uri: Uri): Long =
        runCatching {
            if (uri.scheme == "file") {
                File(requireNotNull(uri.path)).length()
            } else {
                context.contentResolver
                    .query(uri, arrayOf(MediaStore.MediaColumns.SIZE), null, null, null)
                    ?.use { if (it.moveToFirst()) it.getLong(0) else 0L } ?: 0L
            }
        }.getOrDefault(0L)

    /** Returns the uri of the mp3 if it still exists, otherwise forgets it and returns null. */
    fun playableUri(id: String): Uri? {
        val stored = prefs.getString(id, null) ?: return null
        val uri = stored.toUri()
        val exists = runCatching {
            context.contentResolver.openAssetFileDescriptor(uri, "r")?.use { it.length != 0L } ?: false
        }.getOrDefault(false)
        if (!exists) {
            Timber.tag(TAG).w("mp3 file for $id is missing, falling back to streaming")
            prefs.edit().remove(id).apply()
            return null
        }
        return uri
    }

    /**
     * Looks in Music/Velune for mp3 files made by this app and adds the ones the app
     * does not know yet (for example after uninstalling and reinstalling).
     * The video id is stored inside each mp3 (in its tags), so the file name can be anything.
     * After a reinstall this needs the audio permission, otherwise Android hides the files.
     */
    fun restoreFromFolder(): Set<String> {
        val knownUris = prefs.all.values.mapNotNull { it as? String }.toSet()
        val found = runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) findInMediaStore(knownUris) else findInAppMusicDir(knownUris)
        }.onFailure { Timber.tag(TAG).w(it, "Scanning the music folder failed") }.getOrDefault(emptyMap())

        val added = mutableSetOf<String>()
        val editor = prefs.edit()
        found.forEach { (id, uri) ->
            if (!prefs.contains(id)) {
                editor.putString(id, uri.toString())
                added += id
            }
        }
        editor.commit()
        if (added.isNotEmpty()) Timber.tag(TAG).i("Restored ${added.size} mp3 songs from the music folder")
        return added
    }

    fun hasAudioPermission(): Boolean =
        when {
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU ->
                ContextCompat.checkSelfPermission(context, Manifest.permission.READ_MEDIA_AUDIO) == PackageManager.PERMISSION_GRANTED
            Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q ->
                ContextCompat.checkSelfPermission(context, Manifest.permission.READ_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED
            else -> true
        }

    private fun findInMediaStore(knownUris: Set<String>): Map<String, Uri> {
        val result = mutableMapOf<String, Uri>()
        val collection = MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        val projection = arrayOf(MediaStore.Audio.Media._ID)
        val selection = "${MediaStore.Audio.Media.RELATIVE_PATH} LIKE ?"
        val args = arrayOf("${Environment.DIRECTORY_MUSIC}/$FOLDER_NAME/%")
        context.contentResolver.query(collection, projection, selection, args, null)?.use { cursor ->
            val idColumn = cursor.getColumnIndexOrThrow(MediaStore.Audio.Media._ID)
            while (cursor.moveToNext()) {
                val uri = ContentUris.withAppendedId(collection, cursor.getLong(idColumn))
                if (uri.toString() in knownUris) continue // already known, no need to read it again
                val videoId = readVideoId(uri) ?: continue
                result[videoId] = uri
            }
        }
        return result
    }

    private fun findInAppMusicDir(knownUris: Set<String>): Map<String, Uri> {
        val dir = context.getExternalFilesDir(Environment.DIRECTORY_MUSIC)?.resolve(FOLDER_NAME) ?: return emptyMap()
        val result = mutableMapOf<String, Uri>()
        dir.listFiles { file -> file.extension.equals("mp3", ignoreCase = true) }?.forEach { file ->
            val uri = Uri.fromFile(file)
            if (uri.toString() in knownUris) return@forEach
            val videoId = readVideoId(uri) ?: return@forEach
            result[videoId] = uri
        }
        return result
    }

    /** Reads the video id that was written inside the mp3 by [writeVideoId]. */
    private fun readVideoId(uri: Uri): String? =
        runCatching {
            context.contentResolver.openFileDescriptor(uri, "r")?.use { pfd ->
                val io = FileDescriptorIo(pfd.fileDescriptor)
                val raw = AudioTagger.read(io, AudioFormat.MP3).tag.first(FieldKey.CUSTOM1)?.trim()
                if (raw != null && raw.startsWith(ID_PREFIX)) {
                    raw.removePrefix(ID_PREFIX).takeIf { VIDEO_ID_REGEX.matches(it) }
                } else {
                    null
                }
            }
        }.getOrNull()

    /** Writes the video id inside the mp3 (before it is saved in the music folder). */
    fun writeVideoId(file: File, id: String) {
        runCatching {
            RandomAccessFile(file, "rw").use { raf ->
                val io = FileChannelIo(raf.channel)
                val audio = AudioTagger.read(io, AudioFormat.MP3)
                audio.tag.set(FieldKey.CUSTOM1, ID_PREFIX + id)
                AudioTagger.write(io, audio.tag, AudioFormat.MP3)
            }
        }.onFailure { Timber.tag(TAG).w(it, "Could not write the video id inside the mp3 of $id") }
    }

    fun save(id: String, displayName: String, source: File): Uri? {
        val safeName = sanitize(displayName).take(100) + ".mp3"
        val uri = runCatching {
            if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.Q) {
                saveToMediaStore(safeName, source)
            } else {
                saveToAppMusicDir(safeName, source)
            }
        }.onFailure { Timber.tag(TAG).e(it, "Saving mp3 failed for $id") }.getOrNull()
        if (uri != null) prefs.edit().putString(id, uri.toString()).commit()
        return uri
    }

    /**
     * Forgets the song and deletes its mp3 file.
     * Returns the file uri when Android refuses the delete because this app does not own the file
     * (for example a file made before a reinstall): the user has to allow it, see [DownloadUtil].
     */
    fun delete(id: String): Uri? {
        val stored = prefs.getString(id, null) ?: return null
        prefs.edit().remove(id).apply()
        val uri = stored.toUri()
        return try {
            if (uri.scheme == "file") {
                File(requireNotNull(uri.path)).delete()
            } else {
                val rows = context.contentResolver.delete(uri, null, null)
                if (rows == 0) Timber.tag(TAG).w("Nothing was deleted for $id ($uri)")
            }
            null
        } catch (e: SecurityException) {
            Timber.tag(TAG).w(e, "No permission to delete the mp3 of $id, asking the user")
            uri
        } catch (e: Exception) {
            Timber.tag(TAG).w(e, "Could not delete the mp3 of $id")
            null
        }
    }

    private fun saveToMediaStore(fileName: String, source: File): Uri? {
        val resolver = context.contentResolver
        val values = ContentValues().apply {
            put(MediaStore.Audio.Media.DISPLAY_NAME, fileName)
            put(MediaStore.Audio.Media.MIME_TYPE, "audio/mpeg")
            put(MediaStore.Audio.Media.RELATIVE_PATH, "${Environment.DIRECTORY_MUSIC}/$FOLDER_NAME")
            put(MediaStore.Audio.Media.IS_PENDING, 1)
        }
        val collection = MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
        val uri = resolver.insert(collection, values) ?: return null
        try {
            val out = resolver.openOutputStream(uri, "w") ?: throw IOException("No output stream")
            out.use { stream -> source.inputStream().use { it.copyTo(stream) } }
            values.clear()
            values.put(MediaStore.Audio.Media.IS_PENDING, 0)
            resolver.update(uri, values, null, null)
            return uri
        } catch (e: Exception) {
            runCatching { resolver.delete(uri, null, null) }
            throw e
        }
    }

    // Android 8 and 9: no scoped storage, use the app's own music folder (needs no permission).
    private fun saveToAppMusicDir(fileName: String, source: File): Uri? {
        val dir = context.getExternalFilesDir(Environment.DIRECTORY_MUSIC) ?: return null
        val target = File(dir, "$FOLDER_NAME/$fileName").apply { parentFile?.mkdirs() }
        source.copyTo(target, overwrite = true)
        return Uri.fromFile(target)
    }

    private fun sanitize(name: String): String =
        name.replace(Regex("[\\\\/:*?\"<>|\\[\\]]"), "_").trim().ifBlank { "song" }

    companion object {
        private const val TAG = "Mp3Library"
        const val FOLDER_NAME = "Velune"
        private const val ID_PREFIX = "velune:"
        private val VIDEO_ID_REGEX = Regex("[A-Za-z0-9_-]{11}")
    }
}
