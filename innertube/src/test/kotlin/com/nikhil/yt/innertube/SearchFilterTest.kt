package com.nikhil.yt.innertube

import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Test
import java.net.URLDecoder
import java.nio.charset.StandardCharsets
import java.util.Base64

class SearchFilterTest {
    private fun decode(filter: YouTube.SearchFilter): ByteArray =
        Base64.getDecoder().decode(URLDecoder.decode(filter.value, StandardCharsets.UTF_8.name()))

    @Test fun everySearchFilterHasValidWireEncoding() {
        val filters = listOf(
            YouTube.SearchFilter.FILTER_SONG,
            YouTube.SearchFilter.FILTER_VIDEO,
            YouTube.SearchFilter.FILTER_ALBUM,
            YouTube.SearchFilter.FILTER_ARTIST,
            YouTube.SearchFilter.FILTER_FEATURED_PLAYLIST,
            YouTube.SearchFilter.FILTER_COMMUNITY_PLAYLIST,
        )
        for (filter in filters) {
            // Search params are URL-encoded Base64 protobuf, starting with the
            // length-delimited search-filter field (field 2, wire type 2).
            assertEquals(0x12, decode(filter).first().toInt())
        }
    }

    @Test fun songsRequestAudioResults() {
        // Nested field 17 contains the Songs selector (field 1 = true).
        assertArrayEquals(byteArrayOf(0x12, 0x05, 0x8a.toByte(), 0x01, 0x02, 0x08, 0x01),
            decode(YouTube.SearchFilter.FILTER_SONG).take(7).toByteArray())
    }

    @Test fun videosRequestVideoResults() {
        // Videos uses field 2 = true in the same nested search-filter message.
        assertArrayEquals(byteArrayOf(0x12, 0x05, 0x8a.toByte(), 0x01, 0x02, 0x10, 0x01),
            decode(YouTube.SearchFilter.FILTER_VIDEO).take(7).toByteArray())
    }
}
