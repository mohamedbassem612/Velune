package com.nikhil.yt.connect

import com.nikhil.yt.localsync.LibrarySnapshot

internal fun libraryDelta(current: LibrarySnapshot, base: LibrarySnapshot): LibrarySnapshot {
    val likes = base.likes.associateBy { it.track.id }
    val lists = base.playlists.associateBy { it.id }
    val history = base.history.associateBy { it.key }
    return current.copy(
        likes = current.likes.filter { likes[it.track.id] != it },
        playlists = current.playlists.filter { lists[it.id] != it },
        history = current.history.filter { history[it.key] != it },
    )
}
