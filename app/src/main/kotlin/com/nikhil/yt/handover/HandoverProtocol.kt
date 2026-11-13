package com.nikhil.yt.handover

import com.nikhil.yt.models.MediaMetadata
import org.json.JSONArray
import org.json.JSONObject

internal const val HANDOVER_SERVICE_TYPE = "_velune-player._tcp."

data class HandoverDevice(val name: String, val host: String, val port: Int, val id: String)

internal fun MediaMetadata.toHandoverJson() = JSONObject().apply {
    put("id", id)
    put("title", title)
    put("artists", JSONArray(artists.map { JSONObject().put("id", it.id).put("name", it.name) }))
    put("duration", duration)
    put("explicit", explicit)
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
        explicit = optBoolean("explicit"),
        thumbnailUrl = optString("thumbnail").takeIf { it.isNotBlank() && it != "null" },
        album = albumId?.let { MediaMetadata.Album(it, optString("albumTitle")) },
    )
}

internal fun HandoverPlayback.toSessionJson(includeQueue: Boolean = true) = JSONObject().apply {
    put("sessionId", sessionId); put("rendererId", rendererId)
    put("epoch", epoch); put("revision", revision); put("queueRevision", queueRevision)
    put("index", index); put("position", positionMs); put("duration", durationMs)
    put("playing", playing); put("playbackState", playbackState)
    put("queueTitle", queueTitle); put("repeat", repeatMode); put("shuffle", shuffle); put("speed", speed)
    put("song", song?.toHandoverJson())
    put("deviceVolume", JSONObject().put("level", deviceVolume.level).put("min", deviceVolume.min)
        .put("max", deviceVolume.max).put("muted", deviceVolume.muted).put("fixed", deviceVolume.fixed))
    if (includeQueue) {
        put("queue", JSONArray(queue.map { it.toHandoverJson() }))
        put("order", JSONArray(order))
    }
}

internal fun JSONObject.sessionState(base: HandoverPlayback? = null): HandoverPlayback {
    val items = optJSONArray("queue")
    val queue = if (items != null) {
        require(items.length() <= 20000)
        (0 until items.length()).map { items.getJSONObject(it).handoverSong() }
    } else base?.queue ?: emptyList()
    val ordering = optJSONArray("order")
    val order = if (ordering != null) (0 until ordering.length()).map { ordering.getInt(it) }
        else base?.order ?: queue.indices.toList()
    require(order.size == queue.size && order.toSet() == queue.indices.toSet())
    return HandoverPlayback(
        queue = queue, index = optInt("index", -1).coerceIn(-1, (queue.size - 1).coerceAtLeast(-1)),
        positionMs = optLong("position").coerceAtLeast(0), durationMs = optLong("duration").coerceAtLeast(0),
        playing = optBoolean("playing"),
        queueTitle = optString("queueTitle").takeIf { it.isNotBlank() && it != "null" },
        repeatMode = optInt("repeat").coerceIn(0, 2), shuffle = optBoolean("shuffle"), order = order,
        speed = optDouble("speed", 1.0).toFloat().coerceIn(.25f, 4f),
        sessionId = optString("sessionId"), rendererId = optString("rendererId"),
        epoch = optLong("epoch"), revision = optLong("revision"), queueRevision = optLong("queueRevision"),
        playbackState = optInt("playbackState", 3),
        deviceVolume = optJSONObject("deviceVolume")?.let {
            val min = it.optInt("min").coerceAtLeast(0)
            val max = it.optInt("max").coerceAtLeast(min)
            HandoverVolume(it.optInt("level").coerceIn(min, max), min, max,
                it.optBoolean("muted"), it.optBoolean("fixed"))
        } ?: base?.deviceVolume ?: HandoverVolume(),
    )
}

internal fun playbackOrder(player: androidx.media3.common.Player): List<Int> {
    val timeline = player.currentTimeline
    val order = mutableListOf<Int>()
    var index = timeline.getFirstWindowIndex(true)
    while (index >= 0 && order.size < player.mediaItemCount) {
        order.add(index)
        index = timeline.getNextWindowIndex(index, androidx.media3.common.Player.REPEAT_MODE_OFF, true)
    }
    return order
}

/** Release acknowledgement must carry the session envelope, not only the player snapshot. */
internal fun HandoverPlayback.releaseAcknowledgement(transferId: String, snapshot: HandoverPlayback) =
    JSONObject().put("type", "released").put("sessionId", sessionId).put("epoch", epoch)
        .put("transferId", transferId).put("state", snapshot.toSessionJson())

internal fun HandoverPlayback.acceptsEnvelope(message: JSONObject): Boolean =
    message.optString("sessionId") == sessionId && message.optLong("epoch") == epoch
