package com.nikhil.yt.utils

import org.json.JSONArray

/** The URL and size must always belong to the same device-compatible asset. */
internal object ReleaseInfoParser {
    fun parse(json: String, device: String, architecture: String): List<ReleaseInfo> {
        val releases = JSONArray(json)
        return (0 until releases.length()).map { index ->
            val release = releases.getJSONObject(index)
            val assets = release.optJSONArray("assets")
            val candidates = (0 until (assets?.length() ?: 0)).map { assetIndex ->
                val asset = assets!!.getJSONObject(assetIndex)
                UpdateAsset(
                    name = asset.optString("name"),
                    downloadUrl = asset.optString("browser_download_url"),
                    sizeBytes = asset.optLong("size", -1L).takeIf { it > 0L },
                )
            }
            val apk = UpdateAssetSelector.select(candidates, device, architecture)
            ReleaseInfo(
                tagName = release.optString("tag_name", ""),
                name = release.optString("name", ""),
                body = if (release.has("body")) release.optString("body") else null,
                publishedAt = release.optString("published_at", ""),
                htmlUrl = release.optString("html_url", ""),
                apkUrl = apk?.downloadUrl,
                apkSizeBytes = apk?.sizeBytes,
            )
        }
    }
}
