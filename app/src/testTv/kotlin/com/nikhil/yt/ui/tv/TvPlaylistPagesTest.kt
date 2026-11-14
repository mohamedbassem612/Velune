package com.nikhil.yt.ui.tv

import kotlinx.coroutines.runBlocking
import org.junit.Assert.*
import org.junit.Test

class TvPlaylistPagesTest {
    @Test
    fun loadsEveryPagePreservingOrderAndRepeatedTracks() = runBlocking {
        val calls = mutableListOf<String>()
        val songs = collectTvPlaylistPages(listOf("a", "b"), "next") { token ->
            calls += token
            if (token == "next") listOf("a", "c") to "last" else listOf("d") to null
        }
        assertEquals(listOf("a", "b", "a", "c", "d"), songs)
        assertEquals(listOf("next", "last"), calls)
    }

    @Test
    fun repeatedContinuationFailsInsteadOfHanging() = runBlocking {
        try {
            collectTvPlaylistPages(listOf("a"), "loop") { listOf("b") to "loop" }
            fail("A continuation loop must fail")
        } catch (_: IllegalStateException) { }
    }

    @Test
    fun failedPageDoesNotReturnAPartialPlaylist() = runBlocking {
        try {
            collectTvPlaylistPages(listOf("a"), "next") { error("Network failed") }
            fail("A partially loaded playlist must not be used for whole-list actions")
        } catch (error: IllegalStateException) {
            assertEquals("Network failed", error.message)
        }
    }
}
