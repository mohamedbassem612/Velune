# Connect media version 1

While a compatible phone is connected, the audio route is upstream → complete
phone cache → TV. The TV probes connected providers for existing downloads or
complete caches first. On a miss, the phone fills and commits the entire song to
its player cache before returning an audio handle. This applies to manually
selected tracks, shuffle and autoplay, even when the TV has its own cached copy.
Initial playback of an uncached song waits for the phone download to complete.
The fill uses bounded 512 KiB range requests, matching normal phone playback,
and commits/releases each range before opening the next one. It does not keep
one unbounded upstream request open for the entire file.

Cache fills belong to the phone service, are deduplicated by song ID, and finish
even if an individual TV reader is canceled. Preparing a new song does not hold
the RPC reader lock. Cache completion and metadata persistence are checked before
serving audio, so the phone's Cached screen can list the complete song.

With no compatible provider, the TV uses its own cache/download/upstream route.
If connected providers fail, the open fails rather than silently bypassing the
phone cache. Once disconnected, a subsequent open uses the independent TV route.
Provider bytes are not copied into the TV's song-ID cache, because downloaded MP3
and streamed encodings may differ. Downloaded phone songs remain in Downloads;
new streamed songs are stored in the phone player cache.

## Desktop integration

Windows/Linux providers can implement these messages without using Android or
Media3. Use the existing Connect protocol 4 mutually authenticated TLS channel,
identity/sequence framing and `connect` handshake. Advertise `mediaVersion: 1`
on `connect`. Older peers without this field retain independent TV playback.
No extra listening port, file paths, or public download URLs are exposed.

All requests have `type: "mediaRequest"`, a unique `requestId`, and `operation`.
Replies have `type: "mediaReply"`, matching `requestId`, and boolean `ok`.

* `open`: `song` (catalog ID, at most 256 characters), `position` (nonnegative
  byte offset), `length` (-1 for remaining content, otherwise positive), and
  `cachedOnly`. A cached-only miss must fail immediately without fetching audio.
  Success returns opaque `handle` and remaining `length` (-1 if unknown).
* `read`: `handle`. Returns `data` (standard Base64, at most 65536 decoded bytes)
  and `eof`. Each handle is a sequential byte stream. EOF is a separate response
  with `eof: true` and empty data.
* `close`: `handle`. Releases the reader; repeated close is harmless.

A canceled or timed-out request sends `type: "mediaCancel"` with its original
`requestId`. The provider cancels that RPC task and releases any reader already
created for it. The shared phone cache fill remains independent and can finish;
a delayed open must not leave an unreachable reader occupying a connection slot.

Providers allow at most four open readers per connection. Audio opens allow up
to 120 seconds for cache preparation; other requests time out after 30 seconds. Disconnect fails pending requests and closes provider readers.
Seeking closes the old handle and opens a new one at the requested byte offset.
A read failure is surfaced to the player; bytes from a different encoding are
not appended inside the same open stream. Seamless recovery from a provider
loss midway through a song is not guaranteed and may require restarting it.

The current TV consumes media; mobile supplies media. Future desktop builds
should implement the provider role with the same catalog IDs and cached-only
semantics. Device selection, playback ownership and library sync stay on the
existing Connect messages.

Regression coverage includes shared cache fills, completing a fill after a TV
reader is canceled, retry after a failed fill without stopping playback, and
refusing to open audio before cache preparation completes. Real-device playback
and cache behavior are reserved for user testing.

## Stored song assets (assetVersion 1)

Providers additionally advertise `assetVersion: 1` in `connect`. The optional
`assets` media RPC takes `song` plus `fields` (a subset of `metadata`, `lyrics`,
`cover`). Its `assets` reply contains only locally available fields: metadata uses
the handover song schema, lyrics is plain text/LRC, and cover is Base64 image data
(maximum 2 MiB before encoding). Missing fields are omitted. Exporting assets must
never trigger internet downloads. This is separate from audio encoding/format
metadata: a provider's content length must not overwrite a receiver's audio cache.

Receivers request missing fields only, check all compatible providers, preserve
existing local lyrics and song preferences, and persist received assets. A short,
bounded per-connection result cache prevents simultaneous artwork/lyrics requests
from repeatedly transferring the same data. Old media-only providers still work.

## App-wide song cache identity

Audio already uses the catalog song ID. Playlist queue construction now also
reuses stored song metadata by that ID, retaining the individual playlist entry
ID. Fully cached playback does not resolve missing metadata/related songs online.
Lyrics consumers check the shared Room store before connected devices and online
providers, with per-song request locks and an explicit refresh bypass.

Common song artwork components pass `SongCover(id, url)` to the singleton image
loader. It prefers MP3 artwork and saved covers, reuses older Coil entries, and
uses canonical keys for supported YouTube/Google thumbnail size variants. URL
aliases survive navigation and app restarts. Saved covers have their own bounded
256 MiB lifetime instead of being deleted when audio cache entries are evicted.
My Top 50 now displays artwork instead of passing a rank as the thumbnail slot.

Playlist membership still comes from the playlist endpoint; already stored song
assets are reused independently of which playlist contains the song. These new
changes require the build/tests launched in the user's Terminal and device QA.
