package com.nikhil.yt.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class SpotifyPageMetadataTest {
    @Test
    fun readsReorderedSingleQuotedAttributesAndDecodesEntities() {
        val html = """
            <meta content='Song &amp; Music' property='og:title'>
            <meta property='og:description' content='Artist &quot;Name&quot; · Song · 2026'>
        """.trimIndent()
        assertEquals(SpotifyPageMetadata.Metadata("Song & Music", "Artist \"Name\""), SpotifyPageMetadata.parse(html))
    }

    @Test
    fun readsDoubleQuotedAttributesAndAlternateArtistSeparator() {
        val html = """<meta property="og:title" content="Title"><meta content="Artist • Album" property="og:description">"""
        assertEquals(SpotifyPageMetadata.Metadata("Title", "Artist"), SpotifyPageMetadata.parse(html))
    }

    @Test
    fun missingMetadataAndGenericShellRequireFallback() {
        assertNull(SpotifyPageMetadata.parse("<html><title>Spotify</title></html>"))
        assertNull(SpotifyPageMetadata.parse("""<meta property="og:title" content="Spotify">"""))
    }
}
