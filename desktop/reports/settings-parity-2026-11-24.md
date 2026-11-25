# Desktop settings compared with Android — 24 November 2026

The desktop settings screen follows Android's Appearance, Player & audio, Content, Storage and Privacy sections, with Account/devices and About. Sections appear vertically on one scrolling page, with each heading followed by its settings. Search filters the sections. These are desktop controls backed by working services; settings for missing services are not presented as switches.

## Implemented in this change

| Category | Desktop behavior |
| --- | --- |
| Appearance | Choose startup page and library list/grid; use system font; hide player artwork; change plain lyrics font size and line spacing. Changes apply immediately except startup-page choice, which applies on the next launch. |
| Crossfade | Automatic local queue transitions blend for 2–12 seconds using a prepared second audio helper and separate relays. Off by default; Connect sessions and repeat-one retain normal transitions. |
| Content | Retain explicit/video filters, authenticated search, content language and country; expose the existing Top songs playlist limit. These language controls affect content, not interface translation. |
| Storage | Downloads folder selection/change, open folder, scan/restore and count/size. The folder chooser is available only here and uses the system picker. A first download with no folder selected navigates to Storage and retains the requested songs until a folder is chosen or the picker is cancelled. Changing folders preserves existing files. |
| Cache | Real audio/artwork/lyrics/conversion-source byte counts, configurable limits, idle trimming and confirmed clearing. MP3s are excluded; active playback and transfers are protected. |
| Backup & restore | Validated versioned JSON backup; preview/confirmation; pre-restore snapshot and recovery journal; restart after restore. Credentials, certificates and music bytes are excluded. |
| Privacy | Pause/clear local listening history, pause/clear search history. Clear actions ask for confirmation. Listening deletion creates Connect tombstones so older library snapshots cannot restore the deleted entries. Songs and MP3s are retained. |
| Account / About | Google login/logout, library refresh, imports, Connect device navigation and application version. |

Preferences persist in `catalog.json` alongside existing library preferences. Content/search settings remain in `search.json`, download location in `downloads.json`, cache limits in `storage.json`. Last-playback recovery is automatic. The Remember playback, mute-to-pause, automatic liked-song downloads, default speed and sleep timer settings were removed at the user's request. The library listens to actual playback events to collect listening time, excluding remote playback on another device. Pausing history drops subsequent listening time until recording is enabled again.

## Remaining Android settings and priority

| Priority | Android setting | Work needed on desktop |
| --- | --- | --- |
| High | Audio quality, streaming client and metered-network behavior | Carry quality/client choices consistently through native resolution, helper fallback, prefetch and downloads; define desktop metered detection. Current desktop selection is fixed. |
| High | Equalizer, normalization and skip silence | Implement and test native audio processing/control first. Crossfade is now implemented separately. |
| Medium | Automatic skip on playback errors and seek increment | Add bounded skip rules, avoid retry loops and make keyboard/player seek behavior consistent. |
| Medium | Themes, dynamic color, palette/background customization | Replace hardcoded colors with a theme system. Dark/light/system modes must style all screens and dialogs coherently. |
| Medium | Interface language | Add QML translation catalogs and runtime locale handling. Content language is already supported separately. |
| Medium | Lyrics providers, preferred source, translation/romanization and preloading | Desktop currently reads peer lyrics as plain text; implement fetching, parsing and synchronized rendering before adding these settings. |
| Medium | Discord, Last.fm, ListenBrainz | Implement credential storage, presence/scrobbling lifecycle and service clients. |
| Medium | Listen Together / online rooms | Desktop Connect is separate from Android's Together sessions; port session services before exposing room settings. |
| Medium | Proxy / bypass rules | Route catalog, resolver, helper and download requests through a consistent supported policy. |
| Medium | Updates, changelog and link defaults | Add verified desktop update/install flows and OS URL/protocol registration. Android APK installation does not apply. |
| Later | Library shortcut visibility, playlist tag display and grid sizing | Backend has partial preferences, but the QML navigation still needs consistent consumers before these controls are exposed. |
| Android-specific | Battery optimization, notification permissions, Android equalizer panel, audio offload, screenshot blocking, task-clear and Bluetooth auto-start | Do not copy these switches directly; design desktop-specific integration only when needed. |

## Validation

- Existing download/Android tag compatibility tests.
- Playback tests: persistence across service restarts and latest-position recovery.
- Crossfade tests: complementary volumes, outgoing-source cleanup, pause/release and automatic queue advancement; native libmpv decoder transitions with null audio output.
- Storage tests: limit enforcement, busy protection, MP3 preservation, symlink exclusion, backup round-trip and interrupted-restore recovery.
- Privacy tests: paused recording, resumption and clearing history without resurrection from an older Connect snapshot.
- QML tests: settings load; search and categories; folder selection lives in Settings; first-download routing preserves the batch; saved startup page.
- A rendered settings window is inspected using the Qt UI test renderer. These checks do not imply Windows or live third-party integration validation.
