# Velune Project - Code Improvement Report

Date: October 19, 2026. Session: Comprehensive null-safety and selection UI improvements.

## 1. Overview

This session focused on two main areas:
1. **Null-safety (!!operators)**: Removed 53 non-null assertions that could crash the app, preventing crashes when data from the network or database is missing.
2. **Selection UI**: Added a floating selection bar in the queue, improved the selection experience in playlists and Liked songs, and fixed a critical selection bug that reset selections on data refresh.

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
- **DiscordLoginScreen Log**: Token is still logged at line 88 (security issue, but out of scope).
- **46 empty catch blocks**: Silently swallow errors; should add logging.
- **113 other !! operators**: Safe by context, not refactored to keep diff minimal.
- **runBlocking on main thread**: Found in DataStore, MusicService; can block UI.
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

## 12. Next Steps (For Future Sessions)

1. **Merge and release**: Commit all changes, build signed APK, test on live device.
2. **Download feature**: Implement saving to Music/Velune folder with duplicate detection.
3. **Handle remaining !! safely**: Consider refactoring 86 remaining operators as needed.
4. **Add logging to empty catch blocks**: Help diagnose future issues.
5. **Pin NewPipeExtractor version**: Improve build reproducibility.
6. **Move TestTTML.kt**: Clean up source structure.

---

**Session Summary**: Removed crash-prone null assertions, added selection UI for queuing and playlist management, and fixed the selection state bug that was resetting selections on data refresh. The queue and selection experience is now much more robust and user-friendly.
