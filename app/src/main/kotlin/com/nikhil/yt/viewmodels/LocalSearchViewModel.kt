/*
 * Velune - by Nikhil
 * Nikhil
 * Licensed Under GPL-3.0
 */



package com.nikhil.yt.viewmodels

import android.content.Context
import androidx.media3.datasource.cache.Cache
import com.nikhil.yt.di.DownloadCache
import com.nikhil.yt.di.PlayerCache
import com.nikhil.yt.playback.Mp3Library
import com.nikhil.yt.utils.onlineFlow
import dagger.hilt.android.qualifiers.ApplicationContext
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nikhil.yt.db.MusicDatabase
import com.nikhil.yt.db.entities.Album
import com.nikhil.yt.db.entities.Artist
import com.nikhil.yt.db.entities.LocalItem
import com.nikhil.yt.db.entities.Playlist
import com.nikhil.yt.db.entities.Song
import dagger.hilt.android.lifecycle.HiltViewModel
import kotlinx.coroutines.ExperimentalCoroutinesApi
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.flatMapLatest
import kotlinx.coroutines.flow.flowOf
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.flowOn
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.map
import kotlinx.coroutines.flow.stateIn
import javax.inject.Inject

@OptIn(ExperimentalCoroutinesApi::class)
@HiltViewModel
class LocalSearchViewModel
@Inject
constructor(
    database: MusicDatabase,
    @ApplicationContext context: Context,
    @PlayerCache private val playerCache: Cache,
    @DownloadCache private val downloadCache: Cache,
    private val mp3Library: Mp3Library,
) : ViewModel() {
    val query = MutableStateFlow("")
    val filter = MutableStateFlow(LocalFilter.ALL)

    val result =
        combine(query, filter, context.onlineFlow()) { query, filter, online ->
            Triple(query, filter, online)
        }.flatMapLatest { (query, filter, online) ->
            if (query.isEmpty()) {
                flowOf(LocalSearchResult("", filter, emptyMap()))
            } else {
                (when (filter) {
                    LocalFilter.ALL ->
                        combine(
                            if (online) database.searchSongs(query, PREVIEW_SIZE) else database.searchOfflineSongs(query),
                            database.searchAlbums(query, if (online) PREVIEW_SIZE else Int.MAX_VALUE),
                            database.searchArtists(query, if (online) PREVIEW_SIZE else Int.MAX_VALUE),
                            database.searchPlaylists(query, if (online) PREVIEW_SIZE else Int.MAX_VALUE),
                        ) { songs, albums, artists, playlists ->
                            songs + albums + artists + playlists
                        }

                    LocalFilter.SONG -> if (online) database.searchSongs(query) else database.searchOfflineSongs(query)
                    LocalFilter.ALBUM -> database.searchAlbums(query)
                    LocalFilter.ARTIST -> database.searchArtists(query)
                    LocalFilter.PLAYLIST -> database.searchPlaylists(query)
                }).map { list ->
                    fun playable(song: Song): Boolean {
                        val id = song.song.id
                        val length = song.format?.contentLength ?: 0L
                        return mp3Library.playableUri(id) != null || (length > 0 &&
                            (downloadCache.isCached(id, 0, length) || playerCache.isCached(id, 0, length)))
                    }
                    val visible = if (online) list else list.filter { item ->
                        when (item) {
                            is Song -> playable(item)
                            is Album -> database.albumWithSongs(item.id).first()?.songs?.any(::playable) == true
                            is Artist -> database.artistSongsByCreateDateAsc(item.id).first().any(::playable)
                            is Playlist -> database.playlistSongs(item.id).first().any { playable(it.song) }
                        }
                    }
                    LocalSearchResult(
                        query = query,
                        filter = filter,
                        map =
                        visible.groupBy {
                            when (it) {
                                is Song -> LocalFilter.SONG
                                is Album -> LocalFilter.ALBUM
                                is Artist -> LocalFilter.ARTIST
                                is Playlist -> LocalFilter.PLAYLIST
                            }
                        },
                    )
                }
            }
        }.flowOn(Dispatchers.IO).stateIn(
            viewModelScope,
            SharingStarted.Lazily,
            LocalSearchResult("", filter.value, emptyMap())
        )

    companion object {
        const val PREVIEW_SIZE = 3
    }
}

enum class LocalFilter {
    ALL,
    SONG,
    ALBUM,
    ARTIST,
    PLAYLIST,
}

data class LocalSearchResult(
    val query: String,
    val filter: LocalFilter,
    val map: Map<LocalFilter, List<LocalItem>>,
)
