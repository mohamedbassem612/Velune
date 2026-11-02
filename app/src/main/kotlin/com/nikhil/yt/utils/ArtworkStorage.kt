/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */



package com.nikhil.yt.utils

import android.content.Context
import kotlinx.serialization.Serializable
import kotlinx.serialization.json.Json
import kotlinx.serialization.decodeFromString
import kotlinx.serialization.encodeToString
import java.io.File

@Serializable
data class SavedArtwork(
    val songId: String,
    val thumbnail: String? = null,
    val artist: String? = null
)

object ArtworkStorage {
    private const val FILENAME = "velune_saved_artworks.json"
    private val json = Json { prettyPrint = true; ignoreUnknownKeys = true }

    private fun fileFor(context: Context) = android.util.AtomicFile(File(context.filesDir, FILENAME))

    private fun read(context: Context): List<SavedArtwork> {
        val file = fileFor(context)
        if (!file.baseFile.exists() && !File(file.baseFile.path + ".bak").exists()) return emptyList()
        return json.decodeFromString(file.openRead().bufferedReader().use { it.readText() })
    }

    private fun write(context: Context, items: List<SavedArtwork>) {
        val bytes = json.encodeToString(items).toByteArray(Charsets.UTF_8)
        val file = fileFor(context)
        val stream = file.startWrite()
        try {
            stream.write(bytes)
            file.finishWrite(stream)
        } catch (e: Exception) {
            file.failWrite(stream)
            throw e
        }
    }

    @Synchronized
    fun loadAll(context: Context): List<SavedArtwork> = try {
        read(context)
    } catch (e: Exception) {
        timber.log.Timber.w(e, "Could not read saved artwork metadata")
        emptyList()
    }

    fun findBySongId(context: Context, songId: String): SavedArtwork? =
        loadAll(context).firstOrNull { it.songId == songId }

    @Synchronized
    fun saveOrUpdate(context: Context, artwork: SavedArtwork) {
        try {
            // Do not replace a corrupt index with a single new entry.
            val items = read(context).filterNot { it.songId == artwork.songId } + artwork
            write(context, items)
        } catch (e: Exception) {
            timber.log.Timber.w(e, "Could not save artwork metadata")
        }
    }

    @Synchronized
    fun clear(context: Context) {
        try {
            write(context, emptyList())
        } catch (e: Exception) {
            timber.log.Timber.w(e, "Could not clear artwork metadata")
        }
    }

    @Synchronized
    fun removeBySongId(context: Context, songId: String) {
        try {
            write(context, read(context).filterNot { it.songId == songId })
        } catch (e: Exception) {
            timber.log.Timber.w(e, "Could not remove artwork metadata")
        }
    }
}
