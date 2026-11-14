package com.nikhil.yt.ui.tv

import kotlinx.coroutines.currentCoroutineContext
import kotlinx.coroutines.ensureActive

/** Keep ordering and repeated tracks; fail rather than silently acting on a partial list. */
internal suspend fun <T> collectTvPlaylistPages(
    initial: List<T>,
    continuation: String?,
    loadPage: suspend (String) -> Pair<List<T>, String?>,
): List<T> {
    val songs = initial.toMutableList()
    var token = continuation
    val seen = mutableSetOf<String>()
    while (token != null) {
        currentCoroutineContext().ensureActive()
        check(seen.add(token)) { "Repeated playlist continuation" }
        val (nextSongs, nextToken) = loadPage(token)
        songs += nextSongs
        token = nextToken
    }
    return songs
}
