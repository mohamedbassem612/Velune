package com.nikhil.yt.utils

import java.net.URI

/** Shared by every image request, independent of screen and requested thumbnail size. */
internal object ArtworkIdentity {
    fun videoId(url: String): String? {
        val uri = runCatching { URI(url) }.getOrNull() ?: return null
        val host = uri.host?.lowercase() ?: return null
        if (host != "ytimg.com" && !host.endsWith(".ytimg.com")) return null
        return Regex("^/(?:vi|vi_webp)/([A-Za-z0-9_-]+)/").find(uri.path.orEmpty())?.groupValues?.get(1)
    }

    fun canonical(url: String): String {
        val uri = runCatching { URI(url) }.getOrNull() ?: return url
        val host = uri.host?.lowercase() ?: return url
        videoId(url)?.let { return "youtube-cover:$it" }
        if (host == "googleusercontent.com" || host.endsWith(".googleusercontent.com") ||
            host == "ggpht.com" || host.endsWith(".ggpht.com")) {
            // Strip only a known image transformation suffix, never arbitrary URL parameters.
            return "https://$host" + uri.rawPath.replace(Regex("=(?:w\\d+|s\\d+).*?$"), "")
        }
        return url
    }
}

/** Song ID survives thumbnail URL changes, playlist entry IDs and navigation between screens. */
data class SongCover(val id: String, val url: String?)
