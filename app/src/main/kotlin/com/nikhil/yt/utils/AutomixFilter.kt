/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */

package com.nikhil.yt.utils

data class AutomixCandidate(
    val id: String,
    val title: String,
    val artist: String?,
    val album: String? = null,
)

data class AutomixBlocklist(
    val queueIds: Set<String> = emptySet(),
    val queueKeys: Set<String> = emptySet(),
    val historyIds: Set<String> = emptySet(),
    val historyKeys: Set<String> = emptySet(),
    val dislikedIds: Set<String> = emptySet(),
    val likedIds: Set<String> = emptySet(),
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

    private val arabicMarks = Regex("""[\u0610-\u061a\u0640\u064b-\u065f\u0670\u06d6-\u06ed]""")
    private val alefs = Regex("[أإآٱ]")
    private val recitationWords = setOf(
        "قران", "القران", "قرانيه", "تلاوه", "تلاوات", "سوره", "المصحف",
        "quran", "quraan", "koran", "surah", "surat", "sura", "surahs",
    )
    private val otherFormats = setOf(
        "تفسير", "شرح", "درس", "خطبه", "انشوده", "اغنيه", "ريمكس", "remix",
        "tafsir", "lecture", "podcast", "nasheed", "lyrics", "song",
    )
    // Only distinctive standalone names. Words like "الناس" can also be song titles.
    private val chapterTitles = setOf(
        "الفاتحه", "البقره", "ال عمران", "الكهف", "يس", "الاخلاص", "الفلق",
        "al fatiha", "al fatihah", "al baqarah", "al baqara", "al kahf",
    )
    // Common chapter names that also occur in music need independent reciter evidence.
    private val ambiguousChapters = setOf(
        "الرحمن", "الملك", "النور", "مريم", "طه", "يوسف", "الناس", "النجم", "القمر",
        "الواقعه", "الفجر", "الضحى", "النبا", "الدخان", "الفتح", "محمد", "الانسان",
    )
    private val reciterNames = setOf(
        "الحصري", "المنشاوي", "العفاسي", "السديس", "الشريم", "الدوسري", "عبد الباسط", "عبدالباسط",
    )
    private val filler = setOf("the", "and", "with", "official", "audio", "video", "lyrics", "feat", "ft", "من", "في")

    private val styles = listOf(
        setOf("acoustic", "unplugged", "اكوستيك"),
        setOf("instrumental", "piano", "بيانو"),
        setOf("lofi", "chillhop"),
        setOf("jazz", "جاز"),
        setOf("rap", "hiphop", "راب"),
        setOf("metal", "ميتال"),
        setOf("مهرجان", "مهرجانات"),
    )

    private class Profile(candidate: AutomixCandidate) {
        val artist = normalizeArtist(candidate.artist)
        val album = normalize(candidate.album.orEmpty())
        private val title = normalize(candidate.title)
        private val titleWords = title.split(' ').toSet()
        val terms = titleWords.filterTo(mutableSetOf()) { it.length > 1 && it !in filler && it !in recitationWords }
        private val styleTags = (titleWords + album.split(' ')).let { words ->
            styles.filter { style -> style.any { it in words } }
        }
        private val metadataWords = titleWords + album.split(' ') + artist.split(' ')
        val recitation = metadataWords.none { it in otherFormats } && (
            metadataWords.any { it in recitationWords } ||
                title in chapterTitles ||
                (title in ambiguousChapters && reciterNames.any { " $it " in " $artist " })
        )

        fun similarity(other: Profile): Int {
            val artistScore = if (artist.isNotEmpty() && artist == other.artist) 35 else 0
            val albumScore = if (album.isNotEmpty() && album == other.album) 45 else 0
            val shared = terms.count { it in other.terms }
            val titleScore = 24 * shared / (terms.size + other.terms.size - shared).coerceAtLeast(1)
            val styleScore = if (styleTags.any { it in other.styleTags }) 28 else 0
            return artistScore + albumScore + titleScore + styleScore
        }
    }

    private fun normalize(text: String): String =
        text.lowercase()
            .replace(arabicMarks, "")
            .replace(alefs, "ا")
            .replace('ة', 'ه')
            .replace("qur’an", "quran")
            .replace("qur'an", "quran")
            .replace(nonWord, " ")
            .trim()

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
        seed: AutomixCandidate? = null,
    ): List<String> {
        val seenKeys = HashSet<String>()
        val seenIds = HashSet<String>()
        val fresh = ArrayList<AutomixCandidate>()
        val replayed = ArrayList<AutomixCandidate>()

        val reference = seed?.let(::Profile)
        val seedKey = seed?.let { songKey(it.title, it.artist) }
        val scores = HashMap<String, Int>()
        // Feedback applies to this recording only. A renewed like overrides its saved dislike.
        val excludedIds = blocklist.dislikedIds - blocklist.likedIds
        val availableCandidates = candidates.filterNot { it.id in excludedIds }
        // Restrict a recitation seed when the provider supplies recognizable recitations.
        // Otherwise retain provider candidates, whose metadata may lack recitation markers.
        val hasRecognizedRecitation =
            reference?.recitation == true && availableCandidates.any { Profile(it).recitation }
        for ((index, candidate) in availableCandidates.withIndex()) {
            if (candidate.id == seed?.id || candidate.id in blocklist.queueIds || !seenIds.add(candidate.id)) continue
            val key = songKey(candidate.title, candidate.artist)
            if (key == seedKey || key in blocklist.queueKeys || key in seenKeys) continue
            val profile = reference?.let { Profile(candidate) }
            if (hasRecognizedRecitation && profile?.recitation != true) continue
            val similarity = if (reference != null && profile != null) reference.similarity(profile) else 0
            // Reward this recording without transferring feedback to its artist or genre.
            val likeBonus = if (candidate.id in blocklist.likedIds) 32 else 0
            scores[candidate.id] = similarity + likeBonus + (12 - index).coerceAtLeast(0)
            seenKeys.add(key)
            if (candidate.id in blocklist.historyIds || key in blocklist.historyKeys) {
                replayed.add(candidate)
            } else {
                fresh.add(candidate)
            }
        }

        if (reference != null || blocklist.likedIds.isNotEmpty()) {
            fresh.sortByDescending { scores[it.id] }
            replayed.sortByDescending { scores[it.id] }
        }
        // Prefer fresh results, then restore recent plays only when the pool is small.
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
        val artists = (already + items).associate { it.id to normalizeArtist(it.artist) }
        fun artistOf(item: AutomixCandidate) = artists.getValue(item.id)
        val counts = already.groupingBy(::artistOf).eachCount().toMutableMap()
        val remaining = ArrayList(items)
        val recent = ArrayDeque(already.takeLast(maxConsecutive).map(::artistOf))

        fun repeatsRecent(c: AutomixCandidate): Boolean {
            val artist = artistOf(c)
            return artist.isNotEmpty() && recent.size == maxConsecutive && recent.all { it == artist }
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
            recent.addLast(artist)
            if (recent.size > maxConsecutive) recent.removeFirst()
            if (artist.isNotEmpty()) counts[artist] = (counts[artist] ?: 0) + 1
        }
        return output
    }
}
