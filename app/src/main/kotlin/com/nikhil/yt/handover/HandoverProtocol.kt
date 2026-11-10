package com.nikhil.yt.handover

import com.nikhil.yt.models.MediaMetadata
import org.json.JSONArray
import org.json.JSONObject

internal const val HANDOVER_SERVICE_TYPE = "_velune-player._tcp."

data class HandoverDevice(val name: String, val host: String, val port: Int, val token: String)

data class HandoverPlayback(
    val song: MediaMetadata?,
    val positionMs: Long,
    val durationMs: Long,
    val playing: Boolean,
)

internal fun MediaMetadata.toHandoverJson() = JSONObject().apply {
    put("id", id)
    put("title", title)
    put("artists", JSONArray(artists.map { JSONObject().put("id", it.id).put("name", it.name) }))
    put("duration", duration)
    put("thumbnail", thumbnailUrl)
    put("albumId", album?.id)
    put("albumTitle", album?.title)
}

internal fun JSONObject.handoverSong(): MediaMetadata {
    val artistsJson = getJSONArray("artists")
    val artists = (0 until artistsJson.length()).map { index ->
        val artist = artistsJson.getJSONObject(index)
        MediaMetadata.Artist(artist.optString("id").takeIf { it.isNotBlank() && it != "null" }, artist.getString("name"))
    }
    val albumId = optString("albumId").takeIf { it.isNotBlank() && it != "null" }
    return MediaMetadata(
        id = getString("id"),
        title = getString("title"),
        artists = artists,
        duration = optInt("duration", -1),
        thumbnailUrl = optString("thumbnail").takeIf { it.isNotBlank() && it != "null" },
        album = albumId?.let { MediaMetadata.Album(it, optString("albumTitle")) },
    )
}
