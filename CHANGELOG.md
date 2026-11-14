# Changelog

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
