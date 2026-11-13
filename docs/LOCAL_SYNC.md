# Velune Connect library and device connection

See [architecture, migration and validation](VELUNE_CONNECT.md) for implementation
boundaries and current test evidence. Listen Together remains mobile-only and is
not the transport for TV pairing, library synchronization or playback control.

## Pair once

1. Keep phone and TV on the same LAN. On TV open Settings > Phone playback,
   then allow pairing for two minutes.
2. Open Velune Connect from phone settings or the playback device button.
3. Select the TV, compare the code on both screens, and approve on both only
   if it matches. Approval grants persistent playback and library access.
4. Subsequent connections use the installed device keys and saved pins. Device
   names and addresses are discovery hints, not trust. Revoke access on either
   device to require pairing again. Key loss/reinstall requires re-pairing.

Selecting a device attaches to its session. Transfer playback is an explicit
separate action. Unexpected Wi-Fi loss retries automatically; Disconnect disables
reconnection but keeps trust. Revocation also removes local trust and closes the
connection. Already mirrored library data is retained on both devices.

## Independent playback

The destination resolves and buffers audio independently. Once ready, Connect
pauses the source and commits a new renderer epoch. The TV continues while it is
the renderer even if the phone goes away; it needs its own access to the media
source and Internet connectivity for uncached content. Restart restores a paused
session and queue, subject to Android service restrictions; it does not promise
unattended boot playback. An uncertain transfer outcome favors pausing rather
than duplicate audio. If the source queue/track changes during preparation, the
transfer is rejected and may require explicit resume.

## Library and account boundaries

Connect mirrors local likes/unlikes, bookmarks, available ordered playlist
contents and recent listening history. It does not sign the TV into Google,
copy cookies or passwords, fetch all missing remote playlist pages, or transfer
downloaded audio/local files. Content restricted to an account needs separate
legitimate authorization on the playback device.

Logical clocks and device IDs resolve concurrent edits deterministically. Whole
playlist updates use last-writer ordering by logical counter and device ID;
individual concurrent song-list edits are not merged. Likes, bookmarks and
Connect history deletions retain tombstones. History uses a stable natural key;
up to 5,000 active recent records are merged. Previously observed records outside
the recent capture window are checked in the database before being considered
deleted. Clearing history does not recompute cumulative listening statistics,
matching the existing local clear-history behavior.

Both ends periodically observe local changes. Per-connection acknowledged deltas
send changed records; reconnect sends a full idempotent snapshot. Large playlist
edits resend that playlist. Tombstones are retained, not silently garbage collected
when a device is offline. Limits are inherited/extended from the merge engine:
10 MiB snapshot body, 20,000 likes, 1,000 playlists, 10,000 songs per playlist,
and 50,000 total track references including history tombstones. Exceeding limits
fails synchronization visibly and retains local data.

The journal is an atomic no-backup file, while imported library data is stored in
Room. They are separate persistence domains; a process kill between DB commit
and journal save can cause the next observation to re-version imported changes.
This is idempotent for history insertion and does not grant access or delete the
library, but exactly-once cross-file commits are not claimed.

Legacy Together library exchange remains on mobile. It omits history tombstones
and preserves its older append-only history behavior. Both Connect peers must
use protocol 3; no NSD token or old Together QR is migrated into trusted access.

## Together exclusion

Starting or joining Together detaches that phone from Connect before touching its
player and disables automatic Connect reconnection. TV playback remains owned by
the TV when it is the renderer. Connect stays blocked until Together is explicitly
left or its failed session is cleaned up. Leaving Together does not automatically
reattach or replace TV playback; select Connect again when desired.
