package com.nikhil.yt.utils

import org.junit.Assert.*
import org.junit.Test

class ReleaseInfoParserTest {
    private fun release(assets: String) = """[{"tag_name":"v1.3.19","assets":[$assets]}]"""
    private fun asset(device: String, size: String = "", architecture: String = "universal") =
        """{"name":"velune-$device-$architecture-v1.3.19.apk","browser_download_url":"https://example.com/$device-$architecture.apk"$size}"""

    @Test fun sizeComesFromTheMatchingDevice_notTheFirstAsset() {
        val json = release(asset("tv", ",\"size\":90000000") + "," + asset("mobile", ",\"size\":70000000"))
        assertEquals(70000000L, ReleaseInfoParser.parse(json, "mobile", "universal").single().apkSizeBytes)
        assertEquals(90000000L, ReleaseInfoParser.parse(json, "tv", "universal").single().apkSizeBytes)
    }

    @Test fun sizeFollowsTheSelectedArchitecture() {
        val json = release(asset("mobile", ",\"size\":70000000") + "," + asset("mobile", ",\"size\":40000000", "arm64"))
        val selected = ReleaseInfoParser.parse(json, "mobile", "arm64").single()
        assertEquals("https://example.com/mobile-arm64.apk", selected.apkUrl)
        assertEquals(40000000L, selected.apkSizeBytes)
    }

    @Test fun missingOrInvalidSizeDoesNotBecomeZero() {
        for (size in listOf("", ",\"size\":null", ",\"size\":0", ",\"size\":-1")) {
            val selected = ReleaseInfoParser.parse(release(asset("mobile", size)), "mobile", "universal").single()
            assertNotNull(selected.apkUrl)
            assertNull(selected.apkSizeBytes)
        }
    }

    @Test fun missingCompatibleApkHasNoSize() {
        val selected = ReleaseInfoParser.parse(release(asset("tv", ",\"size\":90000000")), "mobile", "universal").single()
        assertNull(selected.apkUrl)
        assertNull(selected.apkSizeBytes)
    }
}
