package com.nikhil.yt.connect

import org.json.JSONObject

/** The active service supplies LAN assets; consumers do not need to know the renderer platform. */
internal object ConnectAssetAccess {
    @Volatile var fetcher: (suspend (String) -> JSONObject?)? = null
    suspend fun fetch(id: String): JSONObject? = try { fetcher?.invoke(id) }
        catch (cancel: kotlinx.coroutines.CancellationException) { throw cancel }
        catch (_: Exception) { null }
}
