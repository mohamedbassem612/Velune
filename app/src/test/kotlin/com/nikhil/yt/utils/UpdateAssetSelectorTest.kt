package com.nikhil.yt.utils

import org.junit.Assert.*
import org.junit.Test

class UpdateAssetSelectorTest {
    private fun asset(name: String) = UpdateAsset(name, "https://github.com/example/releases/download/v1.3.9/$name")
    private val phone = asset("velune-v1.3.9.apk")
    private val tv = asset("velune-tv-v1.3.9.apk")

    @Test fun mobileSelectsMobile_whenTvIsFirst() {
        assertEquals(phone, UpdateAssetSelector.select(listOf(tv, phone), "mobile", "universal"))
        assertEquals(phone, UpdateAssetSelector.select(listOf(phone, tv), "mobile", "universal"))
    }
    @Test fun publishedNames_keepLegacyMobileSelectionSafe() {
        val mobile = asset("velune-mobile-v1.3.10.apk")
        val television = asset("velune-tv-v1.3.10.apk")
        assertEquals(mobile, listOf(television, mobile).sortedBy { it.name }.first())
        assertEquals(mobile, UpdateAssetSelector.select(listOf(television, mobile), "mobile", "universal"))
        assertEquals(television, UpdateAssetSelector.select(listOf(mobile, television), "tv", "universal"))
    }
    @Test fun tvSelectsTv_regardlessOfAssetOrder() {
        assertEquals(tv, UpdateAssetSelector.select(listOf(phone, tv), "tv", "universal"))
        assertEquals(tv, UpdateAssetSelector.select(listOf(tv, phone), "tv", "universal"))
    }
    @Test fun missingMatchingDevice_doesNotFallBackToAnotherDevice() {
        assertNull(UpdateAssetSelector.select(listOf(tv), "mobile", "universal"))
        assertNull(UpdateAssetSelector.select(listOf(phone), "tv", "universal"))
        assertNull(UpdateAssetSelector.select(listOf(asset("VeluneTV.apk")), "mobile", "universal"))
    }
    @Test fun exactArchitectureWins_andUniversalIsTheOnlyFallback() {
        val arm64 = asset("app-mobile-arm64-release.apk")
        val x86 = asset("app-mobile-x86_64-release.apk")
        assertEquals(arm64, UpdateAssetSelector.select(listOf(phone, x86, arm64), "mobile", "arm64"))
        assertEquals(phone, UpdateAssetSelector.select(listOf(x86, phone), "mobile", "arm64"))
        assertNull(UpdateAssetSelector.select(listOf(x86), "mobile", "arm64"))
        assertEquals(phone, UpdateAssetSelector.select(listOf(arm64, phone), "mobile", "universal"))
    }
    @Test fun debugUnknownAndEmptyAssets_areIgnored() {
        assertEquals(phone, UpdateAssetSelector.select(listOf(asset("Velune-Mobile-preview-debug.apk"),
            asset("unrelated.apk"), phone.copy(downloadUrl = ""), phone), "mobile", "universal"))
        assertNull(UpdateAssetSelector.select(emptyList(), "tv", "universal"))
    }
    @Test fun legacyUniversalAndAbiNames_remainCompatibleWithMobile() {
        assertEquals(asset("Velune.apk"), UpdateAssetSelector.select(listOf(asset("Velune.apk")), "mobile", "universal"))
        assertEquals(asset("app-armeabi-v7a-release.apk"), UpdateAssetSelector.select(
            listOf(asset("app-armeabi-v7a-release.apk")), "mobile", "armeabi"))
        assertNull(UpdateAssetSelector.select(listOf(asset("Velune.apk")), "tv", "universal"))
    }
}
