package com.nikhil.yt.utils

data class UpdateAsset(val name: String, val downloadUrl: String)

/** GitHub asset ordering is not a device compatibility guarantee. */
object UpdateAssetSelector {
    fun select(assets: List<UpdateAsset>, device: String, architecture: String): UpdateAsset? {
        val targetTv = device.equals("tv", ignoreCase = true)
        val targetAbi = normalizeArchitecture(architecture)
        return assets.mapNotNull { asset ->
            val name = asset.name.lowercase()
            if (!name.endsWith(".apk") || asset.downloadUrl.isBlank()) return@mapNotNull null
            val tokens = name.removeSuffix(".apk").split(Regex("[^a-z0-9]+"))
            if ("debug" in tokens) return@mapNotNull null
            val tv = "tv" in tokens || "androidtv" in tokens || "velunetv" in tokens
            if (tv && ("mobile" in tokens || "phone" in tokens)) return@mapNotNull null
            val recognized = name.startsWith("velune") || name.startsWith("app-")
            if (!recognized || tv != targetTv) return@mapNotNull null
            val abi = normalizeArchitecture(name)
            val rank = when {
                abi == targetAbi -> 0
                abi == "universal" -> 1
                else -> return@mapNotNull null
            }
            Triple(asset, rank, name)
        }.sortedWith(compareBy<Triple<UpdateAsset, Int, String>> { it.second }.thenBy { it.third })
            .firstOrNull()?.first
    }

    private fun normalizeArchitecture(value: String): String {
        val name = value.lowercase()
        return when {
            Regex("x86[-_]64").containsMatchIn(name) -> "x86_64"
            "arm64" in name || "aarch64" in name -> "arm64"
            "armeabi" in name || "armv7" in name -> "armeabi"
            Regex("(^|[-_.])x86([-_.]|$)").containsMatchIn(name) -> "x86"
            else -> "universal"
        }
    }
}

internal fun isCompatibleUpdatePackage(
    installedPackage: String,
    installedVersion: Long,
    archivePackage: String?,
    archiveVersion: Long,
): Boolean = installedPackage == archivePackage && archiveVersion >= installedVersion

class InvalidUpdatePackageException : java.io.IOException("The update does not match this application")
