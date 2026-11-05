package com.nikhil.yt.utils

import org.jsoup.Jsoup

internal object SpotifyPageMetadata {
    data class Metadata(val title: String, val artist: String?)

    fun parse(html: String): Metadata? {
        val page = Jsoup.parse(html)
        val title = page.selectFirst("meta[property=og:title]")?.attr("content")
            ?.trim()?.takeIf { it.isNotEmpty() && !it.equals("Spotify", ignoreCase = true) } ?: return null
        val artist = page.selectFirst("meta[property=og:description]")?.attr("content")
            ?.split("·", "•")?.firstOrNull()?.trim()?.takeIf { it.isNotBlank() }
        return Metadata(title, artist)
    }
}
