package com.nikhil.yt.utils

import android.net.Uri
import java.net.HttpURLConnection
import java.net.URL
import java.net.URLEncoder
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import kotlinx.serialization.json.contentOrNull

/**
 * Turns a Spotify link into a search query that can be run on YouTube Music.
 * Spotify has no public unauthenticated API, so the title/artist are read from the
 * og: meta tags of the public share page.
 */
object SpotifyLinkResolver {
    enum class Type { TRACK, ALBUM, ARTIST }

    data class Result(
        val type: Type,
        val title: String,
        val artist: String?,
    ) {
        val query: String get() = listOfNotNull(title, artist).joinToString(" ")
    }

    private val knownTypes = mapOf("track" to Type.TRACK, "album" to Type.ALBUM, "artist" to Type.ARTIST)

    fun isSpotifyUri(uri: Uri): Boolean {
        val host = uri.host?.lowercase()
        return uri.scheme.equals("spotify", true) ||
            host == "open.spotify.com" ||
            host == "spotify.link" ||
            host == "spotify.app.link"
    }

    fun resolve(uri: Uri): Result? {
        val (type, id) = parseTypeAndId(uri) ?: resolveShortLink(uri)?.let { parseTypeAndId(it) } ?: return null
        val pageUrl = "https://open.spotify.com/${type_path(type)}/$id"
        val metadata = fetch(pageUrl)?.let { SpotifyPageMetadata.parse(it) }
        if (metadata != null) {
            return Result(type, metadata.title, if (type == Type.ARTIST) null else metadata.artist)
        }
        // Some share pages serve a shell without Open Graph metadata. oEmbed can still provide a title.
        val embed = fetch("https://open.spotify.com/oembed?url=${URLEncoder.encode(pageUrl, "UTF-8")}") ?: return null
        val embedTitle = runCatching {
            Json.parseToJsonElement(embed).jsonObject["title"]?.jsonPrimitive?.contentOrNull
        }.getOrNull()?.takeIf { it.isNotBlank() } ?: return null
        return Result(type, embedTitle, null)
    }

    private fun type_path(type: Type) = type.name.lowercase()

    private fun parseTypeAndId(uri: Uri): Pair<Type, String>? {
        if (uri.scheme.equals("spotify", true)) {
            // spotify:track:ID
            val parts = uri.schemeSpecificPart.split(":")
            val type = knownTypes[parts.getOrNull(0)] ?: return null
            return parts.getOrNull(1)?.takeIf { it.isNotBlank() }?.let { type to it }
        }
        // open.spotify.com/[intl-xx/]track/ID
        val segments = uri.pathSegments.filterNot { it.startsWith("intl-") }
        val type = knownTypes[segments.getOrNull(0)] ?: return null
        return segments.getOrNull(1)?.takeIf { it.isNotBlank() }?.let { type to it }
    }

    private fun resolveShortLink(uri: Uri): Uri? {
        val host = uri.host?.lowercase()
        if (host != "spotify.link" && host != "spotify.app.link") return null
        return runCatching {
            val conn = URL(uri.toString()).openConnection() as HttpURLConnection
            try {
                conn.instanceFollowRedirects = true
                conn.connectTimeout = 8000
                conn.readTimeout = 8000
                conn.setRequestProperty("User-Agent", UA)
                conn.inputStream.close()
                Uri.parse(conn.url.toString())
            } finally {
                conn.disconnect()
            }
        }.getOrNull()
    }

    private fun fetch(url: String): String? = runCatching {
        val conn = URL(url).openConnection() as HttpURLConnection
        try {
            conn.connectTimeout = 8000
            conn.readTimeout = 8000
            conn.setRequestProperty("User-Agent", UA)
            conn.setRequestProperty("Accept-Language", "en")
            conn.inputStream.bufferedReader().use { it.readText() }
        } finally {
            conn.disconnect()
        }
    }.getOrNull()

    private const val UA = "Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 Chrome/120 Mobile Safari/537.36"
}
