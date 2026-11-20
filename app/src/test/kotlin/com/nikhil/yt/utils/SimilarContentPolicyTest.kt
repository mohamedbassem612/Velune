package com.nikhil.yt.utils

import org.junit.Assert.*
import org.junit.Test

class SimilarContentPolicyTest {
    @Test fun limitingAutofillDoesNotTruncateTheSimilarShelf() {
        val source = (1..60).map { "similar$it" }
        val ranked = source.take(20).reversed()
        val batch = SimilarContentPolicy.select(source, ranked, emptySet(), { it })
        assertEquals(ranked.take(10), batch.queue)
        assertEquals(source.filterNot { it in batch.queue }, batch.preview)
        assertEquals(50, batch.preview.size)
    }

    @Test fun personalTasteFilteringDoesNotRemoveSongRelatedResultsFromTheShelf() {
        val source = listOf("song-related", "favorite", "song-related-2", "skipped-before")
        val batch = SimilarContentPolicy.select(source, listOf("favorite"), emptySet(), { it })
        assertEquals(listOf("favorite"), batch.queue)
        assertEquals(listOf("song-related", "song-related-2", "skipped-before"), batch.preview)
    }

    @Test fun anEmptyPersonalizedSelectionStillLeavesVisibleSimilarContent() {
        val source = listOf("one", "two", "three")
        val batch = SimilarContentPolicy.select(source, emptyList(), emptySet(), { it })
        assertTrue(batch.queue.isEmpty())
        assertEquals(source, batch.preview)
    }

    @Test fun newlyQueuedAndDuplicateTracksAreRemovedFromBothOutputs() {
        val source = listOf("current", "one", "queued", "two", "two", "three")
        val batch = SimilarContentPolicy.select(source, listOf("one", "one", "queued"),
            setOf("current", "queued"), { it })
        assertEquals(listOf("one"), batch.queue)
        assertEquals(listOf("two", "three"), batch.preview)
    }

    @Test fun zeroAutofillKeepsTheOriginalSourceOrder() {
        val source = listOf("one", "two", "three")
        val batch = SimilarContentPolicy.select(source, source.reversed(), emptySet(), { it }, limit = 0)
        assertTrue(batch.queue.isEmpty())
        assertEquals(source, batch.preview)
    }
}
