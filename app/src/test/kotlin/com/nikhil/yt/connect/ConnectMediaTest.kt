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
            .put("operation", "open").put("song", "song-id").put("cachedOnly", true)
            .put("position", 0).put("length", -1))
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

    @Test fun audioPreparationFailureDoesNotBlockOtherRequests() = runBlocking {
        val sent = Channel<JSONObject>(4)
        val preparing = CompletableDeferred<Unit>()
        val finish = CompletableDeferred<Unit>()
        var sourceOpened = false
        val media = ConnectMedia(this, { sent.trySend(it) }, { false },
            { sourceOpened = true; error("Must not open an incomplete cache") },
            ensureCached = {
                preparing.complete(Unit)
                finish.await()
                throw IOException("Cache fill failed")
            })
        media.receive(JSONObject().put("type", "mediaRequest").put("requestId", "audio")
            .put("operation", "open").put("song", "song-id").put("position", 0).put("length", -1))
        withTimeout(2000) { preparing.await() }
        assertFalse(sourceOpened)
        // Another operation can run while audio is being downloaded on the phone.
        media.receive(JSONObject().put("type", "mediaRequest").put("requestId", "assets")
            .put("operation", "assets").put("song", "song-id"))
        val assets = withTimeout(2000) { sent.receive() }
        assertEquals("assets", assets.getString("requestId"))
        assertTrue(assets.getBoolean("ok"))
        finish.complete(Unit)
        val audio = withTimeout(2000) { sent.receive() }
        assertEquals("audio", audio.getString("requestId"))
        assertFalse(audio.getBoolean("ok"))
        assertFalse(sourceOpened)
        media.close()
    }

    @Test fun abandoningAnOpenNotifiesThePhoneToReleaseItsReader() = runBlocking {
        val sent = Channel<JSONObject>(4)
        val media = ConnectMedia(this, { sent.trySend(it) }, { false }, { error("No source") })
        val opening = launch { media.request(JSONObject().put("operation", "open")) }
        val request = sent.receive()
        opening.cancelAndJoin()
        val cancel = withTimeout(2000) { sent.receive() }
        assertEquals("mediaCancel", cancel.getString("type"))
        assertEquals(request.getString("requestId"), cancel.getString("requestId"))
        media.close()
    }

    @Test fun cancelingAPreparingOpenDoesNotLeaveAWaitingRpcTask() = runBlocking {
        val started = CompletableDeferred<Unit>()
        val canceled = CompletableDeferred<Unit>()
        val media = ConnectMedia(this, {}, { false }, { error("Audio must not open") },
            ensureCached = {
                started.complete(Unit)
                try { awaitCancellation() } finally { canceled.complete(Unit) }
            })
        media.receive(JSONObject().put("type", "mediaRequest").put("requestId", "abandoned")
            .put("operation", "open").put("song", "song-id").put("position", 0).put("length", -1))
        withTimeout(2000) { started.await() }
        media.receive(JSONObject().put("type", "mediaCancel").put("requestId", "abandoned"))
        withTimeout(2000) { canceled.await() }
        media.close()
    }

}
