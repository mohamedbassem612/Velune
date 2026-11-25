import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ScrollView {
    id: settings
    objectName: "desktopSettings"
    property var appState: ({})
    property string category: "All"
    property var prefs: appState.preferences || ({})
    property var content: appState.settings || ({})
    property var downloads: appState.downloads || ({})
    property var playback: appState.playback || ({})
    property var cache: appState.cache || ({})
    signal chooseDownloadFolder()
    signal importAudio()
    signal openDownloads()
    signal toggleFullScreen()
    signal openDevices()
    signal exportBackup()
    signal importBackup()
    clip: true
    contentWidth: availableWidth
    function preference(key, value) {
        let patch = {}; patch[key] = value;
        backend.action("preferences", patch);
    }
    function matches(name, keywords) {
        let query = settingsSearch.text.trim().toLowerCase();
        return !query || (name + " " + keywords).toLowerCase().indexOf(query) >= 0;
    }
    function scrollToCategory() {
        if (category === "Storage") {
            settingsSearch.clear();
            contentItem.contentY = Math.max(0, Math.min(storageGroup.y, contentItem.contentHeight - contentItem.height));
        } else if (category === "All") contentItem.contentY = 0;
    }
    onCategoryChanged: Qt.callLater(scrollToCategory)
    Component.onCompleted: Qt.callLater(scrollToCategory)
    ColumnLayout {
        width: settings.availableWidth
        spacing: 18
        Text {
            text: "Settings"
            color: "white"
            font.pixelSize: 36
            font.weight: Font.Bold
            Layout.topMargin: 15
        }
        Text {
            text: "Make Velune feel right for you."
            color: "#858585"
        }
        TextField {
            id: settingsSearch
            objectName: "settingsSearch"
            Layout.fillWidth: true
            placeholderText: "Find settings: downloads, playback, history, appearance…"
            Accessible.name: "Search settings"
            onTextEdited: settings.category = "All"
        }
        SettingsGroup {
            visible: settings.matches("Account", "google youtube music login sign in sync library devices connect import")
            title: "Account & devices"
            description: settings.appState.loggedIn ? (settings.appState.account + " · " + (settings.appState.email || "")) : "Sign in to access your YouTube Music library."
            Flow {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    text: settings.appState.loginPending ? "Cancel sign-in" : settings.appState.loggedIn ? "Sign out" : "Sign in with Google"
                    primary: !settings.appState.loggedIn
                    onClicked: backend.action(settings.appState.loggedIn ? "logout" : "login")
                }
                ActionButton {
                    text: settings.appState.refreshing ? "Refreshing…" : "Refresh library"
                    enabled: !!settings.appState.loggedIn && !settings.appState.refreshing
                    onClicked: backend.action("refresh")
                }
                ActionButton { text: "Available devices"; onClicked: settings.openDevices() }
                ActionButton { text: "Import audio"; onClicked: settings.importAudio() }
            }
        }
        SettingsGroup {
            visible: settings.matches("Appearance", "default open tab start page font system cover thumbnail lyrics text size spacing library list grid")
            objectName: "appearanceSettings"
            title: "Appearance"
            description: "Player, library and startup preferences."
            SettingsChoice {
                text: "Open Velune on"
                choices: ["Home", "Search", "Explore", "Library", "Downloads"]
                value: settings.prefs.defaultPage || "Home"
                onSelected: value => settings.preference("defaultPage", value)
            }
            SettingsChoice {
                text: "Library layout"
                choices: ["List", "Grid"]
                values: ["list", "grid"]
                value: settings.prefs.view || "list"
                onSelected: value => settings.preference("view", value)
            }
            SettingsToggle {
                text: "Use system font"
                description: "Use your desktop’s default font instead of Inter."
                checked: settings.prefs.systemFont || false
                onToggled: value => settings.preference("systemFont", value)
            }
            SettingsToggle {
                text: "Hide player artwork"
                checked: settings.prefs.hidePlayerArtwork || false
                onToggled: value => settings.preference("hidePlayerArtwork", value)
            }
            SettingsChoice {
                text: "Lyrics text size"
                choices: ["Small · 14", "Default · 18", "Large · 24", "Extra large · 32"]
                values: [14, 18, 24, 32]
                value: settings.prefs.lyricsFontSize || 18
                onSelected: value => settings.preference("lyricsFontSize", value)
            }
            SettingsChoice {
                text: "Lyrics line spacing"
                choices: ["Compact", "Default", "Relaxed"]
                values: [1, 1.4, 1.8]
                value: settings.prefs.lyricsLineHeight || 1.4
                onSelected: value => settings.preference("lyricsLineHeight", value)
            }
            ActionButton { text: "Toggle full screen"; onClicked: settings.toggleFullScreen() }
        }
        SettingsGroup {
            visible: settings.matches("Crossfade", "player audio seamless transition overlap fade duration")
            objectName: "playbackSettings"
            title: "Crossfade"
            description: "Blend the end of a song into the beginning of the next one during local playback. Device sessions use their own timing."
            SettingsChoice {
                text: "Transition duration"
                choices: ["Off", "2 seconds", "4 seconds", "6 seconds", "8 seconds", "12 seconds"]
                values: [0, 2, 4, 6, 8, 12]
                value: settings.prefs.crossfadeSeconds || 0
                onSelected: value => settings.preference("crossfadeSeconds", value)
            }
        }
        SettingsGroup {
            visible: settings.matches("Content", "explicit videos language country region account search top songs playlist size")
            title: "Content"
            description: "Language and region affect YouTube Music content and search results."
            SettingsToggle {
                text: "Hide explicit content"
                checked: settings.content.hideExplicit || false
                onToggled: value => backend.action("settings", {"hideExplicit": value})
            }
            SettingsToggle {
                text: "Hide music videos"
                checked: settings.content.hideVideos || false
                onToggled: value => backend.action("settings", {"hideVideos": value})
            }
            SettingsToggle {
                text: "Use my account for search"
                checked: settings.content.useLogin !== false
                onToggled: value => backend.action("settings", {"useLogin": value})
            }
            SettingsChoice {
                text: "Top songs playlist size"
                choices: ["25 songs", "50 songs", "100 songs", "200 songs"]
                values: [25, 50, 100, 200]
                value: settings.prefs.topSize || 50
                onSelected: value => settings.preference("topSize", value)
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Content language"; color: "#eeeeee"; Layout.fillWidth: true }
                TextField {
                    Layout.preferredWidth: 205
                    text: settings.content.language || "en"
                    placeholderText: "en or ar"
                    maximumLength: 12
                    Accessible.name: "Content language code"
                    onEditingFinished: backend.action("settings", {"language": text})
                }
            }
            RowLayout {
                Layout.fillWidth: true
                Text { text: "Content country"; color: "#eeeeee"; Layout.fillWidth: true }
                TextField {
                    Layout.preferredWidth: 205
                    text: settings.content.country || "US"
                    placeholderText: "US or EG"
                    maximumLength: 2
                    Accessible.name: "Content country code"
                    onEditingFinished: backend.action("settings", {"country": text.toUpperCase()})
                }
            }
        }
        SettingsGroup {
            visible: settings.matches("Storage", "download downloads folder location disk mp3 scan restore import")
            id: storageGroup
            objectName: "storageSettings"
            title: "Storage & downloads"
            description: "Choose where downloaded music is saved. Choose an existing Velune folder to restore your songs."
            Text {
                text: settings.downloads.folder || "No downloads folder selected"
                color: "#eeeeee"
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                ActionButton {
                    text: settings.downloads.folder ? "Change folder…" : "Choose folder…"
                    objectName: "chooseDownloadFolder"
                    primary: true
                    enabled: settings.downloads.canChangeFolder !== false
                    onClicked: settings.chooseDownloadFolder()
                }
                ActionButton {
                    text: "Open folder"
                    enabled: !!settings.downloads.folder
                    onClicked: Qt.openUrlExternally(settings.downloads.folderUrl)
                }
                ActionButton {
                    text: settings.downloads.scanning ? "Scanning…" : "Scan & restore songs"
                    enabled: !!settings.downloads.folder && !settings.downloads.busy && !settings.downloads.scanning
                    onClicked: backend.action("downloadScan")
                }
                ActionButton { text: "View downloads"; onClicked: settings.openDownloads() }
            }
            Text {
                text: (settings.downloads.items || []).length + " downloaded songs · " + ((settings.downloads.bytes || 0) / 1048576).toFixed(1) + " MB"
                color: "#999999"
            }
            Text {
                text: "Files stay in your folder after reinstalling. Changing the folder does not move or delete music. Downloads are saved as MP3."
                color: "#858585"
                font.pixelSize: 12
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            Text {
                text: settings.downloads.error || ""
                color: "#efab98"
                visible: text.length > 0
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
        }
        SettingsGroup {
            visible: settings.matches("Cache", "storage audio image pictures temporary size limit clear delete disk")
            title: "Cache"
            objectName: "cacheSettings"
            description: "Reusable device audio and artwork. Limits are enforced when playback and transfers are idle. Downloaded MP3 files are kept separately."
            Text {
                text: "Audio: " + ((settings.cache.audioBytes || 0) / 1048576).toFixed(1) + " MB  ·  Artwork: " + ((settings.cache.imagesBytes || 0) / 1048576).toFixed(1) + " MB  ·  Conversion sources: " + ((settings.cache.temporaryBytes || 0) / 1048576).toFixed(1) + " MB"
                color: "#aaaaaa"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            SettingsChoice {
                text: "Audio cache limit"
                choices: ["128 MB", "512 MB", "1 GB", "2 GB", "4 GB", "8 GB", "16 GB"]
                values: [128, 512, 1024, 2048, 4096, 8192, 16384]
                value: settings.cache.audioMiB || 1024
                onSelected: value => backend.action("cacheLimits", {"audioMiB": value, "imageMiB": settings.cache.imageMiB || 256})
            }
            SettingsChoice {
                text: "Artwork & lyrics cache limit"
                choices: ["16 MB", "64 MB", "128 MB", "256 MB", "512 MB", "1 GB"]
                values: [16, 64, 128, 256, 512, 1024]
                value: settings.cache.imageMiB || 256
                onSelected: value => backend.action("cacheLimits", {"audioMiB": settings.cache.audioMiB || 1024, "imageMiB": value})
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                ActionButton { text: "Refresh usage"; onClicked: backend.action("cacheRefresh") }
                ActionButton { text: "Clear audio cache…"; onClicked: { cacheConfirm.kind = "audio"; cacheConfirm.open(); } }
                ActionButton { text: "Clear artwork & lyrics…"; onClicked: { cacheConfirm.kind = "images"; cacheConfirm.open(); } }
                ActionButton { text: "Clear conversion cache…"; onClicked: { cacheConfirm.kind = "temporary"; cacheConfirm.open(); } }
            }
        }
        SettingsGroup {
            visible: settings.matches("Backup & restore", "storage export import library settings recovery file")
            title: "Backup & restore"
            objectName: "backupSettings"
            description: "Save your library, playlists, history and settings to a portable backup file. Music files and account sessions are not included."
            Flow {
                Layout.fillWidth: true
                spacing: 8
                ActionButton { text: "Create backup…"; primary: true; onClicked: settings.exportBackup() }
                ActionButton { text: "Restore backup…"; onClicked: settings.importBackup() }
            }
            Text {
                text: "Restoring replaces saved library and settings, then restarts Velune. Your current data is kept as a recovery snapshot. Choose your downloads folder again on a new computer."
                color: "#858585"
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
        SettingsGroup {
            visible: settings.matches("Privacy", "listening listen search history pause clear delete statistics tracking")
            objectName: "privacySettings"
            title: "Privacy & history"
            SettingsToggle {
                text: "Pause listening history"
                description: "Stop recording new local listening events and statistics. Existing history is kept."
                checked: settings.prefs.pauseListeningHistory || false
                onToggled: value => settings.preference("pauseListeningHistory", value)
            }
            SettingsToggle {
                text: "Pause search history"
                checked: settings.content.pauseHistory || false
                onToggled: value => backend.action("settings", {"pauseHistory": value})
            }
            RowLayout {
                ActionButton {
                    text: "Clear listening history…"
                    onClicked: { confirmClear.listening = true; confirmClear.open(); }
                }
                ActionButton {
                    text: "Clear search history…"
                    onClicked: { confirmClear.listening = false; confirmClear.open(); }
                }
            }
            Text {
                text: "History controls apply to Velune’s saved history on this computer."
                color: "#858585"
                font.pixelSize: 12
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
            }
        }
        SettingsGroup {
            visible: settings.matches("About", "version velune desktop qt music")
            title: "About Velune"
            description: "Velune Desktop " + (settings.appState.version || "")
            Text {
                text: "Your music, across your devices."
                color: "#aaaaaa"
            }
        }
        Item { Layout.preferredHeight: 20 }
    }
    Dialog {
        id: cacheConfirm
        property string kind: "audio"
        parent: Overlay.overlay
        anchors.centerIn: parent
        implicitWidth: 470
        width: Math.min(implicitWidth, parent.width - 60)
        modal: true
        title: "Clear " + (kind === "temporary" ? "conversion" : kind === "images" ? "artwork & lyrics" : "audio") + " cache?"
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Local playback will stop before clearing. Wait for downloads and device transfers to finish. Saved MP3 downloads are kept." + (cacheConfirm.kind === "temporary" ? " Failed conversions will need to download their source again." : "")
        }
        onAccepted: backend.action("cacheClear", {"kind": kind})
    }
    Dialog {
        id: confirmClear
        property bool listening: false
        parent: Overlay.overlay
        anchors.centerIn: parent
        implicitWidth: 470
        width: Math.min(implicitWidth, parent.width - 60)
        modal: true
        title: listening ? "Clear listening history?" : "Clear search history?"
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: confirmClear.listening ? "Remove saved listening history and reset listening statistics. Your songs and downloads are kept." : "Remove all saved search queries from this computer."
        }
        onAccepted: backend.action(listening ? "listeningHistoryClear" : "historyClear")
    }
}
