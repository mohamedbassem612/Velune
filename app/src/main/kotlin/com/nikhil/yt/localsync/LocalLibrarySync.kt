package com.nikhil.yt.localsync

import android.content.Context
import android.util.AtomicFile
import com.nikhil.yt.db.MusicDatabase
import com.nikhil.yt.db.entities.Event
import com.nikhil.yt.db.entities.PlaylistEntity
import com.nikhil.yt.db.entities.PlaylistSongMap
import com.nikhil.yt.db.entities.Song
import com.nikhil.yt.models.MediaMetadata
import dagger.hilt.android.qualifiers.ApplicationContext
import java.io.File
import java.time.LocalDateTime
import java.util.UUID
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json

enum class LibrarySyncStatus { Idle, Syncing, Synced, Retrying }

@Singleton
class LocalLibrarySync @Inject constructor(
    @ApplicationContext context: Context,
    private val database: MusicDatabase,
) {
    val status = MutableStateFlow(LibrarySyncStatus.Idle)
    private val mutex = Mutex()
    private val connectPreferences = context.getSharedPreferences("tv_library_owner", Context.MODE_PRIVATE)
    fun hasConnectLibrary() = connectPreferences.contains("peer")
    fun ownsConnectLibrary(peerId: String) = connectPreferences.getString("peer", null) == peerId
    private val file = AtomicFile(File(context.noBackupFilesDir, "local-library-sync.json"))
    private val json = Json { ignoreUnknownKeys = true; encodeDefaults = true }
    // Device identity and conflict clocks must not be cloned by Android backup.
    private val device = AtomicFile(File(context.noBackupFilesDir, "local-library-sync-device")).let { identity ->
        if (identity.baseFile.exists() || File(identity.baseFile.path + ".bak").exists()) {
            identity.openRead().bufferedReader().use { it.readText() }.also { UUID.fromString(it) }
        } else {
            val id = UUID.randomUUID().toString()
            val output = identity.startWrite()
            try {
                output.write(id.toByteArray())
                identity.finishWrite(output)
            } catch (failure: Exception) {
                identity.failWrite(output)
                throw failure
            }
            id
        }
    }
    private var journal: LibrarySnapshot? = null

    private fun load(): LibrarySnapshot {
        journal?.let { return it }
        val loaded = if (file.baseFile.exists() || File(file.baseFile.path + ".bak").exists()) {
            // Fail visibly on corrupt journals: silently resetting clocks can resurrect removals.
            json.decodeFromString<LibrarySnapshot>(file.openRead().bufferedReader().use { it.readText() })
        } else LibrarySnapshot(device = device)
        require(loaded.device == device && loaded.schema == 1) { "Invalid local sync journal" }
        journal = loaded
        return loaded
    }

    private fun save(snapshot: LibrarySnapshot) {
        val output = file.startWrite()
        try {
            output.write(json.encodeToString(LibrarySnapshot.serializer(), snapshot).toByteArray())
            file.finishWrite(output)
            journal = snapshot
        } catch (failure: Exception) {
            file.failWrite(output)
            throw failure
        }
    }

    private suspend fun capture(): LibrarySnapshot = database.withTransaction {
        val likes = likedSongsByRowIdAsc().first().filterNot { it.song.isLocal }
            .map { SyncLike(it.toSyncTrack(), true) }
        val lists = playlistsByCreateDateAsc().first().filterNot { it.playlist.isLocal }.map { list ->
            SyncPlaylist(
                id = list.id, name = list.title, browseId = list.playlist.browseId,
                thumbnail = list.playlist.thumbnailUrl,
                songs = playlistSongs(list.id).first().map { it.song }.filterNot { it.song.isLocal }.map { it.toSyncTrack() },
            )
        }
        val previous = load()
        val recent = recentEvents(LibraryMerge.HISTORY_LIMIT).first().filterNot { it.song.song.isLocal }.map {
            SyncHistory(it.song.toSyncTrack(), it.event.timestamp.toString(), it.event.playTime)
        }
        val recentKeys = recent.mapTo(hashSetOf()) { it.key }
        // Falling outside the recent window is not deletion. Check previously observed events explicitly.
        val retained = previous.history.filter { !it.removed && it.key !in recentKeys &&
            localSyncEventExists(it.track.id, LocalDateTime.parse(it.timestamp), it.playTime) }
        LibraryMerge.observe(previous, LibrarySnapshot(device = device, likes = likes, playlists = lists, history = recent + retained))
    }

    suspend fun snapshot(connect: Boolean = false): LibrarySnapshot = withContext(Dispatchers.IO) {
        mutex.withLock { capture().also { save(it) }.let { if (connect) it else it.copy(history = it.history.filterNot { event -> event.removed }) } }
    }

    suspend fun accept(remote: LibrarySnapshot, connect: Boolean = false, connectPeer: String? = null, authorized: () -> Boolean = { true }): LibrarySnapshot = withContext(Dispatchers.IO) {
        validate(remote)
        mutex.withLock {
            check(authorized()) { "Library peer is no longer authorized" }
            val replacing = connectPeer != null && !ownsConnectLibrary(connectPeer)
            val previous = capture()
            val local = if (replacing) LibrarySnapshot(device = device) else previous
            val merged = LibraryMerge.forConnect(previous, remote, replacing)
            val oldLikes = local.likes.associateBy { it.track.id }
            val oldLists = local.playlists.associateBy { it.id }
            database.withTransaction {
                check(authorized()) { "Library peer is no longer authorized" }
                if (replacing) {
                    // The TV presents one phone library. Never export removals to the old phone.
                    clearConnectSongLibrary()
                    playlistsByCreateDateAsc().first().filterNot { it.playlist.isLocal }.forEach {
                        delete(it.playlist)
                    }
                    clearConnectListenHistory()
                }
                merged.likes.filter { it.version > (oldLikes[it.track.id]?.version ?: SyncVersion()) }.forEach { item ->
                    insert(item.track.toMetadata())
                    val existing = song(item.track.id).first()?.song ?: error("Missing synced song")
                    update(existing.copy(liked = item.liked, likedDate = if (item.liked) existing.likedDate ?: LocalDateTime.now() else null,
                        inLibrary = if (item.liked) existing.inLibrary ?: LocalDateTime.now() else existing.inLibrary))
                }
                merged.playlists.filter { it.version > (oldLists[it.id]?.version ?: SyncVersion()) }.forEach { item ->
                    val existing = playlist(item.id).first()?.playlist
                    if (item.removed) {
                        existing?.let { update(it.copy(bookmarkedAt = null)) }
                    } else {
                        val entity = (existing ?: PlaylistEntity(id = item.id, name = item.name)).copy(
                            name = item.name, browseId = item.browseId, thumbnailUrl = item.thumbnail,
                            bookmarkedAt = existing?.bookmarkedAt ?: LocalDateTime.now(),
                            isEditable = existing?.isEditable ?: (item.browseId == null),
                            lastUpdateTime = LocalDateTime.now(),
                        )
                        if (existing == null) insert(entity) else update(entity)
                        clearPlaylist(item.id)
                        item.songs.forEachIndexed { position, track ->
                            insert(track.toMetadata())
                            insert(PlaylistSongMap(playlistId = item.id, songId = track.id, position = position))
                        }
                    }
                }
                merged.history.forEach { item ->
                    val timestamp = LocalDateTime.parse(item.timestamp)
                    if (item.removed) {
                        deleteLocalSyncEvent(item.track.id, timestamp, item.playTime)
                    } else if (!localSyncEventExists(item.track.id, timestamp, item.playTime)) {
                        insert(item.track.toMetadata())
                        insert(Event(songId = item.track.id, timestamp = timestamp, playTime = item.playTime))
                        incrementTotalPlayTime(item.track.id, item.playTime)
                    }
                }
            }
            save(merged)
            if (connectPeer != null) connectPreferences.edit().putString("peer", connectPeer).commit()
            status.value = LibrarySyncStatus.Synced
            if (connect) merged else merged.copy(history = merged.history.filterNot { it.removed })
        }
    }

    fun encode(snapshot: LibrarySnapshot): String = json.encodeToString(LibrarySnapshot.serializer(), snapshot)
    fun decode(raw: String): LibrarySnapshot = json.decodeFromString<LibrarySnapshot>(raw).also(::validate)

    private fun validate(snapshot: LibrarySnapshot) {
        require(snapshot.schema == 1 && snapshot.device.isNotBlank() && snapshot.counter in 0..Long.MAX_VALUE / 2)
        require(snapshot.likes.size <= 20_000 && snapshot.playlists.size <= 1000 && snapshot.history.size <= 50_000)
        val tracks = snapshot.likes.map { it.track } + snapshot.playlists.flatMap { it.songs } + snapshot.history.map { it.track }
        require(tracks.size <= 50_000)
        tracks.forEach { require(it.id.isNotBlank() && it.id.length <= 256 && it.title.length <= 4096 && it.artists.size <= 100) }
        (snapshot.likes.map { it.version } + snapshot.playlists.map { it.version }).forEach {
            require(it.counter in 1..snapshot.counter && it.device.isNotBlank())
        }
        snapshot.playlists.forEach { require(it.id.isNotBlank() && it.name.length <= 4096 && it.songs.size <= 10_000) }
        snapshot.history.forEach {
            require(it.playTime >= 0 && it.version.counter in 0..snapshot.counter)
            if (it.version.counter > 0) require(it.version.device.isNotBlank())
            LocalDateTime.parse(it.timestamp)
        }
    }

    private fun Song.toSyncTrack() = SyncTrack(id, title, artists.map { it.name }, song.duration, thumbnailUrl)
    private fun SyncTrack.toMetadata() = MediaMetadata(id, title, artists.map { MediaMetadata.Artist(null, it) }, duration, thumbnail)

    companion object { const val MAX_BODY_BYTES = 10 * 1024 * 1024 }
}
