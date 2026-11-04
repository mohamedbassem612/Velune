# Offline support

Changes after `9d9b46d` on `velune-offline-fixes`:

- Local search includes cached songs outside the library and filters out incomplete caches and missing MP3 files while offline. Albums, artists and playlists are included when they contain playable local audio. Database/cache reads run on IO.
- Album, artist, playlist, continuation, library, explore and browse-based lyrics responses are saved after successful online reads. Offline reads use those snapshots. Unvisited pages report unavailable; this does not download the entire YouTube catalogue.
- Snapshots are separated by session/account and locale, and limited to 64 MiB per partition. Audio cache and download storage retain their existing settings and storage UI.
- Starting a queue offline filters out unavailable audio. Autoloading online recommendations is disabled offline. A playback failure skips to the next fully available queued song, or pauses. Missing audio does not attempt to resolve a streaming URL offline.
- Likes, playlist likes, rename, add, remove and reorder operations on existing remote playlists are persisted when disconnected. Replay preserves order and removes each operation after success. Replay runs on app startup and every 30 seconds while the app is alive; opening the app after a restart resumes it. Account/session changes isolate pending edits. Online library synchronization flushes pending edits before reading the server; it does not run offline.
- Completed downloads prefetch artwork and lyrics. Failed downloads are retried on connectivity restoration, at most three restoration attempts per song; completion resets that counter. Media3 continues to manage network requirements, progress and partial-download resumption.
- Offline Home refreshes availability after file deletion/download changes. The connection banner and empty-library message support English and Arabic.

## Boundaries

A page or lyric must have been saved before it can be displayed offline. Downloaded audio is durable; ordinary player/image caches can be evicted according to the user's storage settings. Local playlists remain available; creating a new *remote* playlist still requires a server-generated ID and an online connection.

Queued mutations use at-least-once delivery: a process failure after server success but before the local acknowledgement can cause a replay. Session/cookie changes create a new partition rather than applying old edits to another session. Permanent API rejection retains the operation and stops replay, avoiding silently discarding later edits. Pending edits are not sent by a background worker while the app is terminated.

The JVM tests cover persisted mutation order, disconnected replay retention, unavailable page failure, and rejecting edits when no storage is configured. Device testing of flight mode, reconnect during playback, Android storage permission loss, download retries and playlist editing is still needed.

Manual Android CI builds accept `release_version`; these build a signed artifact without creating a GitHub Release.

Local validation: `:innertube:test` passed (5 tests, including 2 offline mutation tests). Android device behavior is not validated by these JVM tests.


## Offline song appearance

While the device has no validated internet connection, song rows, song grid cards,
Home carousel cards and queue entries dim to 38% opacity if they cannot play locally.
Albums, artists and playlists keep their normal appearance. Song menus remain available.

Availability follows playback rules: an MP3 must be readable, or the complete audio
range must exist in either the download cache or the player cache. Partial cache
entries, download status alone and an MP3 index entry without an accessible file do
not qualify. Cache metadata supplies the content length first, with the database
format as a fallback; unknown lengths do not prove a complete download.

A single shared monitor checks storage on an IO dispatcher every three seconds
while the visible activity is offline. It catches completed downloads, MP3 conversion,
cache eviction and file removal. Returning online immediately removes the dimming;
backgrounding the activity or going online stops storage polling.
