# Velune Desktop 0.3.0

The desktop now uses Qt Quick/QML for its interface and C++ for its services. The HTML/CSS design has been recreated in QML. Electron and Node.js are absent from the active build and installed package.

`velune` is a QGuiApplication with Qt Quick Controls, library/search services, LAN discovery and Velune Connect protocol 4. It has no Qt Widgets, Qt Multimedia or WebEngine linkage. `velune-audio` is the existing Qt-free C++20 libmpv host, running separately for each media source. `velune-login` is a separate Qt Quick WebEngine application opened only by an explicit sign-in action. Its Chromium profile stays in memory and its process exits after the main application validates the account, on cancellation, or on sign-out. See [Google sign-in](GOOGLE_SIGN_IN.md).

The QML UI includes the sidebar, home cards, hover states, bottom player, search/filter/history/pagination, library list/grid and sorting, likes, playlists/tags, imports, device selection, queue editing, peer lyrics and playback settings. Artwork and shared assets use the existing metadata and cache. HTML/CSS files in `design/` are visual references only.

Desktop playback remembers the current queue, selected song and position in `last-playback.json` in the app data directory. It checkpoints every two seconds and on normal exit. On reopening, the player restores paused; pressing Play resolves the current source and seeks to the saved position. No expiring stream URLs or Connect ownership are restored. Shuffle, repeat, speed and volume are retained. Clearing the queue clears the saved selection.

## Downloads

Choose **Settings → Storage → Choose folder**, then **Download MP3** from a song menu. The first download opens Storage settings if no folder is configured. Collections have **Download all**. The Downloads page shows queued jobs, progress, cancel/retry actions and saved songs, with **Scan folder** to refresh files added externally. Folder selection, changing location and opening the folder are in Settings. Changing the folder selects a new destination; it does not move or delete existing music.

Settings now has searchable Account, Appearance, Player & audio, Content, Storage, Privacy and About sections. See the [Android settings comparison](reports/settings-parity-2026-11-24.md) for implemented behavior and remaining work. Listening/search history controls operate on Velune's local history; deleting listening history also keeps deletion markers for Connect merges.

Saved files are standard MP3s with title, artist, album and optional embedded artwork. Conversion matches Android's `libmp3lame -q:a 2` and ID3v2.3 settings. The identity is Android's exact `FieldKey.CUSTOM1` mapping: a `COMM` frame with description `Songs-DB_Custom1` and value `velune:<11-character-video-id>`. Recovery reads that tag, not the filename or an installation-specific database. Select the same folder after reinstalling, or select its new location after moving the files to another drive/OS. Recognized files appear as downloaded and play locally; no duplicate request is made. Android-tagged MP3s are recognized too. Windows storage paths use Qt APIs and portable filenames; a Windows application build is still separate work and has not been tested here.

Folder scans run on selection, startup and before a new batch; the root directory is watched for changes. Nested folders are scanned too (use **Scan folder** after changing a nested folder externally). Unrecognized MP3s remain untouched and can still be added using Import audio. Renaming a file preserves recognition as long as its embedded identity is retained. Downloads are registered without automatically liking songs.

Complete local/Connect audio is reused before resolving a network stream. Network downloads validate bounded byte ranges and a complete length before conversion. A completed source is kept for retry if conversion fails. A temporary MP3 is tagged and checked before being renamed into the selected folder, and existing unrelated files are never overwritten. Cancelling stops the active operation; incomplete files are not listed as downloaded. The pending queue is currently session-local; completed MP3s and completed conversion sources survive restarts.

FFmpeg with `libmp3lame` is required for new downloads, either on PATH or as `tools/ffmpeg` (`tools/ffmpeg.exe` on Windows). Restoring and playing existing MP3s does not need FFmpeg. Packaging includes the supplied FFmpeg executable and its shared-library dependencies.

Connect provides a TLS coordinator and renderer, mutual certificate identities, mDNS discovery, library merge, complete audio/cache transfer and optional artwork/lyrics. Catalog cache fills use bounded HTTPS ranges, a 512 MiB per-song limit and atomic completion. Playback handoff prepares the destination while paused, waits for source release acknowledgement from the native audio process, persists the new owner and then commits. Source/helper generations fence obsolete events. LAN protocol 4 retains Android's existing trust model; see its implementation before deploying on an untrusted network.

## Linux build

Qt 6.4 development packages, OpenSSL and QtKeychain are required. On Ubuntu 24.04:

```sh
sudo apt-get install build-essential cmake ffmpeg qt6-base-dev qt6-declarative-dev qt6-webengine-dev libqt6keychain1 qtkeychain-qt6-dev libssl-dev \
  qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript \
  qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-controls \
  qml6-module-qtquick-templates qml6-module-qtquick-layouts qml6-module-qtquick-dialogs \
  qml6-module-qtquick-nativestyle qml6-module-qtquick-shapes qml6-module-qtwebengine qml6-module-qtwebchannel \
  qml6-module-qt-labs-folderlistmodel
bash scripts/build-linux.sh
```

The build compiles C++ and QML without starting the application or running functional tests. libmpv headers are vendored; the native helper dynamically loads the supplied libmpv 0.37+ runtime. It never falls back to Qt Multimedia.

## Install Velune Desktop on Linux

GitHub currently contains the Linux source and build workflow, but does not publish a ready-to-run binary release. To install the current Qt Quick desktop version on Ubuntu 24.04 (x86_64), clone the repository and run the following from its root:

```sh
git clone https://github.com/mohamedbassem612/Velune.git
cd Velune/desktop

sudo apt-get update
sudo apt-get install -y build-essential cmake ffmpeg qt6-base-dev qt6-declarative-dev \
  qt6-webengine-dev qtkeychain-qt6-dev libssl-dev \
  qml6-module-qtqml qml6-module-qtqml-models qml6-module-qtqml-workerscript \
  qml6-module-qtquick qml6-module-qtquick-window qml6-module-qtquick-controls \
  qml6-module-qtquick-templates qml6-module-qtquick-layouts \
  qml6-module-qtquick-dialogs qml6-module-qtquick-nativestyle \
  qml6-module-qtquick-shapes qml6-module-qtwebengine qml6-module-qtwebchannel \
  qml6-module-qt-labs-folderlistmodel

bash scripts/build-linux.sh
```

The build must then be packaged with the audited libmpv runtime and verified `yt-dlp`/Deno tools. Pass their directories explicitly:

```sh
python3 scripts/package-linux.py \
  --audio-runtime /path/to/audited/native/lib \
  --tools /path/to/verified/tools \
  --ffmpeg "$(command -v ffmpeg)"
bash scripts/install-linux.sh
```

The installer places the application in `~/.local/lib/velune`, creates the launcher `~/.local/bin/velune`, and adds a desktop-menu entry. Start it with:

```sh
~/.local/bin/velune
```

The package is currently targeted at Ubuntu 24.04/Linux x86_64. The installer does not launch the application or modify the music library. `--audio-runtime` must contain `libmpv.so.2`, and `--tools` must contain `yt-dlp`, `deno`, and `versions.json`; without these verified runtime files packaging stops with an error.

## Package and install

Supply a matching audited libmpv runtime and the verified yt-dlp/Deno tools. Deno is a playback extraction helper; there is no Node service. The existing `install-playback-tools.py` can prepare pinned tools. Packaging does not download dependencies or execute a Velune process.

```sh
python3 scripts/package-linux.py \
  --qml /usr/lib/x86_64-linux-gnu/qt6/qml \
  --audio-runtime /path/to/audited/native/lib \
  --tools /path/to/verified/tools
bash scripts/install-linux.sh
```

Use `--ffmpeg /path/to/ffmpeg` when FFmpeg is not installed on PATH. Include the corresponding licensing/source notices with a distributed build.

Downloads tests (FFmpeg must be available) run without launching the application or accessing YouTube:

```sh
cmake -S . -B build-qml -DVELUNE_DOWNLOAD_TESTS=ON
cmake --build build-qml --parallel 4
ctest --test-dir build-qml --output-on-failure
```

They cover Android-style ID3v2.3/v2.4 tags, renamed files, reinstall/drive relocation with fresh app data, incomplete files, duplicate prevention, cancellation while queued, actual MP3 conversion from complete cache, and simulated network range failures/retry. Live YouTube downloads and native folder-picker interaction require a manual end-to-end check.

The generated identity was also checked in both directions against Android's actual `jaudiotagger-kt` 1.0.0 library: Android read a desktop-written `CUSTOM1`, and desktop read an Android-written `CUSTOM1` from a real MP3.

The package is `dist/linux-qml/`; installation is `~/.local/lib/velune/` with launcher `~/.local/bin/velune`. Qt libraries/plugins/QML and Chromium resources are staged privately; the main process only loads the modules it uses. The audio child has a separate library path. System libc, the loader and graphics drivers remain host dependencies. Packages target Linux x86_64 / Ubuntu 24.04 and are not a universal Linux binary.

The installer preserves the preceding installation as a rollback directory, never modifies personal library files and does not launch the app. The migration also preserves the original Qt Widgets and Electron source archives outside the repo in the task's `backups/` folder. To roll back, close Velune and point the launcher at the desired backup together with its original environment; do not copy only an executable between runtimes.

## Data and validation

The default data location stays `~/.local/share/Velune/Velune`. Library/catalog/search/media schemas, Connect certificates and completed peer audio are reused. Account sessions use the system keyring; Electron's encrypted session is retained in place but is not imported into C++. An older verified Qt keyring session can be restored; otherwise sign in again.

Build, QML compilation, source review and ELF dependency checks were performed. Application launch, Google sign-in, audio output, UI appearance and device/transfer flows are deliberately left to the user as requested. This build has not been functionally validated. Chromium/Qt/libmpv/helper versions and third-party source obligations must be reviewed before distributing a release; see [notices](THIRD_PARTY.md). Windows packaging is described in [WINDOWS_DEPLOYMENT.md](WINDOWS_DEPLOYMENT.md).
