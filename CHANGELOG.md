# Changelog

## 1.3.14 — 2026-11-17

### Improved Quick Picks

- Recommendations now use listening duration, recent and long-term listening habits, likes, and early skips to build a personal taste profile.
- Discover songs related to several tracks you enjoy, with a boost for YouTube Music chart songs that fit your taste.
- Actual listening gains priority over old or imported likes as listening history grows.
- Repeated early skips lower a song's recommendation priority; their effect fades over time and successful listening helps the song recover.
- Discovery excludes liked and recently played songs, limits repeated artists, and removes common duplicate video/audio uploads.
- Fresh recommendations replace cached picks in the same session. Network timeouts preserve results from sources that respond, with local picks available as a fallback.

## 1.3.13 — 2026-11-14

### Since 1.3.10

- The updater selects the correct APK for the phone or TV.
- Connect phone and TV for playback handover, library sync, and sharing cached songs, lyrics, and cover art.
- TV users can browse and open playlists and albums, and switch between library sections.
- TV playback can start while the phone continues caching the rest of a song.
- Liked songs stay in the phone's Liked list after TV sync, and songs can be copied as shareable links.

## 1.3.12 — 2026-11-14

### Added

- Added authenticated LAN media sharing between the phone and TV.
- Added transfer of locally stored song metadata, lyrics, and cover artwork.
- Added a reusable TV Release workflow with signed APK artifacts.

### Changed

- Reused song metadata and audio cache entries by song ID across playlists and screens.
- Made artwork storage independent from audio cache eviction and shared it across the app.
- Added canonical artwork identities for thumbnail URL size variants.
- Reused saved lyrics before querying a provider, with per-song request locking.
- Fixed playlist artwork rendering so ranked entries do not use their rank as the image input.

### Validation

- Mobile and TV Kotlin compilation passed.
- TV Release APK built successfully with the 1.3.12 signing key.
