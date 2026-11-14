package com.nikhil.yt.connect

import kotlinx.coroutines.*
import kotlinx.coroutines.channels.Channel
import org.json.JSONObject
import org.junit.Assert.*
import org.junit.Test
import java.io.IOException

class ConnectMediaTest {
    @Test fun repliesAreCorrelatedEvenWhenTheyArriveOutOfOrder() = runBlocking {
        val sent = Channel<JSONObject>(4)
        val media = ConnectMedia(this, { sent.trySend(it) }, { false }, { error("No source expected") })
        val first = async { media.request(JSONObject().put("operation", "first")) }
        val second = async { media.request(JSONObject().put("operation", "second")) }
        val a = sent.receive()
        val b = sent.receive()
        media.receive(JSONObject().put("type", "mediaReply").put("requestId", b.getString("requestId"))
            .put("ok", true).put("result", "second"))
        media.receive(JSONObject().put("type", "mediaReply").put("requestId", a.getString("requestId"))
            .put("ok", true).put("result", "first"))
        assertEquals("first", first.await().getString("result"))
        assertEquals("second", second.await().getString("result"))
        media.close()
    }

    @Test fun disconnectUnblocksPendingReads() = runBlocking {
        val sent = Channel<JSONObject>(1)
        val media = ConnectMedia(this, { sent.trySend(it) }, { false }, { error("No source expected") })
        val result = async { runCatching { media.request(JSONObject().put("operation", "read")) } }
        sent.receive()
        media.close()
        assertTrue(result.await().exceptionOrNull() is IOException)
        assertTrue(runCatching { media.request(JSONObject()) }.exceptionOrNull() is IOException)
    }

    @Test fun cachedOnlyMissDoesNotOpenOrDownloadASource() = runBlocking {
        val sent = Channel<JSONObject>(1)
        var opened = false
        val media = ConnectMedia(this, { sent.trySend(it) }, { false }, { opened = true; error("Must not download") })
        media.receive(JSONObject().put("type", "mediaRequest").put("requestId", "probe")
            .put("operation", "open").put("song", "song-id").put("cachedOnly", true))
        val response = withTimeout(2000) { sent.receive() }
        assertFalse(response.getBoolean("ok"))
        assertEquals("probe", response.getString("requestId"))
        assertFalse(opened)
        media.close()
    }
    @Test fun assetsOnlyRequestsTheMissingFieldsWithoutOpeningAudio() = runBlocking {
        val sent = Channel<JSONObject>(1)
        var fields: Set<String>? = null
        val media = ConnectMedia(this, { sent.trySend(it) }, { error("No audio probe expected") },
            { error("No audio download expected") }, { id, requested ->
                assertEquals("song-id", id)
                fields = requested
                JSONObject().put("lyrics", "[00:01.00]Saved lyrics")
            })
        media.receive(JSONObject().put("type", "mediaRequest").put("requestId", "assets")
            .put("operation", "assets").put("song", "song-id")
            .put("fields", org.json.JSONArray(listOf("lyrics", "unknown"))))
        val response = withTimeout(2000) { sent.receive() }
        assertTrue(response.getBoolean("ok"))
        assertEquals(setOf("lyrics"), fields)
        assertEquals("[00:01.00]Saved lyrics", response.getJSONObject("assets").getString("lyrics"))
        media.close()
    }

}
