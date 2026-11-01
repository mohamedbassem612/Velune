package com.nikhil.yt.utils

import org.junit.Assert.*
import org.junit.Test

class SpotifyLinkResolverTest {
    private val id = "4cOdK2wGLETKBW3PvgPWqT"
    private val track = SpotifyLinkResolver.Track("Never Gonna Give You Up", listOf("Rick Astley"))

    @Test fun extractsTrackIdFromSharedLocalizedAndEmbeddedLinks() {
        listOf(
            "https://open.spotify.com/track/$id?si=abc",
            "https://open.spotify.com/intl-ar/track/$id",
            "https://open.spotify.com/embed/track/$id",
            "spotify:track:$id",
        ).forEach { assertEquals(it, id, SpotifyLinkResolver.trackId(it)) }
    }

    @Test fun rejectsOtherContentAndLookalikeHosts() {
        listOf(
            "https://open.spotify.com/album/$id",
            "https://open.spotify.com/playlist/$id",
            "https://open.spotify.com/episode/$id",
            "https://open.spotify.com/track/invalid",
            "https://open.spotify.com.evil.com/track/$id",
            "https://open.spotify.com@evil.com/track/$id",
            "spotify:album:$id",
            "file://open.spotify.com/track/$id",
        ).forEach { assertNull(it, SpotifyLinkResolver.trackId(it)) }
    }

    @Test fun recognizesShortLinksButRejectsUnrelatedDestinations() {
        assertTrue(SpotifyLinkResolver.isSpotifyLink("https://spotify.link/example"))
        assertTrue(SpotifyLinkResolver.isSpotifyLink("https://spotify.app.link/example"))
        assertFalse(SpotifyLinkResolver.isSpotifyLink("https://evil.com/track/$id"))
        assertFalse(SpotifyLinkResolver.isSpotifyLink("https://open.spotify.com.evil.com/track/$id"))
    }

    @Test fun readsTitleAndArtistsFromPublicEmbedMetadata() {
        val parsed = SpotifyLinkResolver.parseMetadata(page("""
            {"type":"track","name":"Never Gonna Give You Up",
            "artists":[{"name":"Rick Astley","uri":"spotify:artist:example"}]}
        """))
        assertEquals(track, parsed)
        assertEquals("Never Gonna Give You Up Rick Astley", parsed.query)
    }

    @Test fun preservesUnicodeAndFeaturedArtists() {
        val parsed = SpotifyLinkResolver.parseMetadata(page("""
            {"type":"track","name":"أغنية","artists":[{"name":"فنان"},{"name":"Beyoncé"}]}
        """))
        assertEquals("أغنية فنان Beyoncé", parsed.query)
    }

    @Test fun rejectsMissingMetadataInsteadOfSearchingWithAnEmptyQuery() {
        listOf("<html>Unavailable</html>", page("""{"type":"album","name":"Album"}"""),
            page("""{"type":"track","name":"Song","artists":[]}""")
        ).forEach { html ->
            try {
                SpotifyLinkResolver.parseMetadata(html)
                fail("Missing track metadata must fail")
            } catch (_: IllegalArgumentException) { }
        }
    }

    @Test fun acceptsMatchingArtistAndTitleRegardlessOfPunctuation() {
        assertTrue(SpotifyLinkResolver.matchScore(track, "Never Gonna Give You Up!", listOf("Rick Astley")) > 0)
        assertTrue(SpotifyLinkResolver.matchScore(
            SpotifyLinkResolver.Track("Halo", listOf("Beyoncé")), "Halo", listOf("Beyonce")) > 0)
    }

    @Test fun rejectsCoversAndDifferentSongsFromTheSameArtist() {
        assertEquals(0.0, SpotifyLinkResolver.matchScore(track, track.title, listOf("Cover Band")), 0.0)
        assertEquals(0.0, SpotifyLinkResolver.matchScore(track, "Together Forever", track.artists), 0.0)
    }

    @Test fun prefersExactTitleOverExtraVersionWords() {
        val exact = SpotifyLinkResolver.matchScore(track, track.title, track.artists)
        val alternate = SpotifyLinkResolver.matchScore(track, "${track.title} (Remastered)", track.artists)
        assertTrue(exact > alternate)
    }

    @Test fun readsReportedRobertoTrackMetadata() {
        val parsed = SpotifyLinkResolver.parseMetadata(page("""
            {"type":"track","name":"Roberto","artists":[{"name":"Cairokee"}]}
        """))
        assertEquals("Roberto Cairokee", parsed.query)
        assertEquals("64UXkp5qVMJduPib0auZnn", SpotifyLinkResolver.trackId(
            "https://open.spotify.com/track/64UXkp5qVMJduPib0auZnn?si=PaKTd_yeSpeSOwr7PCL1Tg&utm_source=whatsapp"))
    }

    @Test fun fallsBackToPublicSongMetadataWhenEmbedIsMissing() {
        val parsed = SpotifyLinkResolver.parseMetadata("""
            <meta property="og:type" content="music.song">
            <meta property="og:title" content="Roberto">
            <meta property="og:description" content="Cairokee · Roma · Song · 2022">
        """)
        assertEquals(SpotifyLinkResolver.Track("Roberto", listOf("Cairokee")), parsed)
    }

    @Test fun malformedEmbedDoesNotHideValidPublicSongMetadata() {
        val parsed = SpotifyLinkResolver.parseMetadata("""
            <script id="__NEXT_DATA__">{invalid json}</script>
            <meta property="og:type" content="music.song">
            <meta property="og:title" content="Roberto">
            <meta property="og:description" content="Cairokee · Roma · Song · 2022">
        """)
        assertEquals("Roberto Cairokee", parsed.query)
    }

    @Test fun blockedEmbedFallsBackToRegularTrackPage() {
        val requested = mutableListOf<String>()
        val result = SpotifyLinkResolver.resolve("https://open.spotify.com/track/$id") { url ->
            requested += url
            if ("/embed/" in url) SpotifyLinkResolver.Response(403, "blocked") else
                SpotifyLinkResolver.Response(200, page("""
                    {"type":"track","name":"Roberto","artists":[{"name":"Cairokee"}]}
                """))
        }
        assertEquals("Roberto Cairokee", result.query)
        assertEquals(2, requested.size)
    }

    @Test fun unavailableHtmlFallsBackToOEmbedTitleWithoutAutoplay() {
        val result = SpotifyLinkResolver.resolve("https://open.spotify.com/track/$id") { url ->
            if ("/oembed?" in url) SpotifyLinkResolver.Response(200,
                """{"provider_name":"Spotify","title":"Roberto"}""")
            else SpotifyLinkResolver.Response(200, "<html>Consent required</html>")
        }
        assertEquals("Roberto", result.query)
        assertTrue(result.artists.isEmpty())
        assertEquals(0.0, SpotifyLinkResolver.matchScore(result, "Roberto", listOf("Cairokee")), 0.0)
    }

    @Test fun allBlockedResponsesReportHttpStatusInsteadOfBlamingLinkFormat() {
        try {
            SpotifyLinkResolver.resolve("https://open.spotify.com/track/$id") {
                SpotifyLinkResolver.Response(403, "blocked")
            }
            fail("Blocked metadata should be reported")
        } catch (error: SpotifyLinkResolver.MetadataException) {
            assertEquals("HTTP 403", error.reason)
            assertEquals(2, error.suppressed.size)
        }
    }

    @Test fun shortLinkRedirectIsResolvedBeforeMetadataLookup() {
        val result = SpotifyLinkResolver.resolve("https://spotify.link/example") { url ->
            if (url.startsWith("https://spotify.link/"))
                SpotifyLinkResolver.Response(302, "", "https://open.spotify.com/track/$id?si=shared")
            else SpotifyLinkResolver.Response(200, page("""
                {"type":"track","name":"Roberto","artists":[{"name":"Cairokee"}]}
            """))
        }
        assertEquals("Roberto Cairokee", result.query)
    }

    @Test fun localizedPublicMetadataDoesNotRequireEnglishSongLabel() {
        val parsed = SpotifyLinkResolver.parseMetadata("""
            <meta property="og:type" content="music.song">
            <meta property="og:title" content="روبرتو">
            <meta property="og:description" content="كايروكي · روما · أغنية · ٢٠٢٢">
        """)
        assertEquals("روبرتو كايروكي", parsed.query)
    }

    @Test fun unexpectedRedirectDoesNotFetchAnUnrelatedSite() {
        var calls = 0
        try {
            SpotifyLinkResolver.resolve("https://spotify.link/example") {
                calls++
                SpotifyLinkResolver.Response(302, "", "https://example.com/")
            }
            fail("Unexpected redirect must be rejected")
        } catch (_: IllegalArgumentException) {
            assertEquals(1, calls)
        }
    }

    private fun page(entity: String) = """
        <html><script id="__NEXT_DATA__" type="application/json">
        {"props":{"pageProps":{"state":{"data":{"entity":$entity}}}}}
        </script></html>
    """
}
