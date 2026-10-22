# Velune Project - Code Improvement Report

Date: October 19-21, 2026. Sessions: (1) Comprehensive null-safety and selection UI improvements, (2) Mood chips fix, (3) App startup performance pass, (4) Song stream start speed fix, (5) Hardcoded tokens and token logging fix.

## 1. Overview

This session focused on two main areas:
1. **Null-safety (!!operators)**: Removed 53 non-null assertions that could crash the app, preventing crashes when data from the network or database is missing.
2. **Selection UI**: Added a floating selection bar in the queue, improved the selection experience in playlists and Liked songs, and fixed a critical selection bug that reset selections on data refresh.
3. **Startup performance (section 14)**: Four patches (perf-group1 to perf-group4) that made the app open fast. Measured on the release build: first frame at about 357ms and queue ready at about 831ms from process start.
4. **Song stream start speed (section 15)**: Two patches (stream-start and stream-start-2) that cut the time from tapping a song to having a playable URL from 2.2-5.6 seconds down to about 0.45 seconds.
5. **Security: hardcoded tokens and token logging (section 16)**: Removed two server tokens that were written directly in `MusicService.kt` and removed the Discord token from the logs (patch tokens-fix).

## 2. Null Safety Improvements

### Problem
The codebase had 139 uses of the `!!` operator, which crashes the app if the value is null. High-risk ones were in queue calculations, playlist loading, and data parsing from external sources.

### Solution
Replaced 53 instances with safe alternatives:
- `metadata!!` → `metadata?.duration ?: 0` (fallback to safe default)
- `songs!!` → `songs.orEmpty()` (empty list if null)
- `null!!` → `let { x -> ... }` blocks (handle null case explicitly)
- `!!` in data parsing → `mapNotNull { ... }` (skip invalid items)

### Modified Files
- `app/src/main/kotlin/com/nikhil/yt/ui/player/Queue.kt`: Queue display and selection, 12 cases
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/playlist/AutoPlaylistScreen.kt`: Songs list, 12 cases
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/playlist/TopPlaylistScreen.kt`: Songs list, 12 cases
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/HomeScreenComponents.kt`: Album navigation, 1 case
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/StatsScreen.kt`: Album count display, 1 case
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/playlist/LocalPlaylistScreen.kt`: Playlist name and reorder, 4 cases
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/HistoryScreen.kt`: History selection, 1 case
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/playlist/OnlinePlaylistScreen.kt`: Online songs selection, 1 case
- `app/src/main/kotlin/com/nikhil/yt/MainActivity.kt`: Search query parsing, 3 cases
- `app/src/main/kotlin/com/nikhil/yt/playback/MusicService.kt`: Preload item playback, 1 case
- `app/src/main/kotlin/com/nikhil/yt/together/TogetherOnlineApi.kt`: Request retry, 1 case
- `kizzy/src/main/kotlin/com/my/kizzy/gateway/DiscordWebSocket.kt`: Discord message parsing, 2 cases

### Remaining !! (86)
Left untouched because they are:
- Inside `runCatching` blocks (failures are handled gracefully).
- Navigation parameters (must be present for the screen to open).
- Database operations or system calls (assumed valid by contract).
- Inferred from prior checks (safe by logic, harder to refactor without changing flow).

## 3. Queue Selection Bar (New Feature)

### Problem
The queue had a selection mode but no visible UI to act on selected songs. Selecting via long-press worked, but there were no action buttons.

### Solution
Added `QueueSelectionBar.kt`, a new composable that shows when songs are selected:
- **Close button**: Cancel selection.
- **Count display**: Shows "X items selected" and allows select-all toggle.
- **Action buttons** (in a row below):
  - **Play next** (playlist_play icon): Insert selected songs after current song.
  - **Add to playlist**: Open playlist dialog; detects duplicates and confirms before adding.
  - **Like** (favorite/favorite_border icon): Toggle liked status. Switches to dislike if all are liked.
  - **Remove from queue** (delete icon): Delete selected songs from the queue, from end to start (safe order).

### Key Details
- Selection state (`selectedSongs`, `selectedItems`) is maintained in Queue.kt.
- Removal iterates in reverse order to avoid index shifting bugs.
- Like toggle respects per-song state: if all selected are liked, button shows "Dislike all".
- Bar is pinned at the bottom, visible above the snackbar, disappearing when selection is cleared.

### New Files
- `app/src/main/kotlin/com/nikhil/yt/ui/menu/QueueSelectionBar.kt`: 180 lines. Reusable composable for queue actions.

### Modified Files
- `app/src/main/kotlin/com/nikhil/yt/ui/player/Queue.kt`: Import and integration of the new bar.

## 4. Selection Menu Improvements (Liked, Playlists, etc.)

### Problems
1. **Add to library button was redundant**: Like already adds to library.
2. **Play next was missing**: No way to queue songs without playing them immediately.
3. **Awkward big-box UI**: The "Play" button in the grid and "Play" in the list were confusing.

### Solutions

#### 4a. Removed "Add to Library"
Deleted from `SelectionSongsMenu.kt` (37 lines). It was confusing because:
- Affects Liked, playlists, albums, and history screens.
- Redundant with Like (Like automatically adds to library).
- "Remove from library" doesn't exist in the same menu.

**Modified Files**: `app/src/main/kotlin/com/nikhil/yt/ui/menu/SelectionSongsMenu.kt`

#### 4b. Replaced "Play" with "Play Next" in the Grid
Changed the big action grid in the selection menu:
- **Old**: Play (plays selected songs immediately), Shuffle, Add to playlist.
- **New**: Play next (queues after current), Shuffle, Add to playlist.

The list still has a "Play" item for full playback control.

**Modified Files**: `app/src/main/kotlin/com/nikhil/yt/ui/menu/SelectionSongsMenu.kt`

## 5. Selection Bug Fix (Critical)

### Problem
When you selected a song in Liked, playlists, etc. for the first time, the selection would immediately clear and the list would jump. This happened repeatedly.

### Root Cause
The selection was rebuilt from scratch every time the list refreshed from the database. Selection was matched by **position in the list**, not by song identity. So when the database updated (sync, playback logging, sort), the list order changed, and the old position no longer matched.

### Solution
Created `rememberWrappedItems()` helper function in `ItemWrapper.kt`:
- Remembers which items are selected by **song ID**, not position.
- When the list updates, it finds each song by ID and restores its selection state.
- If a song is not in the new list, it remains selected but invisible (will reappear if it comes back).

### Example (before and after)
```kotlin
// Before: Selection lost on every refresh
val wrappedSongs = remember(songs) {
    songs?.map { ItemWrapper(it) }?.toMutableStateList() ?: mutableStateListOf()
}

// After: Selection remembered by ID
val wrappedSongs = rememberWrappedItems(songs) { it.id }
```

### Modified Files
- `app/src/main/kotlin/com/nikhil/yt/ui/utils/ItemWrapper.kt`: Added `rememberWrappedItems()` helper (18 lines).
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/playlist/AutoPlaylistScreen.kt`: Uses new helper.
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/playlist/TopPlaylistScreen.kt`: Uses new helper.
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/playlist/LocalPlaylistScreen.kt`: Uses new helper.
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/library/LibrarySongsScreen.kt`: Uses new helper.
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/AlbumScreen.kt`: Uses new helper.
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/playlist/CachePlaylistScreen.kt`: Uses new helper.

## 6. Default Selection State Bug

### Problem
When you opened a selection menu for the first time after launching the app, every song was pre-selected (even though the UI showed them unselected).

### Root Cause
`ItemWrapper` initialized `_isSelected` to `true`, but the UI defaulted to showing no selections.

### Solution
Changed default from `mutableStateOf(true)` to `mutableStateOf(false)` in `ItemWrapper.kt`.

**Modified Files**: `app/src/main/kotlin/com/nikhil/yt/ui/utils/ItemWrapper.kt`

## 7. Implementation Scripts

All changes can be applied via safe Python scripts that:
- Work on any version of the repo (not tied to line numbers).
- Are idempotent (safe to run multiple times).
- Create `.bak` backups before modifying files.
- Check for existing changes and skip if already applied.

### Scripts (in `/mnt/user-data/outputs/`)
1. **null-safety-all.patch**: Old patch format (line-number dependent, now deprecated).
2. **apply_null_safety.py**: Apply all 53 null-safety fixes. Handles 11 files.
3. **fix_statsscreen.py**: Fix StatsScreen compilation error (Int? vs Any).
4. **add_queue_selection_bar.py**: Add the queue selection bar.
5. **fix_liked_selection.py**: Remove Add to library, fix default selection=false.
6. **add_play_next_selection.py**: Add Play next to selection menus (deprecated, see below).
7. **move_play_next_to_grid.py**: Move Play next to grid, remove from list.
8. **fix_selection_reset.py**: Fix selection bug with `rememberWrappedItems()`.

### Application Order (Recommended)
```bash
python3 apply_null_safety.py
python3 fix_statsscreen.py  # (if needed)
python3 add_queue_selection_bar.py
python3 fix_liked_selection.py
python3 move_play_next_to_grid.py
python3 fix_selection_reset.py
```

Or one-liner:
```bash
for s in apply_null_safety fix_statsscreen add_queue_selection_bar fix_liked_selection move_play_next_to_grid fix_selection_reset; do
  python3 $s.py
done
```

All scripts are safe to re-run; they detect what's already applied and skip it.

## 8. Testing Checklist

After building, test the following:

### Queue
- [ ] Select one song (long press): bar appears, count = 1.
- [ ] Select multiple songs: count increases.
- [ ] Play next: selected songs appear after current song, selection clears.
- [ ] Add to playlist: dialog opens, allows selecting playlists, detects duplicates.
- [ ] Like: button toggles heart icon, selection clears on action.
- [ ] Remove from queue: songs disappear, list order is preserved.
- [ ] Select all / deselect all: toggles all songs in queue.

### Liked & Playlists
- [ ] Long press a song: selection mode on, bar (or bottom menu) appears.
- [ ] Play next (in grid): queues songs without playing immediately.
- [ ] Play (in list): plays songs immediately (different from grid).
- [ ] Add to playlist: works as in queue.
- [ ] Like: works as in queue.
- [ ] Selection survives when list refreshes: select a few songs, wait 5-10 seconds (may trigger sync or state update), selection should persist.

### General
- [ ] No crashes on null data (test by selecting songs from online playlists, history, etc.).
- [ ] App opens and Liked / Playlists load without freezing or crashes.

## 9. Known Limitations & Future Work

### Not Changed
- **DiscordLoginScreen Log**: Fixed in section 16 (the token is no longer logged).
- **46 empty catch blocks**: Silently swallow errors; should add logging.
- **113 other !! operators**: Safe by context, not refactored to keep diff minimal.
- **runBlocking**: Still present in MusicService, DownloadUtil, DataStore and the Kizzy Discord gateway. Details and a plan are in `runblocking-report.md`.
- **NewPipeExtractor master-SNAPSHOT**: Builds not reproducible; should pin version.
- **Encryption claims in README**: Not actually implemented; should update docs or implement.
- **TestTTML.kt**: Test file in main source set; should move to test folder.

### Related to Download Feature (In Planned Report.md, Section 6)
The download feature (to Music/Velune with format preservation, scan on startup, duplicate detection) is still planned. It depends on all the code cleanup above being stable.

## 10. Statistics

| Category | Count |
|----------|-------|
| Null-safety fixes applied | 53 |
| Remaining !! (safe by context) | 86 |
| New files created | 1 (QueueSelectionBar.kt) |
| Files modified | 19 |
| Lines added (helpers, UI) | ~250 |
| Lines removed (Add to Library) | ~37 |
| Python scripts provided | 8 |
| Selection bug root causes fixed | 2 (position-based state, default state) |

## 11. Git Status

All changes are ready to commit. The repo includes:
- Original queue null-safety fixes (from last session).
- 53 new null-safety fixes across 11 files.
- New QueueSelectionBar composable.
- Selection UI refinements (removed Add to Library, Play next placement).
- Critical selection state preservation fix.

No breaking changes to public APIs or data structures.

## 13. Mood / Category Chips Suggestions Bug Fix (October 20, 2026)

### Problem
Tapping the mood / genre chips on the Home screen (e.g. *Relax*, *Sleep*, *Sad*, *Romance*, *Energy*) failed to bring up suggestions.

### Root Cause
1. **Silent InnerTube Crash (`YouTube.kt`)**: When querying filtered chips with `params`, YouTube often returns data directly in `response.contents.sectionListRenderer` rather than nesting under `singleColumnBrowseResultsRenderer.tabs[0]`. Because `singleColumnBrowseResultsRenderer` was null, `sectionListRender?.contents!!` threw a `NullPointerException`. Wrapped in `runCatching`, this silently failed and returned `null`, aborting `toggleChip()` without updating the state.
2. **Missing UI Filtering (`HomeScreen.kt`)**: Even when suggestions were available, personal sections (*Quick Picks*, *Keep Listening*, *Account Playlists*, *Forgotten Favorites*, *Similar Recommendations*, *For You*, *Mood & Genres*) were not conditioned on `selectedChip == null`. They remained at the top of the feed, pushing the chip's suggestions below all personal content.
3. **Chip Row Loss Risk (`HomeViewModel.kt`)**: When switching to filtered responses where YouTube omitted the chip header, the chips row could disappear or state transitions could become inconsistent on network failure.

### Solution & Changes
- **`innertube/src/main/kotlin/com/nikhil/yt/innertube/YouTube.kt`**:
  - Added fallback resolution for `sectionListRenderer` from `response.contents?.sectionListRenderer` and `continuationContents`.
  - Replaced `sectionListRender?.contents!!` with `sectionListRender?.contents.orEmpty()`.
  - Safe-called `sectionListRender?.header?.chipCloudRenderer`.
  - Also made `moodAndGenres()` null-safe by removing `contents!!`.
- **`app/src/main/kotlin/com/nikhil/yt/viewmodels/HomeViewModel.kt`**:
  - Added fallback for `chips` in `toggleChip()`: `nextSections.chips ?: homePage.value?.chips ?: originalHomePage?.chips`.
  - Protected `previousHomePage` state from corruption if network query returns null.
- **`app/src/main/kotlin/com/nikhil/yt/ui/screens/HomeScreen.kt`**:
  - Guarded personal home sections (*Quick Picks*, *Keep Listening*, *Account Playlists*, *Forgotten Favorites*, *Similar Recommendations*, *For You*, *Mood & Genres*) with `if (selectedChip == null)`.
  - When a mood chip is active, its suggestions appear directly under the chips row.
  - Tapping the chip again or pressing Back cleanly restores the standard home feed.

## 14. App Startup Performance (October 20-21, 2026)

### Goal
Make the app open faster, use less RAM and avoid heavy work on the main thread, without changing behavior.

### How it was tested
All measurements were done on the **release** build (arm64) on the phone. The debug build is much slower and gives misleading numbers, so it must not be used to judge speed. A small helper (`StartupTrace`) logs how many milliseconds passed since the process started, for each startup step (Logcat tag `StartupTrace`).

### Patch perf-group1: Stream probe, logging and database queries
- **`YTPlayerUtils.kt`**: Added `validatedStreamClientsUntil`. After a client's stream URL passes the validation probe once, the probe is skipped for 30 minutes for that client. Any 403/429 or bot detection removes the client from the validated list again.
- **`GlobalLog.kt`**: Replaced "copy the whole 500-entry list for every log line" with a ring buffer. Adding a line is now cheap, and the list shown in the UI is rebuilt at most every 250ms.
- **`DatabaseDao.kt`**: Added `recentEvents(limit)` (SQL `LIMIT`).
- **`ForYouSuggestionEngine.kt`, `HomeViewModel.kt`**: Replaced `database.events().first().take(20)` and `events().first().firstOrNull()` (which loaded the whole play history table) with `recentEvents(20)` and `recentEvents(1)`.
- Result: song playback started noticeably faster (confirmed on the phone).

### Patch perf-group2: Home screen work and baseline profile
- **`HomeViewModel.kt`**: The "For You" suggestions now wait 1.5 seconds so the network is free for the main feed and for a song the user taps first. The similar-recommendation requests (per artist and per song) now run at the same time instead of one after another.
- **`Thumbnail.kt` (Canvas artwork cache)**: Loading the cache file from disk moved off the main thread. Entries added while loading are kept, and the file is never overwritten before it was loaded.
- **`baseline-prof.txt` + `profileinstaller` dependency**: Added a hand-written baseline profile so Android pre-compiles the app code and it warms up faster.
- **`libs.versions.toml`, `app/build.gradle.kts`**: Added the `profileinstaller` library.
- Result: small difference by itself. Cold start was still slow, so the startup path was traced next.

### Patch perf-group3: Startup tracing and database warm-up
- **`StartupTrace.kt`** (new): Logs time since process start for each step.
- **`App.kt`**: The Room database is now opened on a background thread (`db-warmup`) right when the app process starts. Before, it was first opened on the main thread while the first screen loaded.
- **`MainActivity.kt`, `MusicService.kt`, `MusicDatabase.kt`**: Added trace marks (first frame, database opened, player built, queue restore).
- **`MusicService.kt` queue restore**: The UI is told "queue items are ready" as soon as the items are loaded, instead of waiting for the automix and player state restore (which included a fixed 1 second delay). The small polling delay was reduced from 100ms to 25ms.

### Patch perf-group4: Finer tracing and local release signing
- **`app/build.gradle.kts`**: If `keystore/release.keystore` does not exist, the release build is signed with the debug key. This allows installing and testing the real release speed locally.
- **`App.kt`, `MainActivity.kt`, `MusicService.kt`**: More trace marks inside the queue restore (file read, converted to queue, main thread reached, playQueue returned).

### Measured result (release build)
| Step | Time since process start |
|------|--------------------------|
| MainActivity first frame | about 357ms |
| Queue ready | about 831ms |

App open speed is now acceptable.

## 15. Song Stream Start Speed (October 21, 2026)

### Problem
Tapping a song took about 5-8 seconds before playback started, even with fast internet.

### Finding (what it was NOT)
The app does **not** download the whole song before playing. ExoPlayer reads the song in 512 KB chunks (`CHUNK_LENGTH`) and starts playing from the first chunk. The delay happened **before** any audio byte was requested.

### Root Cause (from timing logs)
Inside `createDataSourceFactory()` in `MusicService.kt`, a blocking call (`runBlocking`) waits for `YTPlayerUtils.playerResponseForPlayback()` before ExoPlayer gets the URL. That function tried the stream clients one after another (the list has about 17 clients). Each failed client cost one network request of 150-300ms. In the logs about 15 clients failed on every song, and `VISIONOS` was the one that finally worked. This alone added up to 2.2-5.6 seconds. On the very first song there was also about 1.6 seconds of one-time URL decryption work.

### Patch stream-start (measurement, warm-up, prefetch)
- **`YTPlayerUtils.kt`**: Added a `timed(...)` helper and `StreamTiming` logs (Logcat tag `StreamTiming`) for each step: signature timestamp, metadata call, fallback client calls, URL finding, validation, and the total resolver time (`RESOLVER TOTAL`). Uses `android.util.Log.i`, which stays in release builds.
- **`NewPipe.kt`**: Added `NewPipeUtils.warmUp()`. It prepares the YouTube JS player (signature timestamp and throttling parameter code) ahead of time. Failures are ignored.
- **`MusicService.kt`**: `warmUp()` is started in the background when the service starts, so the first song does not pay for the JS player download.
- **`MusicService.kt`**: Added `prefetchNextStream()`, called from `onMediaItemTransition`. It resolves the next song's stream URL in the background (format info and URL cache included), skips songs that are already downloaded, and avoids duplicate work with `prefetchInFlight`. Skipping to the next song or auto-advancing starts almost instantly.

### Patch stream-start-2 (remember the working client)
- **`YTPlayerUtils.kt`**: The client that worked last is saved (`lastWorkingClient`, also stored in SharedPreferences `stream_client_prefs`, so it survives app restarts) and tried **first** on the next song. If it fails, the normal order is used and the new winner is saved.
- Extra logs: `WINNER client=... after N tries`, and `SKIP <client> ...` with the reason (status, no expire time, or no usable audio formats).

### Measured result (release build)
| Case | Resolver time |
|------|---------------|
| Before (first song) | 5631ms |
| Before (second song) | 2264ms |
| After (normal song) | 436-450ms |
| After (first song after cold start, includes warm-up running in parallel) | 1252ms |
| Next song (prefetched) | URL already cached, no wait |

In all tested songs the winner was `VISIONOS` on the first try.

### Notes
- The repeated `url served from cache (no resolve)` log lines are normal. They are the 512 KB chunk requests while the song plays, not repeated downloads.
- The first song after a fresh install still walks through the client list once, because no winner is saved yet.
- The stream URL work is still done inside `runBlocking` in the data source (see limitations in section 9).

### Possible next step (not done)
Run the metadata request (about 250ms) and the winning client's request (about 180ms) at the same time instead of one after another. Expected saving: about 200ms. Not done because the gain is small compared to the risk.

## 16. Hardcoded Tokens and Token Logging (October 21, 2026)

### Problem
1. **Two hardcoded server tokens in `MusicService.kt`**: The Together (listen together) feature authenticates to the app server (`velune-server.onrender.com`) with a bearer token. The token was written directly in the code in two places: when hosting an online session and when joining one (two different values). The repository is public, so these values were visible to anyone, including in the git history.
2. **Discord token written to the logs**: `MusicService.kt` logged `Presence manager started with token=...` with the real Discord user token, and `DiscordLoginScreen.kt` logged `Token: ...` when the token was captured from the login WebView. The in-app log screen (GlobalLog) shows these lines, so the token could leak through a shared log.

### Solution (patch tokens-fix)
- **`MusicService.kt`**: Both places now read the token from `BuildConfig.TOGETHER_BEARER_TOKEN` (trimmed, blank means missing). This is the setup the project already had: `app/build.gradle.kts` reads `TOGETHER_BEARER_TOKEN` from `local.properties` (which is in `.gitignore`) or from an environment variable. If the token is missing, the existing "token missing" error is shown instead of silently using a wrong value.
- **`MusicService.kt`**: The Discord presence log line no longer prints the token.
- **`DiscordLoginScreen.kt`**: The log line no longer prints the token.

### What must be done by the project owner
1. Add the token to `local.properties` (not committed):
   `TOGETHER_BEARER_TOKEN=<the token the server expects>`
2. **Rotate the server token.** The old values (`VeluneAdminToken`, `velune_server_token`) are in the public git history, so they must be treated as leaked. Create a new token on the server and put the new one in `local.properties`.
3. The two old values were different. The code now uses one token for both hosting and joining. If the server really needs two different tokens, tell the developer so a second BuildConfig field can be added.
4. If the Discord token was ever shared inside a log or a screenshot, log out and log in again to get a new one.

### Important limit
Any value placed inside an APK (BuildConfig included) can be extracted by someone who unpacks the app. This change keeps the token out of the public repository, which was the real exposure, but it is not a full protection. A stronger design is for the server to limit abuse (rate limits, per-session keys) instead of trusting one shared token inside the app.

### Modified Files
- `app/src/main/kotlin/com/nikhil/yt/playback/MusicService.kt`
- `app/src/main/kotlin/com/nikhil/yt/ui/screens/settings/DiscordLoginScreen.kt`

## 17. Updated Statistics (Performance and Security Sessions)

| Category | Value |
|----------|-------|
| Performance patches applied | 6 (perf-group1 to 4, stream-start, stream-start-2) |
| Security patch | 1 (tokens-fix): 2 hardcoded tokens removed, 2 token log lines removed |
| App open (first frame, release) | about 357ms |
| Queue ready (release) | about 831ms |
| Stream resolve time per song | 2.2-5.6s down to about 0.45s |
| New files | 1 (StartupTrace.kt) |
| Main files touched | YTPlayerUtils.kt, MusicService.kt, NewPipe.kt, HomeViewModel.kt, App.kt, MainActivity.kt, GlobalLog.kt, DatabaseDao.kt |

## 18. Still To Do

- Rotate the Together server token (the old values are in public git history) and add the new one to `local.properties`.
- `runBlocking` usage: see the separate handoff file `runblocking-report.md`.
- README encryption claim.
- Pin the NewPipeExtractor version (currently `master-SNAPSHOT`).
- Remove the RESTORE logs, `StartupTrace` and `StreamTiming` logs when no longer needed, and delete `.bak` files.
- Download feature (Music/Velune, original format, MediaStore, duplicate check by videoId).

## 19. YouTube Music Style UI (patch ytmusic-ui)

Goal: make the app look like YouTube Music.

- Colors: new fixed dark and light color schemes (`YtMusicColors.kt`). Black background, white text, grey secondary text, red as the accent. `VeluneTheme` has a new `ytMusicStyle` parameter (default true). Set it to false to return to the old dynamic colors.
- Bottom bar: new flat `YtMusicNavigationBar` (no floating pill, icon with small label, white when selected). Bar height is now 64dp (56dp slim).
- Mini player: flat full-width bar with a small square thumbnail, title and artist, play/pause and next buttons, and a thin progress line on top. The gap between the mini player and the bar is now 0.
- Chips (home and library filters): 8dp corners, white chip with black text when selected, no check icon.

### Modified Files
- `MainActivity.kt`, `constants/Dimensions.kt`, `ui/theme/Theme.kt`, `ui/component/ChipsRow.kt`, `ui/player/MiniPlayer.kt`, `ui/player/MiniPlayerComponents.kt`
- New: `ui/component/YtMusicNavigationBar.kt`, `ui/theme/YtMusicColors.kt`


---

**Overall Summary**: Removed crash-prone null assertions across the app and InnerTube, added selection UI for queuing and playlist management, fixed selection state resets on data refresh, and restored the mood/category chip suggestions on the home screen. Also made the app open much faster (first frame about 357ms on release) and cut the song start delay from 2-5 seconds of URL resolving to about 0.45 seconds, by remembering the working stream client, warming up the JS player, and prefetching the next song.
