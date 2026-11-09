# Android TV preview

Mobile and TV live in the same repository and use the same playback service,
network clients, database code, and HomeViewModel. Device-specific entry points
are selected by Gradle source sets, not by screen width.

## Build and install

Requirements: JDK 21 and Android SDK 36. Set ANDROID_HOME or sdk.dir in
local.properties as for the mobile build.

```sh
# Current mobile app
./gradlew :app:assembleMobileUniversalRelease

# TV preview (all supported ABIs, including emulator x86_64)
./gradlew :app:assembleTvUniversalRelease
./gradlew :app:installTvUniversalRelease
```

Device is the first flavor dimension, followed by ABI. Other ABI variants remain
available, for example `assembleTvArm64Release` and `assembleMobileArm64Release`.
Scripts that previously called `assembleUniversalRelease` must now call
`assembleMobileUniversalRelease` explicitly. Android CI uses the new task and
uploads a signed mobile and TV Release APKs. Branch builds remain artifacts; only tagged
builds publish release assets. The update and sync checks use Release unit-test tasks.

| Build | Application ID |
| --- | --- |
| Mobile release | `com.nikhil.yt` |
| TV release | `com.nikhil.yt.tv` |

These IDs allow side-by-side installation. Each installed app has its own private
data, account, settings, database, and download cache. Existing public MP3 exports
still use `Music/Velune` and may be visible to both apps with storage permission.
Sharing source code does not sync user data.

## First preview

- Dedicated landscape interface with a fixed left navigation rail (Home, Search,
  Library, Settings), large artwork fading into the rail, horizontal personalized
  shelves, a real seed-based radio shelf, discovery and recent-listening shelves,
  and a compact player. Focus outlines remain visible with the remote.
- D-pad navigation, center/select activation, search via the system keyboard.
- Home recommendations from the shared HomeViewModel, song search using the
  shared YouTube client, and liked songs, bookmarked playlists and recent listening
  history from the shared database implementation.
- Play, pause, previous, next, and like controls using the existing MusicService.
- Back returns from Search or Likes to Home; Back from Home follows Android's
  normal exit behavior. Remote media keys use the existing media session.
- TV launcher banner and explicit touchscreen/Leanback feature declarations.
- QR pairing for local shared playback and library sync. See [Local sync](LOCAL_SYNC.md).

This is an initial preview. TV-specific account sign-in, additional settings, album and artist library
browsing, search pagination, queue and lyrics screens, seek controls, and deep-link
navigation still need TV interfaces. The mobile share/deep-link filters and phone
shortcuts are excluded from the TV launcher manifest. Phone widgets and other
shared manifest components have not yet been audited for TV distribution.

## Before distributing

Build both device flavors and exercise the TV APK on an Android TV emulator and
real hardware. Check cold start, focus after loading/search, D-pad access to every
control, keyboard dismissal, playback errors, background playback, remote media
keys, Back behavior, and side-by-side installation with mobile. A successful build
alone does not establish remote usability or store readiness.

Platform references: [TV app setup](https://developer.android.com/training/tv/get-started/create)
and [Compose focus requests](https://developer.android.com/develop/ui/compose/touch-input/focus/request-focus).

## Base revision

TV work is based on main commit `05e6c98`, including the in-app update dialog and
resumable-download changes. The original mobile UI remains selected by its
source-set entry point.

## Latest validation

- The earlier TV and mobile universal debug previews were built on main base `05e6c98`.
- 17 focused unit tests passed: library merge/link behavior, Together core and
  playback planning, and the incoming resumable-update regression.
- Both APK ZIP contents and signatures were checked.
- The earlier TV preview was installed and launched successfully on SHARP Android
  TV at 192.168.1.3. LAN pairing and authenticated library exchange were exercised.
- On 2026-11-10 the latest left-rail/artwork/shelf redesign was installed and
  visually checked on the SHARP TV. D-pad navigation from the rail into the
  personalized shelf and down into the radio shelf scrolled the content into view
  and updated the background artwork. No startup crash was observed.
- Phone-to-TV synchronization still needs a real-phone check with the updated
  mobile preview. Each preview has its own private application data.

## Release delivery policy

From 2026-11-10, deliver and install Release APKs only. Release has
`isDebuggable = false`, code shrinking and resource shrinking enabled. Without
`app/keystore/release.keystore`, local Release APKs are unsigned validation outputs;
they never fall back to the Android debug certificate. GitHub Actions signs both
mobile and TV Release APKs using the existing publishing secrets. The installed
mobile production app uses a different certificate from this laptop’s debug key,
so a locally debug-signed APK cannot update it. Do not remove the production app
to work around that mismatch.

Release tasks: `assembleMobileUniversalRelease`, `assembleTvUniversalRelease`,
`testMobileUniversalReleaseUnitTest`, and `testTvUniversalReleaseUnitTest`.

## Verified signed Release deployment — 2026-11-10

GitHub Actions run 42721718154 built signed mobile and TV 1.3.8 Release APKs
from commit b267533. Both certificates match the installed production mobile
certificate. Artifact ZIP SHA-256:
`2686e15dd74914d206f4b63a33b42fa2669bc134afd589d6e472483c7ce86c54`.

Both APKs have `debuggable=false`, version code 10308, and passed signature
verification. Mobile (`com.nikhil.yt`) was updated in place with the same UID;
TV (`com.nikhil.yt.tv`) was installed separately. The Debug packages
`com.nikhil.yt.debug` and `com.nikhil.yt.tv.debug` were uninstalled and their
absence verified. Both Release processes launched without a startup fatal error;
the TV Release interface and local-pairing QR were observed on-screen.

The 17 focused app unit tests also passed against Release variants, alongside
the CI search-filter tests. The publishing key was used only by GitHub Actions
and was not copied to this laptop. No merge into main was performed.
