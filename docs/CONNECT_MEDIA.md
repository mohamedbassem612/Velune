# Connect media version 1

TV playback checks its own MP3 downloads and complete Media3 caches first. If
nothing is available locally, it probes **all connected media providers** for a
complete local file/cache before asking a provider to fetch audio. A provider
uses the mobile playback resolver: MP3 download, download cache, player cache,
then upstream streaming into the player cache as the TV consumes bytes. Playback
starts without waiting for the whole song to download. With no compatible
provider (or if all opens fail), the TV uses its normal independent stream path.
Partial caches are reused by the provider's cache chain; they are not advertised
as complete offline songs. The TV does not copy provider bytes into its own
song-ID cache, because MP3 and streamed encodings may differ.

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

Providers allow at most four open readers per connection. Requests time out after
30 seconds. Disconnect fails pending requests and closes provider readers.
Seeking closes the old handle and opens a new one at the requested byte offset.
A read failure is surfaced to the player; bytes from a different encoding are
not appended inside the same open stream. Seamless recovery from a provider
loss midway through a song is not guaranteed and may require restarting it.

The current TV consumes media; mobile supplies media. Future desktop builds
should implement the provider role with the same catalog IDs and cached-only
semantics. Device selection, playback ownership and library sync stay on the
existing Connect messages.

Validation: 33 Connect/cache JVM tests passed, and mobile/TV debug APKs built
successfully. Real-device LAN playback, MP3 seeking, cache eviction during
playback and disconnect recovery are reserved for user testing.

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
