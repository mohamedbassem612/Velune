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
import io.ktor.client.HttpClient
import io.ktor.client.engine.okhttp.OkHttp
import io.ktor.client.request.header
import io.ktor.client.request.post
import io.ktor.client.request.setBody
import io.ktor.client.statement.bodyAsText
import io.ktor.http.ContentType
import io.ktor.http.HttpHeaders
import io.ktor.http.contentType
import java.io.File
import java.time.LocalDateTime
import java.util.UUID
import java.util.concurrent.TimeUnit
import javax.inject.Inject
import javax.inject.Singleton
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.first
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.sync.Mutex
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import kotlinx.serialization.json.Json
import com.nikhil.yt.together.TogetherJoinInfo

enum class LibrarySyncStatus { Idle, Syncing, Synced, Retrying }

@Singleton
class LocalLibrarySync @Inject constructor(
    @ApplicationContext context: Context,
    private val database: MusicDatabase,
) {
    val status = MutableStateFlow(LibrarySyncStatus.Idle)
    private val mutex = Mutex()
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
            output.write(json.encodeToString(LibrarySnapshot.serializer(), snapshot.copy(history = emptyList())).toByteArray())
            file.finishWrite(output)
            journal = snapshot.copy(history = emptyList())
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
        val history = recentEvents(LibraryMerge.HISTORY_LIMIT).first().filterNot { it.song.song.isLocal }.map {
            SyncHistory(it.song.toSyncTrack(), it.event.timestamp.toString(), it.event.playTime)
        }
        LibraryMerge.observe(load(), LibrarySnapshot(device = device, likes = likes, playlists = lists, history = history))
    }

    suspend fun snapshot(): LibrarySnapshot = withContext(Dispatchers.IO) {
        mutex.withLock { capture().also { save(it) } }
    }

    suspend fun accept(remote: LibrarySnapshot): LibrarySnapshot = withContext(Dispatchers.IO) {
        validate(remote)
        mutex.withLock {
            val local = capture()
            val merged = LibraryMerge.merge(local, remote)
            val oldLikes = local.likes.associateBy { it.track.id }
            val oldLists = local.playlists.associateBy { it.id }
            database.withTransaction {
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
                    if (!localSyncEventExists(item.track.id, timestamp, item.playTime)) {
                        insert(item.track.toMetadata())
                        insert(Event(songId = item.track.id, timestamp = timestamp, playTime = item.playTime))
                        incrementTotalPlayTime(item.track.id, item.playTime)
                    }
                }
            }
            save(merged)
            status.value = LibrarySyncStatus.Synced
            merged
        }
    }

    suspend fun exchange(info: TogetherJoinInfo, participant: String) {
        status.value = LibrarySyncStatus.Syncing
        val body = json.encodeToString(LibrarySnapshot.serializer(), snapshot())
        require(body.toByteArray().size <= MAX_BODY_BYTES) { "Library is too large for this preview" }
        HttpClient(OkHttp) {
            engine { config { connectTimeout(10, TimeUnit.SECONDS); readTimeout(30, TimeUnit.SECONDS); writeTimeout(30, TimeUnit.SECONDS) } }
        }.use { client ->
            val response = client.post("http://${info.host}:${info.port}/library") {
                header(HttpHeaders.Authorization, "Bearer ${info.sessionKey}")
                header("X-Velune-Participant", participant)
                contentType(ContentType.Application.Json)
                setBody(body)
            }
            check(response.status.value == 200) { "Library sync unavailable (${response.status.value})" }
            val text = response.bodyAsText()
            require(text.toByteArray().size <= MAX_BODY_BYTES) { "Library response is too large" }
            accept(json.decodeFromString<LibrarySnapshot>(text))
        }
    }

    fun encode(snapshot: LibrarySnapshot): String = json.encodeToString(LibrarySnapshot.serializer(), snapshot)
    fun decode(raw: String): LibrarySnapshot = json.decodeFromString<LibrarySnapshot>(raw).also(::validate)

    private fun validate(snapshot: LibrarySnapshot) {
        require(snapshot.schema == 1 && snapshot.device.isNotBlank() && snapshot.counter in 0..Long.MAX_VALUE / 2)
        require(snapshot.likes.size <= 20_000 && snapshot.playlists.size <= 1000 && snapshot.history.size <= LibraryMerge.HISTORY_LIMIT)
        val tracks = snapshot.likes.map { it.track } + snapshot.playlists.flatMap { it.songs } + snapshot.history.map { it.track }
        require(tracks.size <= 50_000)
        tracks.forEach { require(it.id.isNotBlank() && it.id.length <= 256 && it.title.length <= 4096 && it.artists.size <= 100) }
        (snapshot.likes.map { it.version } + snapshot.playlists.map { it.version }).forEach {
            require(it.counter in 1..snapshot.counter && it.device.isNotBlank())
        }
        snapshot.playlists.forEach { require(it.id.isNotBlank() && it.name.length <= 4096 && it.songs.size <= 10_000) }
        snapshot.history.forEach { require(it.playTime >= 0); LocalDateTime.parse(it.timestamp) }
    }

    private fun Song.toSyncTrack() = SyncTrack(id, title, artists.map { it.name }, song.duration, thumbnailUrl)
    private fun SyncTrack.toMetadata() = MediaMetadata(id, title, artists.map { MediaMetadata.Artist(null, it) }, duration, thumbnail)

    companion object { const val MAX_BODY_BYTES = 10 * 1024 * 1024 }
}
