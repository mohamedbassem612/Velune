package com.nikhil.yt.utils

import java.net.URI
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

    /** Blocking network calls; callers must use Dispatchers.IO. Redirects stay on Spotify hosts. */
    fun resolve(url: String): Track {
        require(isSpotifyLink(url)) { "Unsupported Spotify link" }
        var current = url
        var id = trackId(current)
        for (attempt in 0 until 5) {
            if (id != null) break
            require(URI(current).host?.lowercase() in shortHosts) { "Expected a Spotify track link" }
            val response = Jsoup.connect(current).followRedirects(false).ignoreHttpErrors(true)
                .timeout(15_000).maxBodySize(512_000).execute()
            check(response.statusCode() in 300..399) { "Spotify short link did not redirect" }
            val location = requireNotNull(response.header("Location")) { "Missing Spotify redirect" }
            current = URI(current).resolve(location).toString()
            require(isSpotifyLink(current)) { "Unexpected Spotify redirect" }
            id = trackId(current)
        }
        val trackId = requireNotNull(id) { "Expected a Spotify track link" }
        // Spotify can serve an unavailable/consent page for the embed in some regions.
        // The regular public track page provides an independent metadata fallback.
        val embedded = runCatching {
            parseMetadata(fetchPage("https://open.spotify.com/embed/track/$trackId"))
        }
        return embedded.getOrElse {
            parseMetadata(fetchPage("https://open.spotify.com/track/$trackId"))
        }
    }

    private fun fetchPage(url: String): String = Jsoup.connect(url)
        .userAgent("Mozilla/5.0 (Linux; Android 13) AppleWebKit/537.36 Chrome/131.0 Mobile Safari/537.36")
        .header("Accept-Language", "en-US,en;q=0.9")
        .timeout(15_000).maxBodySize(1_000_000).get().html()

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
        // Public descriptions are "artist · album · Song · year"; do not guess from other pages.
        require(title.isNotBlank() && description.size >= 3 && "Song" in description) {
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
