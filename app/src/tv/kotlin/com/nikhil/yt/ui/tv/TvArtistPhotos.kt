package com.nikhil.yt.ui.tv

import android.util.LruCache
import com.nikhil.yt.innertube.YouTube
import com.nikhil.yt.innertube.models.ArtistItem
import com.nikhil.yt.models.MediaMetadata
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.async
import kotlinx.coroutines.coroutineScope
import kotlinx.coroutines.ensureActive
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext
import okhttp3.HttpUrl.Companion.toHttpUrl
import okhttp3.OkHttpClient
import okhttp3.Request
import org.json.JSONObject
import java.text.Normalizer
import java.util.concurrent.TimeUnit

/** Artist photography only: album artwork is never a backdrop fallback. */
internal object TvArtistPhotos {
    private val cache = LruCache<String, List<String>>(24)
    private val client = OkHttpClient.Builder()
        .callTimeout(12, TimeUnit.SECONDS)
        .build()

    suspend fun load(artist: MediaMetadata.Artist, onPhotos: (List<String>) -> Unit) = coroutineScope {
        val key = "${artist.id}:${artist.name}"
        cache.get(key)?.let { onPhotos(it); return@coroutineScope }
        val urls = mutableListOf<String>()
        artist.thumbnailUrl?.takeIf { it.startsWith("https://") }?.let(urls::add)
        if (urls.isNotEmpty()) onPhotos(urls.toList())
        // Resolve both sources concurrently. A slow encyclopedia request must not hide the profile photo.
        val gallery = async(Dispatchers.IO) { wikiPhotos(artist) }
        val profile = async(Dispatchers.IO) {
            try {
                val known = artist.id?.let { YouTube.artist(it).getOrNull()?.artist?.thumbnail }
                known ?: YouTube.search(artist.name, YouTube.SearchFilter.FILTER_ARTIST).getOrNull()?.items
                    ?.filterIsInstance<ArtistItem>()
                    ?.firstOrNull { normalized(it.title) == normalized(artist.name) }?.thumbnail
            } catch (cancel: CancellationException) { throw cancel
            } catch (_: Exception) { null }
        }
        val profilePublication = launch {
            profile.await()?.takeIf { it.startsWith("https://") }?.let(urls::add)
            if (urls.isNotEmpty()) onPhotos(urls.distinct())
        }
        urls.addAll(gallery.await())
        if (urls.isNotEmpty()) onPhotos(urls.distinct())
        profilePublication.join()
        urls.distinct().also {
            if (it.isNotEmpty()) {
                cache.put(key, it)
                onPhotos(it)
            }
        }
    }

    private suspend fun wikiPhotos(artist: MediaMetadata.Artist): List<String> = withContext(Dispatchers.IO) {
        val urls = mutableListOf<String>()
        try {
            // Photos embedded in the artist's own article, restricted to named portraits/live photos.
            val url = "https://en.wikipedia.org/w/api.php".toHttpUrl().newBuilder()
                .addQueryParameter("action", "query")
                .addQueryParameter("format", "json")
                .addQueryParameter("redirects", "1")
                .addQueryParameter("titles", artist.name)
                .addQueryParameter("generator", "images")
                .addQueryParameter("gimlimit", "50")
                .addQueryParameter("prop", "imageinfo")
                .addQueryParameter("iiprop", "url|size")
                .addQueryParameter("iiurlwidth", "1920")
                .build()
            client.newCall(Request.Builder().url(url)
                .header("User-Agent", "VeluneTV/1.0 (https://github.com/mohamedbassem612/Velune)").build()).execute().use { response ->
                ensureActive()
                if (response.isSuccessful) {
                    val pages = JSONObject(response.body?.string().orEmpty()).optJSONObject("query")?.optJSONObject("pages")
                    pages?.keys()?.forEach { id ->
                        val page = pages.getJSONObject(id)
                        val name = normalized(page.optString("title"))
                        val artistName = normalized(artist.name)
                        val info = page.optJSONArray("imageinfo")?.optJSONObject(0)
                        if (artistName.isNotBlank() && name.contains(artistName) &&
                            Regex("\\.(jpg|jpeg|png)$").containsMatchIn(name) &&
                            !Regex("cover|album|single|logo|signature|poster|artwork|disc|flag").containsMatchIn(name) &&
                            info != null && info.optInt("width") >= 700 && info.optInt("height") >= 400) {
                            info.optString("thumburl").ifBlank { info.optString("url") }
                                .takeIf { it.startsWith("https://") }?.let(urls::add)
                        }
                    }
                }
            }
        } catch (cancel: CancellationException) { throw cancel
        } catch (_: Exception) { /* Fall back to the artist image provided by the music source. */ }
        ensureActive()
        urls.distinct()
    }

    private fun normalized(text: String): String = Normalizer.normalize(text, Normalizer.Form.NFD)
        .replace(Regex("\\p{M}"), "").replace('_', ' ').lowercase()
}
