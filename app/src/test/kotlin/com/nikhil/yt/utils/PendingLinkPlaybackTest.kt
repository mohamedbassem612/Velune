package com.nikhil.yt.utils

import kotlinx.coroutines.CompletableDeferred
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.runBlocking
import kotlinx.coroutines.yield
import org.junit.Assert.assertEquals
import org.junit.Test

class PendingLinkPlaybackTest {
    @Test fun immediatelyReadyPlayerAcceptsAnotherLink() = runBlocking {
        val pending = PendingLinkPlayback<String>(CoroutineScope(coroutineContext + Dispatchers.Unconfined))
        val played = mutableListOf<String>()
        pending.submit("first")
        pending.playIfReady({}, { played.add(it) })
        pending.submit("second")
        pending.playIfReady({}, { played.add(it) })
        assertEquals(listOf("first", "second"), played)
    }

    @Test fun playsConsecutiveLinksIncludingTheSameLinkAgain() = runBlocking {
        val pending = PendingLinkPlayback<String>(this)
        val played = mutableListOf<String>()
        listOf("youtube-first", "spotify-match", "youtube-third", "youtube-third").forEach {
            pending.submit(it)
            pending.playIfReady({}, { song -> played.add(song) })
            yield()
        }
        assertEquals(listOf("youtube-first", "spotify-match", "youtube-third", "youtube-third"), played)
    }

    @Test fun newestLinkWinsWhileWaitingForServiceRestore() = runBlocking {
        val ready = CompletableDeferred<Unit>()
        val pending = PendingLinkPlayback<String>(this)
        val played = mutableListOf<String>()
        pending.submit("old")
        pending.playIfReady({ ready.await() }, { played.add(it) })
        yield()
        pending.submit("new")
        pending.playIfReady({ ready.await() }, { played.add(it) })
        yield()
        ready.complete(Unit)
        yield()
        assertEquals(listOf("new"), played)
    }

    @Test fun waitsForConnectionAndDoesNotPlayTwice() = runBlocking {
        val pending = PendingLinkPlayback<String>(this)
        val played = mutableListOf<String>()
        pending.submit("cold-start")
        assertEquals(emptyList<String>(), played)
        pending.playIfReady({}, { played.add(it) })
        pending.playIfReady({}, { played.add(it) })
        yield()
        pending.playIfReady({}, { played.add(it) })
        yield()
        assertEquals(listOf("cold-start"), played)
    }

    @Test fun cancelledLinkCannotPlayAndNextLinkStillWorks() = runBlocking {
        val ready = CompletableDeferred<Unit>()
        val pending = PendingLinkPlayback<String>(this)
        val played = mutableListOf<String>()
        pending.submit("cancelled")
        pending.playIfReady({ ready.await() }, { played.add(it) })
        yield()
        pending.clear()
        ready.complete(Unit)
        pending.submit("next")
        pending.playIfReady({}, { played.add(it) })
        yield()
        assertEquals(listOf("next"), played)
    }

    @Test fun serviceReconnectResumesPendingLinkUsingNewConnection() = runBlocking {
        val oldConnection = CompletableDeferred<Unit>()
        val pending = PendingLinkPlayback<String>(this)
        val played = mutableListOf<String>()
        pending.submit("song")
        pending.playIfReady({ oldConnection.await() }, { played.add("old:$it") })
        yield()
        pending.pause()
        pending.playIfReady({}, { played.add("new:$it") })
        oldConnection.complete(Unit)
        yield()
        assertEquals(listOf("new:song"), played)
    }
}
