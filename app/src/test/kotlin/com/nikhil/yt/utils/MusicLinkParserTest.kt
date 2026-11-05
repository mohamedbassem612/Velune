package com.nikhil.yt.utils

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class MusicLinkParserTest {
    private val id = "dQw4w9WgXcQ"

    @Test
    fun acceptsYouTubeShareAndWatchVariants() {
        listOf(
            "https://youtu.be/$id?si=share-token",
            "https://www.youtube.com/watch?v=$id&list=PL123",
            "https://music.youtube.com/watch?si=token&v=$id",
            "https://m.youtube.com/watch?v=$id",
            "https://youtube.com/shorts/$id?feature=share",
            "https://www.youtube.com/live/$id",
            "https://www.youtube.com/embed/$id",
            "https://www.youtube.com/v/$id",
            "vnd.youtube:$id",
            "vnd.youtube://$id",
            "vnd.youtube.launch:$id",
        ).forEach { assertEquals(it, id, MusicLinkParser.youtubeVideoId(it)) }
    }

    @Test
    fun extractsLinkFromSharedText() {
        assertEquals("https://youtu.be/$id?si=abc", MusicLinkParser.extractUrl("Listen to this:\nhttps://youtu.be/$id?si=abc"))
        assertEquals("https://open.spotify.com/track/123", MusicLinkParser.extractUrl("Song (https://open.spotify.com/track/123)."))
        assertEquals("spotify:track:123", MusicLinkParser.extractUrl("spotify:track:123"))
        assertNull(MusicLinkParser.extractUrl("No link here"))
    }

    @Test
    fun rejectsUnrelatedHostsAndInvalidVideoIds() {
        listOf(
            "https://example.com/watch?v=$id",
            "https://youtube.com.example.com/watch?v=$id",
            "https://youtu.be/invalid",
            "https://youtube.com/watch?v=",
            "https://youtube.com/playlist?list=PL123",
            "not a uri",
        ).forEach { assertNull(it, MusicLinkParser.youtubeVideoId(it)) }
    }
}
