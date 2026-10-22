/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.utils

import android.content.Context
import com.nikhil.yt.innertube.models.Album
import com.nikhil.yt.innertube.models.Artist
import com.nikhil.yt.innertube.models.SongItem
import org.json.JSONArray
import org.json.JSONObject
import java.io.File

/**
 * Keeps the last Quick picks on disk so the list shows right away when the app opens,
 * before the network answers.
 */
object QuickPicksCache {
    private const val FILE_NAME = "quick_picks_cache.json"

    private fun file(context: Context) = File(context.filesDir, FILE_NAME)

    fun save(context: Context, songs: List<SongItem>) {
        try {
            val array = JSONArray()
            songs.forEach { song ->
                array.put(
                    JSONObject().apply {
                        put("id", song.id)
                        put("title", song.title)
                        put("thumbnail", song.thumbnail)
                        put("explicit", song.explicit)
                        put("duration", song.duration ?: -1)
                        put("artists", JSONArray().also { list ->
                            song.artists.forEach { artist ->
                                list.put(JSONObject().apply {
                                    put("name", artist.name)
                                    put("id", artist.id ?: JSONObject.NULL)
                                })
                            }
                        })
                        song.album?.let {
                            put("album", JSONObject().apply {
                                put("name", it.name)
                                put("id", it.id)
                            })
                        }
                    }
                )
            }
            val target = file(context)
            val temp = File(target.parentFile, "$FILE_NAME.tmp")
            temp.writeText(array.toString())
            if (!temp.renameTo(target)) {
                target.writeText(array.toString())
                temp.delete()
            }
        } catch (_: Exception) {
        }
    }

    fun load(context: Context): List<SongItem>? {
        return try {
            val target = file(context)
            if (!target.exists()) return null
            val array = JSONArray(target.readText())
            val result = ArrayList<SongItem>(array.length())
            for (i in 0 until array.length()) {
                val obj = array.getJSONObject(i)
                val artistsJson = obj.optJSONArray("artists") ?: JSONArray()
                val artists = (0 until artistsJson.length()).map { index ->
                    val artist = artistsJson.getJSONObject(index)
                    Artist(
                        name = artist.getString("name"),
                        id = if (artist.isNull("id")) null else artist.getString("id"),
                    )
                }
                val album = obj.optJSONObject("album")?.let {
                    Album(name = it.getString("name"), id = it.getString("id"))
                }
                val duration = obj.optInt("duration", -1)
                result += SongItem(
                    id = obj.getString("id"),
                    title = obj.getString("title"),
                    artists = artists,
                    album = album,
                    duration = if (duration >= 0) duration else null,
                    thumbnail = obj.getString("thumbnail"),
                    explicit = obj.optBoolean("explicit", false),
                )
            }
            result
        } catch (_: Exception) {
            null
        }
    }
}
