package com.nikhil.yt.utils

import java.io.IOException
import java.net.Proxy
import java.net.URI
import java.net.URLEncoder
import java.util.concurrent.TimeUnit
import okhttp3.OkHttpClient
import okhttp3.Request
import java.text.Normalizer
import kotlinx.serialization.json.Json
import kotlinx.serialization.json.contentOrNull
import kotlinx.serialization.json.jsonArray
import kotlinx.serialization.json.jsonObject
import kotlinx.serialization.json.jsonPrimitive
import org.jsoup.Jsoup

/** Reads public track metadata only. Playback always comes from YouTube Music. */
object SpotifyLinkResolver {
    data class Track(val title: String, val artists: List<String>) {
        val query: String get() = "$title ${artists.joinToString(" ")}".trim()
    }

    private val trackIdPattern = Regex("[A-Za-z0-9]{22}")
    private val shortHosts = setOf("spotify.link", "spotify.app.link")
    private val client by lazy {
        OkHttpClient.Builder().followRedirects(false).followSslRedirects(false)
            .connectTimeout(15, TimeUnit.SECONDS).readTimeout(15, TimeUnit.SECONDS)
            .callTimeout(20, TimeUnit.SECONDS).build()
    }

    internal data class Response(val status: Int, val body: String, val location: String? = null)
    class MetadataException(val reason: String, cause: Throwable? = null) : IOException(reason, cause)


    fun isSpotifyLink(url: String): Boolean = runCatching {
        val uri = URI(url)
        uri.scheme.equals("spotify", true) ||
            (uri.scheme?.lowercase() in setOf("http", "https") &&
                (uri.host.equals("open.spotify.com", true) || uri.host?.lowercase() in shortHosts))
    }.getOrDefault(false)

    internal fun trackId(url: String): String? = runCatching {
        val uri = URI(url)
        if (uri.scheme.equals("spotify", true)) {
            return@runCatching uri.schemeSpecificPart.removePrefix("track:")
                .takeIf { uri.schemeSpecificPart.startsWith("track:") && trackIdPattern.matches(it) }
        }
        if (uri.scheme?.lowercase() !in setOf("http", "https") ||
            !uri.host.equals("open.spotify.com", true)
        ) return@runCatching null
        val parts = uri.path.orEmpty().split('/').filter(String::isNotEmpty).toMutableList()
        if (parts.firstOrNull()?.startsWith("intl-") == true) parts.removeAt(0)
        if (parts.firstOrNull() == "embed") parts.removeAt(0)
        parts.getOrNull(1)?.takeIf {
            parts.size == 2 && parts[0] == "track" && trackIdPattern.matches(it)
        }
    }.getOrNull()

    /** Blocking network calls; callers must use Dispatchers.IO. */
    fun resolve(url: String, proxy: Proxy? = null): Track {
        val transport = if (proxy == null) client else client.newBuilder().proxy(proxy).build()
        return resolve(url) { request(it, transport) }
    }

    // A transport boundary lets tests exercise real fallback and redirect behavior.
    internal fun resolve(url: String, fetch: (String) -> Response): Track {
        require(isSpotifyLink(url)) { "Unsupported Spotify link" }
        var current = url
        var id = trackId(current)
        for (attempt in 0 until 5) {
            if (id != null) break
            require(URI(current).host?.lowercase() in shortHosts) { "Expected a Spotify track link" }
            val response = fetch(current)
            if (response.status !in 300..399) throw MetadataException("HTTP ${response.status}")
            val location = response.location ?: throw MetadataException("Missing redirect")
            current = URI(current).resolve(location).toString()
            require(isSpotifyLink(current)) { "Unexpected Spotify redirect" }
            id = trackId(current)
        }
        val trackId = id ?: throw MetadataException("Not a Spotify track link")
        val failures = mutableListOf<Exception>()
        for (path in listOf("embed/track/$trackId", "track/$trackId")) {
            try {
                return parseMetadata(fetchBody("https://open.spotify.com/$path", fetch))
            } catch (error: Exception) {
                failures += error
            }
        }
        // oEmbed can still supply the title when HTML metadata is blocked or changed.
        // With no artist, show song search results instead of autoplaying an uncertain match.
        try {
            val encoded = URLEncoder.encode("https://open.spotify.com/track/$trackId", "UTF-8")
            return parseOEmbed(fetchBody("https://open.spotify.com/oembed?url=$encoded", fetch))
        } catch (error: Exception) {
            failures += error
        }
        val diagnostic = failures.filterIsInstance<MetadataException>().firstOrNull()?.reason
            ?: if (failures.any { it is IOException }) "Network request failed" else "Track data unavailable"
        throw MetadataException(diagnostic, failures.lastOrNull()).also { combined ->
            failures.dropLast(1).forEach(combined::addSuppressed)
        }
    }

    private fun request(url: String, transport: OkHttpClient): Response {
        val request = Request.Builder().url(url)
            .header("User-Agent", "Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 Chrome/131.0 Mobile Safari/537.36")
            .header("Accept-Language", "en-US,en;q=0.9").build()
        return transport.newCall(request).execute().use { response ->
            Response(response.code, response.body?.string().orEmpty(), response.header("Location"))
        }
    }

    private fun fetchBody(url: String, fetch: (String) -> Response): String {
        var current = url
        repeat(5) {
            val response = fetch(current)
            if (response.status in 200..299) return response.body
            if (response.status !in 300..399) throw MetadataException("HTTP ${response.status}")
            val location = response.location ?: throw MetadataException("Missing redirect")
            current = URI(current).resolve(location).toString()
            if (!isSpotifyLink(current) || URI(current).scheme?.lowercase() !in setOf("http", "https")) {
                throw MetadataException("Spotify redirected away from track data")
            }
        }
        throw MetadataException("Too many redirects")
    }

    internal fun parseOEmbed(body: String): Track {
        val data = Json.parseToJsonElement(body).jsonObject
        require(data["provider_name"]?.jsonPrimitive?.contentOrNull == "Spotify")
        val title = data["title"]?.jsonPrimitive?.contentOrNull?.trim().orEmpty()
        require(title.isNotEmpty()) { "Spotify title unavailable" }
        return Track(title, emptyList())
    }

    internal fun parseMetadata(html: String): Track {
        val page = Jsoup.parse(html)
        val script = page.selectFirst("script#__NEXT_DATA__")?.data()
        if (!script.isNullOrBlank()) {
            val embedded = runCatching {
                val entity = requireNotNull(Json.parseToJsonElement(script).jsonObject["props"]?.jsonObject
                    ?.get("pageProps")?.jsonObject?.get("state")?.jsonObject
                    ?.get("data")?.jsonObject?.get("entity")?.jsonObject)
                require(entity["type"]?.jsonPrimitive?.contentOrNull == "track")
                val title = entity["name"]?.jsonPrimitive?.contentOrNull?.trim().orEmpty()
                val artists = entity["artists"]?.jsonArray?.mapNotNull {
                    it.jsonObject["name"]?.jsonPrimitive?.contentOrNull?.trim()?.takeIf(String::isNotEmpty)
                }.orEmpty()
                require(title.isNotEmpty() && artists.isNotEmpty())
                Track(title, artists)
            }.getOrNull()
            if (embedded != null) return embedded
        }
        fun meta(property: String) = page.selectFirst("meta[property=$property]")?.attr("content")?.trim().orEmpty()
        require(meta("og:type") == "music.song") { "Spotify track metadata unavailable" }
        val title = meta("og:title")
        val description = meta("og:description").split('·').map(String::trim)
        // The music.song type identifies a track; the Song label itself may be localized.
        require(title.isNotBlank() && description.size >= 2) {
            "Missing Spotify song title or artist"
        }
        val artist = description.first()
        require(artist.isNotBlank()) { "Missing Spotify artist" }
        return Track(title, listOf(artist))
    }

    // Requiring both title and artist avoids autoplaying an unrelated song or a cover.
    internal fun matchScore(track: Track, title: String, artists: List<String>): Double {
        fun tokens(value: String): Set<String> = Normalizer.normalize(value.lowercase(), Normalizer.Form.NFKD)
            .replace(Regex("\\p{M}+"), "")
            .split(Regex("[^\\p{L}\\p{N}]+"))
            .filter(String::isNotEmpty).toSet()
        fun overlap(expected: Set<String>, actual: Set<String>): Double =
            if (expected.isEmpty()) 0.0 else expected.intersect(actual).size.toDouble() / expected.size
        val wantedTitle = tokens(track.title)
        val foundTitle = tokens(title)
        val titleScore = overlap(wantedTitle, foundTitle)
        val titlePrecision = overlap(foundTitle, wantedTitle)
        val artistScore = track.artists.maxOfOrNull { expected ->
            artists.maxOfOrNull { overlap(tokens(expected), tokens(it)) } ?: 0.0
        } ?: 0.0
        return if (titleScore >= 0.8 && titlePrecision >= 0.6 && artistScore >= 0.8)
            titleScore + titlePrecision + artistScore else 0.0
    }
}
