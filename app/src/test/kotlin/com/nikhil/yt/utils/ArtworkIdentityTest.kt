package com.nikhil.yt.utils

import org.junit.Assert.*
import org.junit.Test

class ArtworkIdentityTest {
    @Test fun youtubeSizesAndFormatsShareAnIdentity() {
        val small = "https://i.ytimg.com/vi/abc_123-defg/hqdefault.jpg"
        val large = "https://i.ytimg.com/vi_webp/abc_123-defg/maxresdefault.webp"
        assertEquals(ArtworkIdentity.canonical(small), ArtworkIdentity.canonical(large))
        assertEquals("abc_123-defg", ArtworkIdentity.videoId(large))
    }

    @Test fun googleSizesShareAnIdentityAcrossScreens() {
        assertEquals(ArtworkIdentity.canonical("https://lh3.googleusercontent.com/art=w120-h120-p-l90-rj"),
            ArtworkIdentity.canonical("https://lh3.googleusercontent.com/art=w544-h544-l90-rj"))
        assertNotEquals(ArtworkIdentity.canonical("https://lh3.googleusercontent.com/art=w120-h120"),
            ArtworkIdentity.canonical("https://lh3.googleusercontent.com/other=w120-h120"))
    }

    @Test fun unrelatedUrlsAreNotConflated() {
        val url = "https://example.com/image?size=40&token=abc"
        assertEquals(url, ArtworkIdentity.canonical(url))
        assertNull(ArtworkIdentity.videoId("https://fake-ytimg.com/vi/song/hqdefault.jpg"))
        assertEquals("invalid", ArtworkIdentity.canonical("invalid"))
    }
}
