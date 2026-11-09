# Local device sync preview

TV can host a Music Together LAN session using **Settings > Link a phone**.
The QR code contains the TV address, session identifier, a random session secret,
and an explicit library-sync flag. Scan it on a phone with the updated mobile
build and open it in Velune. Both devices must be reachable on the same network.
Stopping the session invalidates the link. The code grants access to playback
controls and the library for that session; do not share its screenshot publicly.

## Shared behavior

Playback uses the existing Music Together WebSocket protocol: queue, current
track, play/pause, seeks, previous/next, repeat and shuffle. This is the existing
shared-listening mode, so both joined devices can play audio. A dedicated mode
that uses the phone solely as a silent remote is not implemented yet.

Updated clients opt into library sync through the explicit QR flag. Every 15
seconds, an accepted guest sends a library snapshot to the host and imports the
merged response. The endpoint requires the session secret and an active, approved
participant ID. It is available only on LAN hosts that explicitly enabled library
sync; online rooms and ordinary listening-only rooms keep it disabled.

The snapshot includes:

- Liked songs, including later unlike changes.
- Bookmarked playlists, their names and locally available ordered song lists.
  Removing a playlist bookmark syncs that removal; imported underlying playlist
  rows are retained to avoid destroying local data.
- Up to the most recent 5,000 listening events, with duplicate detection.

Each installed app retains its own account and settings. Cookies, access tokens,
account credentials, audio files, downloads and local-file tracks are excluded.
Remote playlists are represented by their locally available contents; this does
not fetch missing pages from YouTube.

## Conflicts and persistence

Likes and playlists use logical version counters with a random installation ID as
a deterministic tie breaker. Whole-playlist conflicts choose one version. A later
observed edit gets a newer version; concurrent changes to the same playlist do not
merge individual song edits. Removal records persist, preventing an offline peer
from resurrecting older likes or bookmarks. The journal and device ID live in the
Android no-backup directory to avoid cloning one installation's identity.

History is merged by song ID, event timestamp and play duration. Clearing history
is not propagated in this preview, so a peer can restore previously shared events.
Imported events increment local song play time once; remote account statistics,
monthly counters, search history and recommendation caches are not transferred.

The JSON schema is version 1. Limits: 10 MiB per exchange, 20,000 like records,
1,000 playlists, 10,000 songs per playlist, 50,000 track references per snapshot.
Persistent removal records count toward these limits. There is no automatic
background re-pairing after a session ends.

## Validation

`LibraryMergeTest` covers repeat exchange, unlike propagation, deterministic
concurrent conflicts, playlist removals, and later re-likes. Run:

```sh
./gradlew :app:testTvUniversalReleaseUnitTest --tests com.nikhil.yt.localsync.LibraryMergeTest
```

The TV host has been exercised on the connected SHARP Android TV: QR decoding,
WebSocket join, missing/invalid participant authorization, unsupported schema
rejection, and repeated authenticated HTTP exchange. Real phone-to-TV exchange
and shared playback still require installation of the updated mobile preview.
The mobile Release APK keeps the production application ID and must use the
publishing certificate to update it without losing its library. TV uses its own
application ID and receives shared library data through local sync.
