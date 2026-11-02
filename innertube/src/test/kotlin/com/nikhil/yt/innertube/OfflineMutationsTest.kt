package com.nikhil.yt.innertube

import kotlinx.coroutines.runBlocking
import kotlinx.serialization.json.Json
import kotlinx.serialization.decodeFromString
import org.junit.Assert.*
import org.junit.Test
import java.nio.file.Files

class OfflineMutationsTest {
    @Test fun offlineEditsPersistInOrderAndAreRetainedWhileDisconnected() = runBlocking {
        val root = Files.createTempDirectory("velune-offline-test").toFile()
        val previousRoot = YouTube.homeCacheDir
        val previousNetwork = YouTube.networkAvailable
        try {
            YouTube.homeCacheDir = root
            YouTube.networkAvailable = { false }
            assertTrue(YouTube.likeVideo("song", true).isSuccess)
            assertTrue(YouTube.renamePlaylist("playlist", "New name").isSuccess)
            assertTrue(YouTube.likeVideo("song", false).isSuccess)
            val file = root.walkTopDown().first { it.name == "pending.json" }
            val expected = listOf(listOf("likeVideo", "song", "true"),
                listOf("renamePlaylist", "playlist", "New name"), listOf("likeVideo", "song", "false"))
            assertEquals(expected, Json.decodeFromString<List<List<String>>>(file.readText()))
            YouTube.flushPendingMutations()
            assertEquals(expected, Json.decodeFromString<List<List<String>>>(file.readText()))
            assertTrue(YouTube.artist("uncached-artist").isFailure)
        } finally {
            YouTube.homeCacheDir = previousRoot
            YouTube.networkAvailable = previousNetwork
            root.deleteRecursively()
        }
    }

    @Test fun offlineEditWithoutStorageIsNotReportedAsSaved() = runBlocking {
        val previousRoot = YouTube.homeCacheDir
        val previousNetwork = YouTube.networkAvailable
        try {
            YouTube.homeCacheDir = null
            YouTube.networkAvailable = { false }
            assertTrue(YouTube.likeVideo("song", true).isFailure)
        } finally {
            YouTube.homeCacheDir = previousRoot
            YouTube.networkAvailable = previousNetwork
        }
    }
}
