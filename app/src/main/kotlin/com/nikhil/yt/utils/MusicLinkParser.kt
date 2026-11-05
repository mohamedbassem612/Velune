package com.nikhil.yt.utils

import java.net.URI
import java.net.URLDecoder
import java.util.Locale

/** Parsing is kept independent of Android so shared text and URL variants can be tested. */
object MusicLinkParser {
    fun extractUrl(text: String): String? =
        Regex("(?:https?://|spotify:|vnd\\.youtube(?:\\.launch)?:)[^\\s<>]+", RegexOption.IGNORE_CASE)
            .find(text)?.value?.trimEnd('.', ',', ')', ']', '!', '"', '\'')

    fun youtubeVideoId(url: String): String? = runCatching {
        val uri = URI(url)
        val host = uri.host?.lowercase(Locale.ROOT)
        val segments = uri.path.orEmpty().split('/').filter { it.isNotBlank() }
        val id = when {
            uri.scheme in listOf("vnd.youtube", "vnd.youtube.launch") ->
                uri.schemeSpecificPart.substringBefore('?').removePrefix("//").trim('/')
            host == "youtu.be" -> segments.firstOrNull()
            host in listOf("youtube.com", "www.youtube.com", "m.youtube.com", "music.youtube.com", "youtube") ->
                when (segments.firstOrNull()) {
                    "watch" -> uri.rawQuery.orEmpty().split('&').firstNotNullOfOrNull {
                        val pair = it.split('=', limit = 2)
                        if (pair[0] == "v") pair.getOrNull(1)?.let { value -> URLDecoder.decode(value, "UTF-8") } else null
                    }
                    "shorts", "live", "embed", "v" -> segments.getOrNull(1)
                    else -> if (host == "youtube") segments.firstOrNull() else null
                }
            else -> null
        }
        id?.takeIf { it.matches(Regex("[A-Za-z0-9_-]{11}")) }
    }.getOrNull()
}
