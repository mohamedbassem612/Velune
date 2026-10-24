<div align="center">

<img src="fastlane/metadata/android/en-US/images/icon.png" width="96" alt="Velune icon" />

# Velune

An Android client for YouTube Music, built with Kotlin and Jetpack Compose.

[![Build](https://github.com/mohamedbassem612/Velune/actions/workflows/android.yml/badge.svg)](https://github.com/mohamedbassem612/Velune/actions/workflows/android.yml)
[![Latest release](https://img.shields.io/github/v/release/mohamedbassem612/Velune?label=release)](https://github.com/mohamedbassem612/Velune/releases/latest)
[![License: GPL-3.0](https://img.shields.io/github/license/mohamedbassem612/Velune)](LICENSE)

</div>

## Screenshots

<div align="center">

<img src="screenshots/Home.png" width="160" alt="Home" />
<img src="screenshots/Now%20Playing.png" width="160" alt="Now playing" />
<img src="screenshots/Lyrics.png" width="160" alt="Lyrics" />
<img src="screenshots/Library.png" width="160" alt="Library" />

<img src="screenshots/Downloaded.png" width="160" alt="Downloads" />
<img src="screenshots/Stats.png" width="160" alt="Stats" />
<img src="screenshots/History.png" width="160" alt="History" />
<img src="screenshots/Settings.png" width="160" alt="Settings" />

</div>

## Install

Requires Android 8.0 (API 26) or newer.

1. Open the [latest release](https://github.com/mohamedbassem612/Velune/releases/latest) and download the APK.
2. Install it. Android will ask you to allow installs from your browser or file manager the first time.

You can also follow releases with [Obtainium](https://apps.obtainium.imranr.dev/redirect?r=obtainium://add/https://github.com/mohamedbassem612/Velune).

Updates: release builds check GitHub for a newer version when the app is opened (at most every 12 hours). If one is found, the app offers to download it and hands the APK to the Android installer. You still tap Install yourself, and the update must be signed with the same key as the installed app.

## Features

Playback
- Background playback with a media notification
- Crossfade between tracks
- Skip silence
- Audio normalization
- Tempo and pitch control
- Equalizer (built-in, and the system equalizer panel)
- Sleep timer

Library and discovery
- Sign in with a YouTube Music account to use your library
- Personalized home feed, charts, and mood and genre browsing
- Listening history, stats, and a year in music summary
- Listen Together sessions
- Backup and restore of app data

Lyrics
- Multiple lyrics sources: LrcLib, KuGou, SimpMusic, BetterLyrics, and YouTube Music
- Word-by-word highlighting when the source provides word timings
- Lyrics translation

Downloads
- Download songs for offline playback
- Export downloads as MP3 (FFmpeg)

Interface and integrations
- Material 3 with dynamic color
- Discord Rich Presence
- Last.fm scrobbling

## Tech stack

| Area | Libraries |
|------|-----------|
| Language | Kotlin |
| UI | Jetpack Compose, Material 3 |
| Playback | Media3 / ExoPlayer |
| Dependency injection | Hilt |
| Database | Room |
| Networking | Ktor |
| Async | Coroutines and Flow |
| Build | Gradle (Kotlin DSL), JDK 21 |

compileSdk and targetSdk are 36, minSdk is 26.

## Project structure

| Module | Purpose |
|--------|---------|
| `app` | The Android application |
| `innertube` | YouTube Music API client |
| `lrclib`, `kugou`, `simpmusic`, `betterlyrics` | Lyrics providers |
| `lastfm` | Last.fm API client |
| `kizzy` | Discord Rich Presence |
| `canvas` | Canvas artwork support |

## Build from source

Requirements: a recent Android Studio, JDK 21, and the Android SDK for API 36.

```bash
git clone https://github.com/mohamedbassem612/Velune.git
cd Velune
./gradlew :app:assembleUniversalDebug
```

The APK is written to `app/build/outputs/apk/universal/debug/`. You can also open the project in Android Studio, let Gradle sync, and run it.

A local release build (`assembleUniversalRelease`) is signed with the debug key unless `app/keystore/release.keystore` exists, so it will not update an installed official release.

## Releases

Releases are built by GitHub Actions and signed with the project key. Pushing a tag in the form `vMAJOR.MINOR.PATCH` (for example `v1.3.1`) builds the signed APK, sets the app version from the tag, and publishes a GitHub release with the APK attached. The required repository secrets are `ANDROID_KEYSTORE_BASE64`, `ANDROID_KEYSTORE_PASSWORD`, `ANDROID_KEY_ALIAS`, and `ANDROID_KEY_PASSWORD`.

## Credits

Velune is based on the earlier Velune project by [nikhilvishwakarma00](https://github.com/nikhilvishwakarma00), and builds on the work of these projects:

- [ArchiveTune](https://github.com/koiverse/ArchiveTune)
- [Metrolist](https://github.com/mostafaalagamy/Metrolist)
- InnerTune
- Kizzy
- SimpMusic
- BetterLyrics

## Legal

Velune is an independent client. It is not affiliated with, endorsed by, or sponsored by YouTube or Google. Please support artists through official platforms.

Licensed under the [GNU General Public License v3.0](LICENSE).
