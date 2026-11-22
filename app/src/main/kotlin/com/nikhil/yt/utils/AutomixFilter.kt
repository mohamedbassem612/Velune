/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.utils

/**
 * Quality layer for the automix pool (the songs added to the end of the queue).
 * YouTube decides which songs are related; this only cleans and reorders them:
 *  1. drops songs already in the queue, including other versions of the same song
 *  2. pushes recently played songs out (they only come back if too few fresh ones are left)
 *  3. spreads artists so the same artist is not repeated back to back
 * It never adds songs, and never changes YouTube's order except to spread artists.
 * No Android classes here so it can be unit tested.
 */
data class AutomixCandidate(
    val id: String,
    val title: String,
    val artist: String?,
)

data class AutomixBlocklist(
    val queueIds: Set<String> = emptySet(),
    val queueKeys: Set<String> = emptySet(),
    val historyIds: Set<String> = emptySet(),
    val historyKeys: Set<String> = emptySet(),
)

object AutomixFilter {
    const val MAX_CONSECUTIVE_SAME_ARTIST = 2
    const val MAX_PER_ARTIST = 4
    const val MIN_FRESH_BEFORE_REPLAY = 8

    private val bracketed = Regex("""[(\[{][^)\]}]*[)\]}]""")
    private val trailingVersion =
        Regex(
            """\s[-–—|:]\s.*\b(live|remix|remaster(ed)?|version|edit|acoustic|official|lyrics?|audio|video|visuali[sz]er|mix|hd|hq|4k)\b.*$""",
            RegexOption.IGNORE_CASE,
        )
    private val featTail = Regex("""\s(feat\.?|ft\.?|featuring)\s.*$""", RegexOption.IGNORE_CASE)
    private val nonWord = Regex("""[^\p{L}\p{N}]+""")

    private fun normalize(text: String): String =
        text.lowercase().replace(nonWord, " ").trim()

    fun normalizeTitle(title: String): String {
        val stripped =
            title
                .replace(bracketed, " ")
                .replace(trailingVersion, "")
                .replace(featTail, "")
        val result = normalize(stripped)
        return result.ifBlank { normalize(title) }
    }

    fun normalizeArtist(artist: String?): String = artist?.let(::normalize).orEmpty()

    fun songKey(title: String, artist: String?): String =
        normalizeTitle(title) + "|" + normalizeArtist(artist)

    /** Returns the ids to keep, in the order they should be added to the queue. */
    fun refine(
        candidates: List<AutomixCandidate>,
        blocklist: AutomixBlocklist,
        maxPerArtist: Int = MAX_PER_ARTIST,
        maxConsecutive: Int = MAX_CONSECUTIVE_SAME_ARTIST,
    ): List<String> {
        val seenKeys = HashSet<String>()
        val seenIds = HashSet<String>()
        val fresh = ArrayList<AutomixCandidate>()
        val replayed = ArrayList<AutomixCandidate>()

        for (candidate in candidates) {
            if (candidate.id in blocklist.queueIds || !seenIds.add(candidate.id)) continue
            val key = songKey(candidate.title, candidate.artist)
            if (key in blocklist.queueKeys) continue
            if (!seenKeys.add(key)) continue
            if (candidate.id in blocklist.historyIds || key in blocklist.historyKeys) {
                replayed.add(candidate)
            } else {
                fresh.add(candidate)
            }
        }

        val result = spreadArtists(fresh, maxPerArtist, maxConsecutive)
        if (result.size < MIN_FRESH_BEFORE_REPLAY) {
            result += spreadArtists(replayed, maxPerArtist, maxConsecutive, result)
        }
        return result.map { it.id }
    }

    private fun spreadArtists(
        items: List<AutomixCandidate>,
        maxPerArtist: Int,
        maxConsecutive: Int,
        already: List<AutomixCandidate> = emptyList(),
    ): MutableList<AutomixCandidate> {
        val output = ArrayList<AutomixCandidate>(items.size)
        val counts = HashMap<String, Int>()
        val remaining = ArrayList(items)

        fun artistOf(c: AutomixCandidate) = normalizeArtist(c.artist)

        fun repeatsRecent(c: AutomixCandidate): Boolean {
            val artist = artistOf(c)
            if (artist.isEmpty()) return false
            val recent = (already + output).takeLast(maxConsecutive)
            return recent.size == maxConsecutive && recent.all { artistOf(it) == artist }
        }

        while (remaining.isNotEmpty()) {
            val pick =
                remaining.firstOrNull { c ->
                    val artist = artistOf(c)
                    !repeatsRecent(c) && (artist.isEmpty() || (counts[artist] ?: 0) < maxPerArtist)
                } ?: remaining.firstOrNull { !repeatsRecent(it) } ?: remaining.first()
            remaining.remove(pick)
            output.add(pick)
            val artist = artistOf(pick)
            if (artist.isNotEmpty()) counts[artist] = (counts[artist] ?: 0) + 1
        }
        return output
    }
}
