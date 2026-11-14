package com.nikhil.yt.ui.tv

import android.content.Context
import androidx.lifecycle.ViewModel
import androidx.lifecycle.viewModelScope
import com.nikhil.yt.R
import com.nikhil.yt.constants.HideExplicitKey
import com.nikhil.yt.constants.HideVideoKey
import com.nikhil.yt.db.MusicDatabase
import com.nikhil.yt.db.entities.Playlist
import com.nikhil.yt.db.entities.PlaylistEntity
import com.nikhil.yt.db.entities.PlaylistSongMap
import com.nikhil.yt.innertube.YouTube
import com.nikhil.yt.innertube.models.*
import com.nikhil.yt.innertube.pages.AlbumPage
import com.nikhil.yt.models.MediaMetadata
import com.nikhil.yt.models.toMediaMetadata
import com.nikhil.yt.utils.dataStore
import com.nikhil.yt.utils.get
import dagger.hilt.android.lifecycle.HiltViewModel
import dagger.hilt.android.qualifiers.ApplicationContext
import java.time.LocalDateTime
import javax.inject.Inject
import kotlinx.coroutines.*
import kotlinx.coroutines.flow.*

internal const val TV_LIKED_ID = "#liked"

data class TvCollection(
    val id: String,
    val title: String,
    val thumbnail: String? = null,
    val subtitle: String = "",
    val album: Boolean = false,
    val year: Int? = null,
    val local: Boolean = false,
    val artist: Boolean = false,
    val artwork: Int? = null,
    val radioSeed: String? = null,
)

internal val TvCollection.typeLabel: Int
    get() = when { album -> R.string.tv_album; artist -> R.string.tv_artist; radioSeed != null -> R.string.tv_radio; else -> R.string.tv_playlist }

fun AlbumItem.toTvCollection() = TvCollection(id, title, thumbnail, artists.orEmpty().joinToString { it.name }, album = true, year = year)
fun PlaylistItem.toTvCollection() = TvCollection(id, title, thumbnail, author?.name.orEmpty())

data class TvCollectionState(
    val collection: TvCollection? = null,
    val songs: List<MediaMetadata> = emptyList(),
    val loading: Boolean = false,
    val failed: Boolean = false,
    val saved: Boolean = false,
    val saving: Boolean = false,
)

@HiltViewModel
class TvCollectionViewModel @Inject constructor(
    val database: MusicDatabase,
    @ApplicationContext private val context: Context,
) : ViewModel() {
    private val mutableState = MutableStateFlow(TvCollectionState())
    val state = mutableState.asStateFlow()
    private val mutableMessages = MutableSharedFlow<Int>(extraBufferCapacity = 4)
    val messages = mutableMessages.asSharedFlow()
    private var loadJob: Job? = null
    private var albumPage: AlbumPage? = null

    fun open(collection: TvCollection) {
        loadJob?.cancel()
        albumPage = null
        mutableState.value = TvCollectionState(collection, loading = true)
        loadJob = viewModelScope.launch {
            try {
                if (collection.id == TV_LIKED_ID) {
                    database.likedSongsByCreateDateAsc().collect { songs ->
                        mutableState.value = TvCollectionState(collection, songs.asReversed().map { it.toMediaMetadata() }, saved = true)
                    }
                    return@launch
                }
                val loaded = withContext(Dispatchers.IO) {
                    val hideExplicit = context.dataStore.get(HideExplicitKey, false)
                    val hideVideo = context.dataStore.get(HideVideoKey, false)
                    if (collection.radioSeed != null) {
                        // Radio is unbounded. Present its current batch as a finite, saveable playlist.
                        val radio = YouTube.next(WatchEndpoint(videoId = collection.radioSeed)).getOrThrow()
                        val songs = radio.items.filterExplicit(hideExplicit).filterVideo(hideVideo).distinctBy { it.id }.map { it.toMediaMetadata() }
                        check(songs.isNotEmpty()) { "Empty radio" }
                        TvCollectionState(collection, songs, saved = database.playlist(collection.id).first() != null)
                    } else if (collection.local) {
                        if (collection.artist) {
                            val artist = database.artist(collection.id).first()
                            TvCollectionState(collection, database.artistSongsByCreateDateAsc(collection.id).first().asReversed().map { it.toMediaMetadata() }, saved = artist?.artist?.bookmarkedAt != null)
                        } else if (collection.album) {
                            val album = database.album(collection.id).first()
                            TvCollectionState(collection, database.albumSongs(collection.id).first().map { it.toMediaMetadata() }, saved = album?.album?.bookmarkedAt != null)
                        } else {
                            val playlist = database.playlist(collection.id).first()
                            val localSongs = database.playlistSongs(collection.id).first().map { it.song.toMediaMetadata() }
                            val songs = if (localSongs.isEmpty() && playlist?.playlist?.browseId != null) {
                                remotePlaylist(playlist.playlist.browseId).second.filterExplicit(hideExplicit).filterVideo(hideVideo).map { it.toMediaMetadata() }
                            } else localSongs
                            TvCollectionState(collection, songs, saved = true)
                        }
                    } else if (collection.album) {
                        val page = YouTube.album(collection.id).getOrThrow()
                        ensureActive()
                        albumPage = page
                        TvCollectionState(page.album.toTvCollection(), page.songs.filterExplicit(hideExplicit).filterVideo(hideVideo).map { it.toMediaMetadata() }, saved = database.album(collection.id).first()?.album?.bookmarkedAt != null)
                    } else {
                        val (playlist, songs) = remotePlaylist(collection.id)
                        TvCollectionState(playlist.toTvCollection(), songs.filterExplicit(hideExplicit).filterVideo(hideVideo).map { it.toMediaMetadata() }, saved = database.playlistByBrowseId(collection.id).first()?.playlist?.bookmarkedAt != null)
                    }
                }
                ensureActive()
                val details = loaded.collection
                val mainArtists = loaded.songs.flatMap { it.artists }.groupingBy { it.name }.eachCount()
                    .entries.sortedByDescending { it.value }.take(3).joinToString { it.key }
                mutableState.value = if (details != null && !details.album && !details.artist && mainArtists.isNotBlank()) {
                    loaded.copy(collection = details.copy(subtitle = mainArtists))
                } else loaded
            } catch (cancelled: CancellationException) {
                throw cancelled
            } catch (_: Exception) {
                mutableState.value = TvCollectionState(collection, failed = true)
            }
        }
    }

    fun close() {
        loadJob?.cancel()
        mutableState.value = TvCollectionState()
    }

    private suspend fun remotePlaylist(id: String): Pair<PlaylistItem, List<SongItem>> {
        val page = YouTube.playlist(id).getOrThrow()
        val songs = collectTvPlaylistPages(page.songs, page.songsContinuation) { token ->
            val next = YouTube.playlistContinuation(token).getOrThrow()
            next.songs to next.continuation
        }
        return page.playlist to songs
    }

    fun save() {
        val snapshot = state.value
        val collection = snapshot.collection ?: return
        if (snapshot.saved || snapshot.saving || snapshot.loading || snapshot.failed) return
        val page = albumPage?.takeIf { it.album.browseId == collection.id }
        mutableState.value = snapshot.copy(saving = true)
        action {
            try {
                database.withTransaction {
                    if (collection.artist) {
                        val artist = artist(collection.id).first()?.artist ?: error("Missing artist")
                        update(artist.copy(bookmarkedAt = LocalDateTime.now()))
                    } else if (collection.album) {
                        if (page != null) insert(page)
                        val album = album(collection.id).first()?.album ?: error("Missing album")
                        update(album.copy(bookmarkedAt = LocalDateTime.now(), inLibrary = album.inLibrary ?: LocalDateTime.now()))
                    } else {
                        val isRadio = collection.radioSeed != null
                        val existing = if (collection.local || isRadio) playlist(collection.id).first() else playlistByBrowseId(collection.id).first()
                        val entity = existing?.playlist?.copy(bookmarkedAt = LocalDateTime.now()) ?: PlaylistEntity(
                            id = if (isRadio) collection.id else PlaylistEntity.generatePlaylistId(),
                            name = collection.title, browseId = if (isRadio) null else collection.id,
                            thumbnailUrl = collection.thumbnail, isEditable = isRadio, isLocal = isRadio,
                            bookmarkedAt = LocalDateTime.now(), remoteSongCount = snapshot.songs.size,
                        )
                        if (existing == null) {
                            insert(entity)
                            snapshot.songs.forEachIndexed { index, song ->
                                insert(song)
                                insert(PlaylistSongMap(songId = song.id, playlistId = entity.id, position = index))
                            }
                        } else update(entity)
                    }
                }
                mutableMessages.emit(R.string.tv_saved_library)
                if (state.value.collection?.id == collection.id) mutableState.update { it.copy(saved = true) }
            } finally {
                if (state.value.collection?.id == collection.id) mutableState.update { it.copy(saving = false) }
            }
        }
    }

    fun toggleLike(song: MediaMetadata) = action {
        database.withTransaction {
            insert(song)
            val stored = getSongByIdBlocking(song.id) ?: error("Missing song")
            update(stored.song.toggleLike())
        }
    }

    fun addToPlaylist(song: MediaMetadata, playlist: Playlist?, newName: String = "") = action {
        require(playlist != null || newName.isNotBlank())
        if (playlist != null) {
            require(playlist.playlist.isEditable)
            if (database.checkInPlaylist(playlist.id, song.id) > 0) {
                mutableMessages.emit(R.string.tv_song_added)
                return@action
            }
            playlist.playlist.browseId?.let { YouTube.addToPlaylist(it, song.id).getOrThrow() }
        }
        database.withTransaction {
            val target = playlist ?: run {
                val entity = PlaylistEntity(name = newName.trim(), isLocal = true, bookmarkedAt = LocalDateTime.now())
                insert(entity)
                database.playlist(entity.id).first() ?: error("Missing playlist")
            }
            insert(song)
            if (checkInPlaylist(target.id, song.id) == 0) {
                val current = database.playlist(target.id).first() ?: error("Missing playlist")
                addSongToPlaylist(current, listOf(song.id))
            }
        }
        mutableMessages.emit(R.string.tv_song_added)
    }

    private fun action(block: suspend () -> Unit) = viewModelScope.launch(Dispatchers.IO) {
        try { block() }
        catch (cancelled: CancellationException) { throw cancelled }
        catch (_: Exception) { mutableMessages.emit(R.string.tv_action_failed) }
    }
}
