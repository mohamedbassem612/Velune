# Mobile music UI — ui-testing

This branch implements the supplied Xiaomi-inspired music brief in the existing Android / Kotlin / Jetpack Compose app. It does not import Xiaomi resources, MIUIX, telephony code or permissions. TV retains its existing theme and RTL policy.

## Default appearance

- Dark mode is on and Pure black is enabled by default: the app and full player background are `#000000`.
- Slightly lighter neutral surfaces separate the mini player, menus and queue. The default palette is black, white and neutral grays. Selected actions use `#F5F5F5` in dark mode and `#191919` in light mode, with separate foreground/container colors.
- Dynamic artwork colors default to off. Existing explicit theme preferences remain available; custom and dynamic accent colors preserve neutral music surfaces.
- The system font defaults to on. Arabic uses the platform's shaping and fallback fonts; no MiSans font asset is assumed.
- The light palette is authored separately. Disabling Pure black gives the dark theme a near-black `#080808` background.

## Components and navigation

| Area | Implementation |
| --- | --- |
| Design values | `ui/theme/MusicDesign.kt`, semantic Material color roles and `MusicShapes` |
| Home | Existing live/offline feed; neutral background, artwork carousel with smaller cover corners and no outline |
| Library | Songs / albums / artists / playlists, plus History and Stats inside the library |
| Music rows | Existing `ListItem` / `ItemThumbnail`; 16sp titles, growing minimum-height rows, subdued descriptions and a stable active background |
| Navigation | Home / My library / Search via `MusicBottomNavigation`; existing width-adaptive rail remains on wide phone layouts |
| Search | A dedicated `MusicSearchScreen` route; soft search field, online/library sources, saved query, history, loading, empty and error states |
| Mini/full player | `ui/player/MusicPlayer.kt`; a single shared cover and title interpolate between measured destinations as the sheet is dragged |
| Playback | Existing `PlayerConnection` and `MusicService`; UI navigation does not create a new player or reload the queue |
| Artwork | `MusicArtwork` / `RetryAsyncImage`, including existing `SongCover` support for cached and embedded artwork |
| Queue | Existing queue editing and playback actions, with a dim layer and rounded top corners |
| Options | Shared full-width action rows for song, album, artist, playlist, selection and lyrics menus; compact artwork/title headers and volume control; a single neutral sheet surface with 30dp top corners and dim layer |

The mini player's height, main title bar and navigation reserve grow with system font size. Bottom content padding includes the mini player, its spacing, navigation and system inset. Player progress and transport controls use LTR geometry while the rest of the app follows the locale's direction. The main manifest enables RTL; the TV overlay explicitly preserves its earlier setting.

Option lists scroll in portrait and landscape. Menus open without a partial-height anchor; their height is capped at 90% of the available display, and action labels wrap with the system font size. Nested action cards and marquee labels are replaced by full-width rows.

Progress clocks are scoped to the slider and mini progress line, rather than the shared metadata or library. Scrubbing commits a seek through the existing connection. Compose's animation clock honors the system animator duration scale, while dragging follows the gesture directly.

Appearance includes a Music player switch, enabled by default. Turning it off exposes the earlier player and its existing layout controls. Legacy-only background, thumbnail and slider settings are hidden while the new player is selected.

## Debug builds on GitHub

`.github/workflows/ui-debug.yml` builds `:app:assembleMobileUniversalDebug` on every push to `ui-testing`, including the initial implementation commit. It uploads an installable Universal Debug APK for the phone as `Velune-ui-testing-debug-<run number>` and puts a download link in the run summary. Builds use Android's debug signing configuration and the `.debug` application ID; no release signing secrets are required. The workflow caches the generated debug keystore for subsequent builds. Artifacts are retained for 14 days. It does not launch an emulator or run device tests.

## Validation

Build and unit-test results for this change are recorded in the commit's delivery notes. `MusicDesignInstrumentedTest` compiles as an optional device smoke test. It uses three cached silent WAV fixtures with mixed Arabic/English titles, missing artwork and a missing artist to check navigation, queue preservation, theme changes and rotation without YouTube playback.

**Runtime/visual validation is outstanding.** The temporary emulator was stopped at the user's request before app screenshot or interaction checks completed. Its files were preserved. No app screenshot, frame-rate result, accessibility pass or device test pass is claimed.

For a later authorized device test, use a disposable debug installation:

```sh
./gradlew :app:assembleMobileUniversalDebug :app:assembleMobileUniversalDebugAndroidTest -PconnectTestBuildType=debug
adb install -r app/build/outputs/apk/mobileUniversal/debug/app-mobile-universal-debug.apk
adb install -r app/build/outputs/apk/androidTest/mobileUniversal/debug/app-mobile-universal-debug-androidTest.apk
adb shell am instrument -w -e class com.nikhil.yt.ui.MusicDesignInstrumentedTest -e language en com.nikhil.yt.debug.test/androidx.test.runner.AndroidJUnitRunner
```

Repeat with `-e language ar` and system font sizes 1.0, 1.3 and 2.0. Review portrait/landscape, light/AMOLED, reduced motion, long lists while playing, scrubbing, sheet drag/cancel, the final list row above the mini player, cached covers and offline search. Captures go to the debug app's external files `music-ui` directory. The test intentionally writes test preferences and fixtures, so it should not run against a personal library.
