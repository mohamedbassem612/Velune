import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

ApplicationWindow {
    id: root
    visible: true
    width: 1440
    height: 900
    minimumWidth: 1000
    minimumHeight: 680
    color: "#050505"
    title: "Velune"
    flags: Qt.Window | Qt.FramelessWindowHint
    palette.window: "#111111"
    palette.windowText: "#eeeeee"
    palette.base: "#191919"
    palette.text: "#eeeeee"
    palette.button: "#202020"
    palette.buttonText: "#eeeeee"
    palette.highlight: "#777777"
    palette.highlightedText: "white"
    font.family: (st.preferences || {}).systemFont ? (st.systemFont || "Sans Serif") : "Inter"
    font.pixelSize: 14
    property var st: backend.state
    property var pb: st.playback || ({})
    property var song: st.song || ({})
    property string page: "Home"
    property string section: "songs"
    property string scope: "all"
    property bool grid: (st.preferences || {}).view === "grid"
    property string settingsCategory: "All"
    property var selectedItem: ({})
    property int selectedQueue: -1
    property string toast: ""
    property int sideWidth: width < 1250 ? 245 : 285
    property var searchInput: null
    property var collectionItems: st.collection || []
    Component.onCompleted: root.navigate((root.st.preferences || {}).defaultPage || "Home")
    property var downloads: st.downloads || ({})
    property var pendingDownloads: []
    function downloadItems(items) {
        if (!downloads.folder) {
            pendingDownloads = items;
            settingsCategory = "Storage";
            navigate("Settings");
            toast = "Choose your downloads folder in Settings → Storage to start downloading.";
            toastTimer.restart();
        } else backend.action("download", {"items": items});
    }
    function saveDownloadFolder(url) {
        let startDownloads = root.pendingDownloads.length > 0;
        backend.action("downloadFolder", {"url": url, "items": root.pendingDownloads});
        root.pendingDownloads = [];
        if (startDownloads) Qt.callLater(function() {
            if (root.downloads.folder) root.navigate("Downloads");
        });
    }
    function statisticsText() {
        let s = root.st.stats || {};
        return Math.floor((s.listeningMs || 0) / 60000) + " minutes listened  ·  " + (s.songs || 0) + " songs  ·  " + (s.events || 0) + " plays";
    }
    function time(ms) {
        let n = Math.max(0, Math.floor((ms || 0) / 1000));
        return Math.floor(n / 60) + ":" + ("0" + (n % 60)).slice(-2);
    }
    function navigate(name) {
        page = name;
        if (name === "Explore")
            backend.action("explore");
        if (name === "Available devices")
            backend.action("discover");
    }
    function openItem(item) {
        backend.action("open", {
                "item": item
            });
        if (item.kind && item.kind !== "song")
            collection.open();
    }
    function menuItem(item, index) {
        selectedItem = item;
        selectedQueue = index === undefined ? -1 : index;
        trackMenu.popup();
    }
    function playItems(items, index) {
        backend.action("play", {
                "items": items,
                "index": index || 0
            });
    }
    function libraryFilter() {
        backend.action("library", {
                "section": section,
                "scope": scope
            });
    }
    Connections {
        target: backend
        function onDownloadFolderRequired() { root.settingsCategory = "Storage"; root.navigate("Settings"); }
        function onRestorePreviewReady() { restoreConfirmation.open(); }
        function onNotification(text) {
            root.toast = text;
            toastTimer.restart();
        }
    }
    Timer {
        id: toastTimer
        interval: 6500
        onTriggered: root.toast = ""
    }
    Shortcut {
        sequence: "Space"
        enabled: !(root.activeFocusItem instanceof TextInput) && !(root.activeFocusItem instanceof TextEdit)
        onActivated: backend.action(root.pb.playing ? "pause" : "resume")
    }
    Shortcut {
        sequence: "Ctrl+F"
        onActivated: {
            root.page = "Search";
            searchFocus.start();
        }
    }
    Shortcut {
        sequence: "F11"
        onActivated: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen()
    }
    Timer {
        id: searchFocus
        interval: 60
        onTriggered: {
            if (root.searchInput)
                root.searchInput.forceActiveFocus();
        }
    }
    Rectangle {
        width: root.sideWidth
        anchors.top: parent.top
        anchors.bottom: player.top
        color: "#070707"
        Rectangle {
            anchors.right: parent.right
            height: parent.height
            width: 1
            color: "#151515"
        }
        ColumnLayout {
            anchors.fill: parent
            anchors.margins: 25
            spacing: 10
            RowLayout {
                Layout.topMargin: 12
                Layout.bottomMargin: 38
                spacing: 13
                Image {
                    source: "qrc:/assets/velune.svg"
                    sourceSize.width: 34
                    sourceSize.height: 34
                    Layout.preferredWidth: 34
                    Layout.preferredHeight: 34
                }
                Text {
                    text: "velune"
                    color: "white"
                    font.pixelSize: 29
                    font.weight: Font.DemiBold
                    font.letterSpacing: -1
                }
            }
            Repeater {
                model: [{
                        "name": "Home",
                        "icon": "⌂"
                    }, {
                        "name": "Search",
                        "icon": "⌕"
                    }, {
                        "name": "Explore",
                        "icon": "◉"
                    }, {
                        "name": "Library",
                        "icon": "▥"
                    }, {
                        "name": "Downloads",
                        "icon": "↓"
                    }, {
                        "name": "Available devices",
                        "icon": "▣"
                    }, {
                        "name": "Settings",
                        "icon": "⚙"
                    }]
                delegate: ActionButton {
                    id: navButton
                    required property var modelData
                    Layout.fillWidth: true
                    implicitHeight: 49
                    text: modelData.icon + "    " + modelData.name
                    selected: root.page === modelData.name
                    onClicked: {
                        if (modelData.name === "Settings") root.settingsCategory = "All";
                        root.navigate(modelData.name);
                    }
                    contentItem: Text {
                        text: navButton.text
                        color: navButton.selected || navButton.hovered ? "white" : "#898989"
                        font.pixelSize: 15
                        verticalAlignment: Text.AlignVCenter
                        leftPadding: 10
                    }
                }
            }
            Item {
                Layout.fillHeight: true
            }
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 1
                color: "#1c1c1c"
                Layout.bottomMargin: 12
            }
            RowLayout {
                Rectangle {
                    Layout.preferredWidth: 6
                    Layout.preferredHeight: 6
                    radius: 3
                    color: "#cfcfcf"
                }
                Text {
                    text: root.st.connection || "This computer"
                    color: "#989898"
                    font.pixelSize: 12
                    Layout.fillWidth: true
                    elide: Text.ElideRight
                }
            }
            ActionButton {
                text: root.st.loginPending ? "Cancel sign-in" : root.st.loggedIn ? (root.st.account || "Account") : "Sign in"
                Layout.fillWidth: true
                onClicked: root.st.loggedIn ? root.navigate("Settings") : backend.action("login")
            }
        }
    }
    Item {
        anchors.left: parent.left
        anchors.leftMargin: root.sideWidth
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.bottom: player.top
        Rectangle {
            id: titlebar
            height: 68
            width: parent.width
            color: "#050505"
            DragHandler {
                target: null
                onActiveChanged: if (active)
                    root.startSystemMove()
            }
            TapHandler {
                onDoubleTapped: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
            }
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 28
                anchors.rightMargin: 15
                spacing: 8
                ActionButton {
                    text: "‹"
                    font.pixelSize: 25
                    hint: "Home"
                    onClicked: root.navigate("Home")
                }
                Text {
                    text: root.page
                    color: "#777777"
                    font.pixelSize: 13
                }
                Item {
                    Layout.fillWidth: true
                }
                Text {
                    text: "0.3.0"
                    color: "#555555"
                    font.pixelSize: 11
                    Layout.rightMargin: 15
                }
                ActionButton {
                    text: "−"
                    hint: "Minimize"
                    onClicked: root.showMinimized()
                }
                ActionButton {
                    text: "□"
                    hint: "Maximize"
                    onClicked: root.visibility === Window.Maximized ? root.showNormal() : root.showMaximized()
                }
                ActionButton {
                    text: "×"
                    hint: "Close"
                    onClicked: root.close()
                }
            }
        }
        Loader {
            id: content
            anchors.top: titlebar.bottom
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: root.width < 1250 ? 28 : 42
            anchors.rightMargin: root.width < 1250 ? 28 : 42
            sourceComponent: root.page === "Home" ? homePage : root.page === "Search" ? searchPage : root.page === "Library" ? libraryPage : root.page === "Available devices" ? devicesPage : root.page === "Settings" ? settingsPage : root.page === "Downloads" ? downloadsPage : explorePage
        }
    }
    Component {
        id: homePage
        ScrollView {
            clip: true
            contentWidth: availableWidth
            ColumnLayout {
                width: parent.width
                spacing: 26
                Text {
                    text: "Welcome back"
                    color: "white"
                    font.pixelSize: 40
                    font.weight: Font.Bold
                    Layout.topMargin: 20
                    font.letterSpacing: -1.6
                }
                Text {
                    text: "Your music, right where you left it."
                    color: "#818181"
                    font.pixelSize: 14
                    Layout.topMargin: -14
                }
                GridLayout {
                    columns: 3
                    rowSpacing: 12
                    columnSpacing: 14
                    Layout.fillWidth: true
                    Repeater {
                        model: 9
                        delegate: Rectangle {
                            required property int index
                            property var entry: index === 0 ? ({
                                    "title": "Liked songs",
                                    "artwork": ""
                                }) : (root.st.home || [])[index - 1] || ({
                                    "title": index === 1 ? "Daily mix 1" : index === 2 ? "Daily mix 2" : "Your library",
                                    "artwork": ""
                                })
                            Layout.fillWidth: true
                            Layout.preferredHeight: 82
                            radius: 13
                            color: quickHover.hovered ? "#242424" : "#141414"
                            Behavior on color  {
                                ColorAnimation {
                                    duration: 130
                                }
                            }
                            HoverHandler {
                                id: quickHover
                            }
                            TapHandler {
                                onTapped: {
                                    if (index === 0) {
                                        root.section = "songs";
                                        root.scope = "liked";
                                        root.libraryFilter();
                                        root.navigate("Library");
                                    } else if (entry.id)
                                        root.openItem(entry);
                                    else {
                                        root.scope = "all";
                                        root.libraryFilter();
                                        root.navigate("Library");
                                    }
                                }
                            }
                            RowLayout {
                                anchors.fill: parent
                                anchors.margins: 10
                                spacing: 12
                                Artwork {
                                    source: entry.artwork || ""
                                    symbol: index === 0 ? "♥" : "♫"
                                    Layout.preferredWidth: 61
                                    Layout.preferredHeight: 61
                                    color: index === 0 ? "#262626" : "#191919"
                                }
                                Text {
                                    text: entry.title || ""
                                    color: "white"
                                    font.pixelSize: 13
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: "▶"
                                    color: "white"
                                    visible: quickHover.hovered
                                    Layout.rightMargin: 6
                                }
                            }
                        }
                    }
                }
                RowLayout {
                    Layout.fillWidth: true
                    Text {
                        text: "Made for you"
                        color: "white"
                        font.pixelSize: 25
                        font.weight: Font.Bold
                    }
                    Item {
                        Layout.fillWidth: true
                    }
                    ActionButton {
                        text: "View library  →"
                        onClicked: root.navigate("Library")
                    }
                }
                GridLayout {
                    columns: 3
                    columnSpacing: 22
                    Layout.fillWidth: true
                    Repeater {
                        model: 3
                        delegate: Rectangle {
                            required property int index
                            property var entry: (root.st.home || [])[index] || ({
                                    "title": ["Your favorites", "Rediscover your music", "Find your next favorite"][index],
                                    "artist": ["All the songs you love", "Explore your library", "Search YouTube Music"][index]
                                })
                            Layout.fillWidth: true
                            Layout.preferredHeight: width + 68
                            radius: 18
                            color: cardHover.hovered ? "#1c1c1c" : "#111111"
                            Behavior on color  {
                                ColorAnimation {
                                    duration: 140
                                }
                            }
                            HoverHandler {
                                id: cardHover
                            }
                            TapHandler {
                                onTapped: entry.id ? root.openItem(entry) : root.navigate(index === 2 ? "Search" : "Library")
                            }
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.margins: 14
                                spacing: 10
                                Artwork {
                                    source: entry.artwork || ""
                                    Layout.fillWidth: true
                                    Layout.fillHeight: true
                                    symbol: ["♥", "♫", "⌕"][index]
                                    ActionButton {
                                        anchors.right: parent.right
                                        anchors.bottom: parent.bottom
                                        anchors.margins: 12
                                        text: "▶"
                                        primary: true
                                        radius: 26
                                        implicitWidth: 48
                                        implicitHeight: 48
                                        opacity: cardHover.hovered ? 1 : 0
                                        Behavior on opacity  {
                                            NumberAnimation {
                                                duration: 140
                                            }
                                        }
                                        onClicked: entry.id ? root.openItem(entry) : root.navigate(index === 2 ? "Search" : "Library")
                                    }
                                }
                                Text {
                                    text: entry.title || ""
                                    color: "white"
                                    font.pixelSize: 15
                                    font.weight: Font.DemiBold
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                                Text {
                                    text: entry.artist || ""
                                    color: "#7d7d7d"
                                    font.pixelSize: 12
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }
                }
                Item {
                    Layout.preferredHeight: 22
                }
            }
        }
    }
    Component {
        id: searchPage
        ColumnLayout {
            function focusSearch() {
                searchField.forceActiveFocus();
            }
            spacing: 16
            Text {
                text: "Search"
                color: "white"
                font.pixelSize: 36
                font.weight: Font.Bold
                Layout.topMargin: 15
            }
            TextField {
                id: searchField
                Component.onCompleted: root.searchInput = searchField
                Layout.fillWidth: true
                implicitHeight: 54
                placeholderText: "Songs, artists, albums, playlists"
                color: "white"
                font.pixelSize: 17
                selectByMouse: true
                leftPadding: 20
                background: Rectangle {
                    color: "#161616"
                    radius: 13
                    border.width: searchField.activeFocus ? 1 : 0
                    border.color: "#606060"
                }
                onAccepted: backend.action("search", {
                        "query": text,
                        "filter": searchFilter.currentValue,
                        "source": searchSource.currentValue
                    })
                onTextEdited: suggestionTimer.restart()
                Timer {
                    id: suggestionTimer
                    interval: 220
                    onTriggered: backend.action("suggest", {
                            "query": searchField.text
                        })
                }
            }
            RowLayout {
                ComboBox {
                    id: searchFilter
                    textRole: "label"
                    valueRole: "value"
                    model: [{
                            "label": "Songs",
                            "value": "songs"
                        }, {
                            "label": "All",
                            "value": "all"
                        }, {
                            "label": "Albums",
                            "value": "albums"
                        }, {
                            "label": "Artists",
                            "value": "artists"
                        }, {
                            "label": "Playlists",
                            "value": "playlists"
                        }, {
                            "label": "Videos",
                            "value": "videos"
                        }]
                    onActivated: backend.action("search", {
                            "query": searchField.text,
                            "filter": currentValue
                        })
                }
                ComboBox {
                    id: searchSource
                    textRole: "label"
                    valueRole: "value"
                    model: [{
                            "label": "Online",
                            "value": "online"
                        }, {
                            "label": "My library",
                            "value": "library"
                        }, {
                            "label": "Offline",
                            "value": "offline"
                        }]
                    onActivated: backend.action("search", {
                            "query": searchField.text,
                            "source": currentValue
                        })
                }
                ActionButton {
                    text: root.st.searchBusy ? "Searching…" : "Search"
                    primary: true
                    onClicked: backend.action("search", {
                            "query": searchField.text,
                            "filter": searchFilter.currentValue,
                            "source": searchSource.currentValue
                        })
                }
                Item {
                    Layout.fillWidth: true
                }
                BusyIndicator {
                    running: root.st.searchBusy || false
                    implicitWidth: 28
                    implicitHeight: 28
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 5
                visible: searchField.text.length === 0
                Repeater {
                    model: root.st.history || []
                    delegate: ActionButton {
                        required property string modelData
                        text: modelData
                        onClicked: {
                            searchField.text = modelData;
                            backend.action("search", {
                                    "query": modelData
                                });
                        }
                        onPressAndHold: backend.action("historyRemove", {
                                "query": modelData
                            })
                    }
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 5
                visible: searchField.activeFocus && searchField.text.length > 0
                Repeater {
                    model: ((root.st.suggestions || {}).queries || []).slice(0, 5)
                    delegate: ActionButton {
                        required property string modelData
                        text: modelData
                        onClicked: {
                            searchField.text = modelData;
                            backend.action("search", {
                                    "query": modelData
                                });
                        }
                    }
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.st.results || []
                spacing: 4
                delegate: TrackRow {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    entry: modelData
                    number: index
                    onActivated: root.openItem(modelData)
                    onMenuRequested: root.menuItem(modelData)
                }
                footer: ActionButton {
                    text: "Load more"
                    visible: !!root.st.continuation
                    onClicked: backend.action("more")
                }
                ScrollBar.vertical: ScrollBar {
                }
                Label {
                    anchors.centerIn: parent
                    visible: parent.count === 0 && !root.st.searchBusy
                    text: "Search for something you love"
                    color: "#777777"
                }
            }
        }
    }
    Component {
        id: libraryPage
        ColumnLayout {
            spacing: 14
            RowLayout {
                Layout.topMargin: 15
                Text {
                    text: "Your library"
                    color: "white"
                    font.pixelSize: 36
                    font.weight: Font.Bold
                }
                Item {
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: "+ Playlist"
                    onClicked: {
                        root.selectedItem = {};
                        playlistTitle.text = "";
                        playlistEditor.open();
                    }
                }
                ActionButton {
                    text: "Import"
                    onClicked: importDialog.open()
                }
                ActionButton {
                    text: "↻"
                    hint: "Refresh library"
                    onClicked: backend.action("refresh")
                }
            }
            RowLayout {
                Repeater {
                    model: ["songs", "albums", "artists", "playlists", "history", "top", "cached"]
                    delegate: ActionButton {
                        required property string modelData
                        text: modelData.charAt(0).toUpperCase() + modelData.slice(1)
                        selected: root.section === modelData
                        onClicked: {
                            root.section = modelData;
                            root.libraryFilter();
                        }
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
            }
            RowLayout {
                ComboBox {
                    textRole: "label"
                    valueRole: "value"
                    model: [{
                            "label": "All",
                            "value": "all"
                        }, {
                            "label": "Liked",
                            "value": "liked"
                        }, {
                            "label": "Offline",
                            "value": "offline"
                        }, {
                            "label": "Fully offline",
                            "value": "fullOffline"
                        }]
                    onActivated: {
                        root.scope = currentValue;
                        root.libraryFilter();
                    }
                    Component.onCompleted: currentIndex = root.scope === "liked" ? 1 : 0
                }
                ComboBox {
                    textRole: "label"
                    valueRole: "value"
                    model: [{
                            "label": "Recently added",
                            "value": "added"
                        }, {
                            "label": "Title",
                            "value": "name"
                        }, {
                            "label": "Artist",
                            "value": "artist"
                        }, {
                            "label": "Most played",
                            "value": "playTime"
                        }, {
                            "label": "Duration",
                            "value": "duration"
                        }]
                    onActivated: backend.action("library", {
                            "sort": currentValue
                        })
                }
                ActionButton {
                    text: "↑↓"
                    property bool descending: true
                    hint: "Reverse sort"
                    onClicked: {
                        descending = !descending;
                        backend.action("library", {
                                "descending": descending
                            });
                    }
                }
                Item {
                    Layout.fillWidth: true
                }
                Text {
                    text: (root.st.library || []).length + " items"
                    color: "#737373"
                    font.pixelSize: 12
                }
                ActionButton {
                    text: root.grid ? "☷" : "▦"
                    hint: "Toggle layout"
                    onClicked: backend.action("preferences", {"view": root.grid ? "list" : "grid"})
                }
            }
            Loader {
                Layout.fillWidth: true
                Layout.fillHeight: true
                sourceComponent: root.grid ? libraryGrid : libraryList
            }
        }
    }
    Component {
        id: libraryList
        ListView {
            clip: true
            model: root.st.library || []
            spacing: 3
            delegate: TrackRow {
                required property var modelData
                required property int index
                width: ListView.view.width
                entry: modelData
                number: index
                current: root.song.id === modelData.id
                onActivated: root.openItem(modelData)
                onMenuRequested: root.menuItem(modelData)
            }
            ScrollBar.vertical: ScrollBar {
            }
            Label {
                visible: parent.count === 0
                anchors.centerIn: parent
                color: "#777777"
                text: "Your library is ready for music. Import audio or sign in."
                wrapMode: Text.WordWrap
            }
        }
    }
    Component {
        id: libraryGrid
        GridView {
            id: libraryGridView
            clip: true
            model: root.st.library || []
            cellWidth: width / Math.max(3, Math.floor(width / 200))
            cellHeight: cellWidth + 55
            delegate: Item {
                required property var modelData
                width: libraryGridView.cellWidth
                height: libraryGridView.cellHeight
                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8
                    Artwork {
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        source: modelData.artwork || ""
                        TapHandler {
                            onTapped: root.openItem(modelData)
                        }
                        TapHandler {
                            acceptedButtons: Qt.RightButton
                            onTapped: root.menuItem(modelData)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        Text {
                            text: modelData.title || ""
                            color: "white"
                            font.pixelSize: 13
                            Layout.fillWidth: true
                            elide: Text.ElideRight
                        }
                        ActionButton {
                            text: "···"
                            implicitWidth: 30
                            implicitHeight: 26
                            onClicked: root.menuItem(modelData)
                        }
                    }
                }
            }
            ScrollBar.vertical: ScrollBar {
            }
        }
    }
    Component {
        id: explorePage
        ColumnLayout {
            spacing: 18
            Text {
                text: "Explore"
                color: "white"
                font.pixelSize: 36
                font.weight: Font.Bold
                Layout.topMargin: 15
            }
            RowLayout {
                Text {
                    text: "Discover something new"
                    color: "#888888"
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: "Refresh"
                    onClicked: backend.action("explore")
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.st.results || []
                delegate: TrackRow {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    entry: modelData
                    number: index
                    onActivated: root.openItem(modelData)
                    onMenuRequested: root.menuItem(modelData)
                }
                ScrollBar.vertical: ScrollBar {
                }
            }
        }
    }
    Component {
        id: devicesPage
        ColumnLayout {
            spacing: 18
            Text {
                text: "Available devices"
                color: "white"
                font.pixelSize: 36
                font.weight: Font.Bold
                Layout.topMargin: 15
            }
            Text {
                text: "Keep your devices on the same network to listen together."
                color: "#818181"
            }
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: 95
                radius: 15
                color: "#161616"
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 20
                    spacing: 18
                    Text {
                        text: "▣"
                        color: "white"
                        font.pixelSize: 32
                    }
                    ColumnLayout {
                        Layout.fillWidth: true
                        Text {
                            text: "This computer"
                            color: "white"
                            font.pixelSize: 17
                            font.weight: Font.DemiBold
                        }
                        Text {
                            text: root.st.local ? "Selected for playback" : "Move playback here"
                            color: "#888888"
                            font.pixelSize: 12
                        }
                    }
                    ActionButton {
                        text: root.st.local ? "Selected" : "Listen here"
                        selected: root.st.local || false
                        onClicked: backend.action("local")
                    }
                }
            }
            RowLayout {
                Text {
                    text: "On your network"
                    color: "white"
                    font.pixelSize: 20
                    font.weight: Font.DemiBold
                    Layout.fillWidth: true
                }
                ActionButton {
                    text: "Refresh"
                    onClicked: backend.action("discover")
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: root.st.devices || []
                clip: true
                spacing: 10
                delegate: Rectangle {
                    required property var modelData
                    width: ListView.view.width
                    height: 85
                    radius: 14
                    color: "#121212"
                    RowLayout {
                        anchors.fill: parent
                        anchors.margins: 18
                        Text {
                            text: "▯"
                            color: "#bbbbbb"
                            font.pixelSize: 32
                        }
                        ColumnLayout {
                            Layout.fillWidth: true
                            Text {
                                text: modelData.name
                                color: "white"
                                font.pixelSize: 16
                            }
                            Text {
                                text: modelData.host
                                color: "#777777"
                                font.pixelSize: 12
                            }
                        }
                        ActionButton {
                            text: "Listen here"
                            onClicked: backend.action("connect", {
                                    "id": modelData.id
                                })
                        }
                    }
                }
                Label {
                    visible: parent.count === 0
                    anchors.centerIn: parent
                    text: "Open Velune on another device to see it here."
                    color: "#777777"
                }
            }
            Flow {
                Layout.fillWidth: true
                spacing: 8
                visible: (root.st.controllers || []).length > 0
                Repeater {
                    model: root.st.controllers || []
                    delegate: Label {
                        required property var modelData
                        text: modelData.name + (modelData.renderer ? " · Playing" : " · Connected")
                        padding: 10
                        color: "#aaaaaa"
                        background: Rectangle {
                            color: "#181818"
                            radius: 8
                        }
                    }
                }
            }
            ActionButton {
                visible: root.st.hasTarget || false
                text: "Disconnect session"
                onClicked: backend.action("disconnect")
            }
        }
    }
    Component {
        id: downloadsPage
        ColumnLayout {
            spacing: 14
            Text {
                text: "Downloads"
                color: "white"
                font.pixelSize: 36
                font.weight: Font.Bold
                Layout.topMargin: 15
            }
            Text {
                text: "Choose or change your downloads folder in Settings → Storage."
                color: "#aaaaaa"
                wrapMode: Text.WrapAnywhere
                Layout.fillWidth: true
            }
            Text {
                text: "Completed songs are ready to play offline."
                color: "#777777"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            RowLayout {
                ActionButton {
                    text: "Storage settings"
                    onClicked: { root.settingsCategory = "Storage"; root.navigate("Settings"); }
                }
                ActionButton {
                    text: "Scan folder"
                    enabled: !!root.downloads.folder && !root.downloads.scanning && !root.downloads.busy
                    onClicked: backend.action("downloadScan")
                }
                BusyIndicator {
                    running: root.downloads.scanning || false
                    visible: running
                    implicitWidth: 28
                    implicitHeight: 28
                }
                Text {
                    text: root.downloads.scanning ? "Scanning… " + (root.downloads.scanned || 0) + " files" : (root.downloads.items || []).length + " songs · " + ((root.downloads.bytes || 0) / 1048576).toFixed(1) + " MB"
                    color: "#999999"
                }
            }
            Text {
                text: root.downloads.error || ""
                visible: text.length > 0
                color: "#efab98"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ProgressBar {
                visible: root.downloads.busy || false
                value: root.downloads.progress || 0
                Layout.fillWidth: true
            }
            ListView {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.min(180, count * 48)
                visible: count > 0
                clip: true
                model: root.downloads.jobs || []
                delegate: RowLayout {
                    required property var modelData
                    width: ListView.view.width
                    height: 48
                    Text {
                        text: modelData.title || modelData.id
                        color: "white"
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text { text: modelData.status; color: "#999999" }
                    ActionButton {
                        text: "Retry"
                        visible: modelData.status.indexOf("Failed") === 0
                        onClicked: root.downloadItems([modelData])
                    }
                    ActionButton {
                        text: modelData.status.indexOf("Failed") === 0 ? "Dismiss" : "Cancel"
                        onClicked: backend.action("downloadCancel", {"id": modelData.id})
                    }
                }
                ScrollBar.vertical: ScrollBar {}
            }
            Text {
                text: "Use a song’s menu to download it, or choose Download all inside a collection."
                visible: (root.downloads.items || []).length === 0 && !root.downloads.scanning
                color: "#777777"
                wrapMode: Text.WordWrap
                Layout.fillWidth: true
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.downloads.items || []
                delegate: TrackRow {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    entry: modelData
                    number: index
                    onActivated: root.playItems(root.downloads.items, index)
                    onMenuRequested: root.menuItem(modelData)
                }
                ScrollBar.vertical: ScrollBar {}
            }
        }
    }
    Component {
        id: settingsPage
        SettingsPage {
            appState: root.st
            category: root.settingsCategory
            onChooseDownloadFolder: downloadFolder.open()
            onImportAudio: importDialog.open()
            onOpenDownloads: root.navigate("Downloads")
            onOpenDevices: root.navigate("Available devices")
            onExportBackup: backupSave.open()
            onImportBackup: backupOpen.open()
            onToggleFullScreen: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen()
        }
    }
    Rectangle {
        id: player
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        height: 128
        radius: 24
        color: "#111111"
        Rectangle {
            anchors.bottom: parent.bottom
            width: parent.width
            height: 28
            color: parent.color
        }
        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 28
            anchors.rightMargin: 28
            spacing: 20
            RowLayout {
                Layout.preferredWidth: Math.max(270, root.width * .27)
                spacing: 15
                Artwork {
                    source: root.song.artwork || ""
                    visible: !(root.st.preferences || {}).hidePlayerArtwork
                    Layout.preferredWidth: 76
                    Layout.preferredHeight: 76
                    radius: 13
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 7
                    Text {
                        text: root.song.title || "No song playing"
                        color: "white"
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    Text {
                        text: root.song.artist || "Choose something to listen to"
                        color: "#828282"
                        font.pixelSize: 12
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                ActionButton {
                    text: root.song.liked ? "♥" : "♡"
                    enabled: !!root.song.id
                    hint: "Like song"
                    onClicked: backend.action("like", {
                            "item": root.song
                        })
                }
            }
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2
                RowLayout {
                    Layout.alignment: Qt.AlignHCenter
                    spacing: 15
                    ActionButton {
                        text: "⤨"
                        selected: root.pb.shuffle || false
                        hint: "Shuffle"
                        onClicked: backend.action("command", {
                                "name": "shuffle",
                                "enabled": !root.pb.shuffle
                            })
                    }
                    ActionButton {
                        text: "◀│"
                        hint: "Previous"
                        onClicked: backend.action("previous")
                    }
                    ActionButton {
                        text: root.pb.playing ? "Ⅱ" : "▶"
                        hint: "Play or pause"
                        primary: true
                        radius: 28
                        implicitWidth: 55
                        implicitHeight: 55
                        font.pixelSize: 21
                        onClicked: backend.action(root.pb.playing ? "pause" : "resume")
                    }
                    ActionButton {
                        text: "│▶"
                        hint: "Next"
                        onClicked: backend.action("next")
                    }
                    ActionButton {
                        text: root.pb.repeat === 1 ? "↻¹" : "↻"
                        selected: (root.pb.repeat || 0) !== 0
                        hint: "Repeat"
                        onClicked: backend.action("command", {
                                "name": "repeat",
                                "mode": ((root.pb.repeat || 0) + 1) % 3
                            })
                    }
                }
                RowLayout {
                    spacing: 10
                    Layout.fillWidth: true
                    Text {
                        text: root.time(root.pb.position)
                        color: "#777777"
                        font.pixelSize: 10
                    }
                    Slider {
                        id: seek
                        Layout.fillWidth: true
                        from: 0
                        to: Math.max(1, root.pb.duration || 0)
                        value: root.pb.position || 0
                        onMoved: backend.action("seek", {
                                "position": value
                            })
                        implicitHeight: 22
                        background: Rectangle {
                            x: seek.leftPadding
                            y: seek.topPadding + seek.availableHeight / 2 - height / 2
                            width: seek.availableWidth
                            height: 3
                            radius: 2
                            color: "#363636"
                            Rectangle {
                                width: seek.visualPosition * parent.width
                                height: parent.height
                                radius: 2
                                color: "#dddddd"
                            }
                        }
                        handle: Rectangle {
                            x: seek.leftPadding + seek.visualPosition * (seek.availableWidth - width)
                            y: seek.topPadding + seek.availableHeight / 2 - height / 2
                            width: 10
                            height: 10
                            radius: 5
                            color: "white"
                            visible: seek.hovered || seek.pressed
                        }
                    }
                    Text {
                        text: root.time(root.pb.duration)
                        color: "#777777"
                        font.pixelSize: 10
                    }
                }
            }
            RowLayout {
                spacing: 1
                Layout.preferredWidth: root.width < 1250 ? 205 : 265
                Layout.alignment: Qt.AlignRight
                ActionButton {
                    text: "≋"
                    hint: "Lyrics"
                    onClicked: {
                        backend.action("lyrics");
                        lyrics.open();
                    }
                }
                ActionButton {
                    text: "☷"
                    hint: "Queue"
                    onClicked: queueDialog.open()
                }
                ActionButton {
                    text: "▣"
                    hint: "Available devices"
                    onClicked: root.navigate("Available devices")
                }
                ActionButton {
                    text: volume.value === 0 ? "◌" : "◖"
                    hint: "Mute"
                    onClicked: backend.action("volume", {
                            "value": volume.value === 0 ? 75 : 0
                        })
                }
                Slider {
                    id: volume
                    Layout.fillWidth: true
                    from: 0
                    to: 100
                    value: (root.pb.deviceVolume || {}).level === undefined ? 75 : root.pb.deviceVolume.level
                    onMoved: backend.action("volume", {
                            "value": Math.round(value)
                        })
                }
            }
        }
    }
    Rectangle {
        visible: root.toast.length > 0
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: player.top
        anchors.bottomMargin: 18
        width: Math.min(root.width - 70, toastText.implicitWidth + 40)
        height: toastText.implicitHeight + 28
        color: "#292929"
        radius: 12
        z: 50
        Text {
            id: toastText
            anchors.centerIn: parent
            width: parent.width - 36
            text: root.toast
            wrapMode: Text.WordWrap
            color: "white"
            font.pixelSize: 13
            horizontalAlignment: Text.AlignHCenter
        }
    }
    FileDialog {
        id: backupSave
        title: "Save Velune backup"
        fileMode: FileDialog.SaveFile
        defaultSuffix: "velune.json"
        nameFilters: ["Velune backup (*.velune.json)"]
        onAccepted: backend.action("backupExport", {"url": selectedFile.toString()})
    }
    FileDialog {
        id: backupOpen
        title: "Open Velune backup"
        fileMode: FileDialog.OpenFile
        nameFilters: ["Velune backup (*.velune.json *.json)"]
        onAccepted: backend.action("backupInspect", {"url": selectedFile.toString()})
    }
    Dialog {
        id: restoreConfirmation
        anchors.centerIn: parent
        width: Math.min(500, root.width - 80)
        title: "Restore this backup?"
        modal: true
        standardButtons: Dialog.Cancel | Dialog.Ok
        Label {
            width: parent.width
            wrapMode: Text.WordWrap
            text: "Backup: " + ((root.st.restorePreview || {}).createdAt || "Unknown date") + "\n" +
                ((root.st.restorePreview || {}).likes || 0) + " liked songs · " + ((root.st.restorePreview || {}).playlists || 0) + " playlists\n\n" +
                "Saved library and settings will be replaced, and Velune will restart. Music files and sign-in sessions are kept. A recovery snapshot of the current data is saved first."
        }
        onAccepted: backend.action("backupRestore")
    }
    FolderDialog {
        id: downloadFolder
        objectName: "downloadFolderDialog"
        // Keep native dialogs enabled; the desktop platform theme provides the picker.
        title: "Choose where to save your downloads"
        onAccepted: root.saveDownloadFolder(selectedFolder.toString())
        onRejected: root.pendingDownloads = []
    }
    FileDialog {
        id: importDialog
        title: "Import local audio"
        fileMode: FileDialog.OpenFiles
        nameFilters: ["Audio (*.mp3 *.m4a *.flac *.ogg *.wav *.opus *.aac)"]
        onAccepted: {
            let paths = [];
            for (let i = 0; i < selectedFiles.length; i++)
                paths.push(selectedFiles[i].toString());
            backend.action("import", {
                    "urls": paths
                });
        }
    }
    Menu {
        id: trackMenu
        MenuItem {
            text: "Play"
            onTriggered: root.openItem(root.selectedItem)
        }
        MenuItem {
            text: root.selectedItem.liked ? "Remove like" : "Like"
            visible: !root.selectedItem.kind || root.selectedItem.kind === "song"
            onTriggered: backend.action("like", {
                    "item": root.selectedItem
                })
        }
        MenuItem {
            text: "Play next"
            onTriggered: backend.action("command", {
                    "name": "playNext",
                    "queue": [root.selectedItem]
                })
        }
        MenuItem {
            text: "Add to queue"
            onTriggered: backend.action("command", {
                    "name": "append",
                    "queue": [root.selectedItem]
                })
        }
        MenuItem {
            text: root.selectedItem.downloaded ? "Downloaded" : "Download MP3"
            visible: !root.selectedItem.kind || root.selectedItem.kind === "song"
            enabled: !root.selectedItem.downloaded
            onTriggered: root.downloadItems([root.selectedItem])
        }
        MenuItem {
            text: "Start radio"
            onTriggered: backend.action("radio", {
                    "item": root.selectedItem
                })
        }
        MenuItem {
            text: "Add to playlist…"
            onTriggered: addPlaylist.open()
        }
        MenuItem {
            text: "Bookmark collection"
            visible: !!root.selectedItem.kind && root.selectedItem.kind !== "song"
            onTriggered: backend.action("bookmark", {
                    "item": root.selectedItem
                })
        }
        MenuItem {
            text: "Rename playlist…"
            visible: root.selectedItem.kind === "playlist"
            onTriggered: {
                playlistTitle.text = root.selectedItem.title || "";
                playlistEditor.open();
            }
        }
        MenuItem {
            text: "Edit tags…"
            visible: root.selectedItem.kind === "playlist"
            onTriggered: {
                tagText.text = (root.selectedItem.tags || []).join(", ");
                tagEditor.open();
            }
        }
        MenuItem {
            text: "Delete playlist"
            visible: root.selectedItem.kind === "playlist"
            onTriggered: backend.action("playlistDelete", {
                    "id": root.selectedItem.id
                })
        }
        MenuItem {
            text: "Remove from queue"
            visible: root.selectedQueue >= 0
            onTriggered: backend.action("command", {
                    "name": "remove",
                    "from": root.selectedQueue,
                    "to": root.selectedQueue + 1
                })
        }
        MenuItem {
            text: "Move up in queue"
            visible: root.selectedQueue > 0
            onTriggered: backend.action("command", {
                    "name": "move",
                    "from": root.selectedQueue,
                    "to": root.selectedQueue - 1
                })
        }
        MenuItem {
            text: "Move down in queue"
            visible: root.selectedQueue >= 0
            onTriggered: backend.action("command", {
                    "name": "move",
                    "from": root.selectedQueue,
                    "to": root.selectedQueue + 1
                })
        }
    }
    Dialog {
        id: collection
        anchors.centerIn: parent
        width: Math.min(850, root.width - 100)
        height: Math.min(650, root.height - 100)
        title: root.st.collectionTitle || "Collection"
        modal: true
        standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent
            RowLayout {
                ActionButton {
                    text: "Play all"
                    primary: true
                    enabled: root.collectionItems.length > 0
                    onClicked: root.playItems(root.collectionItems, 0)
                }
                ActionButton {
                    text: "Download all"
                    enabled: root.collectionItems.length > 0 && !root.st.collectionBusy
                    onClicked: root.downloadItems(root.collectionItems)
                }
                BusyIndicator {
                    running: root.st.collectionBusy || false
                    implicitWidth: 30
                    implicitHeight: 30
                }
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.collectionItems
                delegate: TrackRow {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    entry: modelData
                    number: index
                    onActivated: root.playItems(root.collectionItems, index)
                    onMenuRequested: root.menuItem(modelData)
                }
                ScrollBar.vertical: ScrollBar {
                }
            }
        }
    }
    Dialog {
        id: queueDialog
        anchors.centerIn: parent
        width: Math.min(850, root.width - 100)
        height: Math.min(650, root.height - 100)
        title: "Queue"
        modal: true
        standardButtons: Dialog.Close
        ColumnLayout {
            anchors.fill: parent
            ActionButton {
                text: "Clear queue"
                onClicked: backend.action("command", {
                        "name": "clear"
                    })
            }
            ListView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                clip: true
                model: root.pb.queue || []
                delegate: TrackRow {
                    required property var modelData
                    required property int index
                    width: ListView.view.width
                    entry: modelData
                    number: index
                    current: index === root.pb.index
                    onActivated: backend.action("command", {
                            "name": "seek",
                            "index": index,
                            "position": 0
                        })
                    onMenuRequested: root.menuItem(modelData, index)
                }
                ScrollBar.vertical: ScrollBar {
                }
            }
        }
    }
    Dialog {
        id: lyrics
        anchors.centerIn: parent
        width: Math.min(700, root.width - 100)
        height: Math.min(650, root.height - 100)
        title: "Lyrics"
        modal: true
        standardButtons: Dialog.Close
        ScrollView {
            id: lyricsScroll
            anchors.fill: parent
            contentWidth: availableWidth
            Text {
                width: lyricsScroll.availableWidth
                text: root.st.lyrics || "No lyrics are available for this song yet."
                font.pixelSize: (root.st.preferences || {}).lyricsFontSize || 18
                lineHeight: (root.st.preferences || {}).lyricsLineHeight || 1.4
                wrapMode: Text.WordWrap
                textFormat: Text.PlainText
                color: "#dddddd"
            }
        }
    }
    Dialog {
        id: playlistEditor
        anchors.centerIn: parent
        width: 440
        title: root.selectedItem.id ? "Rename playlist" : "New playlist"
        modal: true
        standardButtons: Dialog.Save | Dialog.Cancel
        TextField {
            id: playlistTitle
            width: parent.width
            placeholderText: "Playlist name"
            maximumLength: 200
        }
        onAccepted: if (playlistTitle.text.trim())
            backend.action("playlistSave", {
                    "id": root.selectedItem.id || "",
                    "title": playlistTitle.text,
                    "items": root.selectedItem.localTracks || root.selectedItem.songs || []
                })
    }
    Dialog {
        id: tagEditor
        anchors.centerIn: parent
        width: 440
        title: "Playlist tags"
        modal: true
        standardButtons: Dialog.Save | Dialog.Cancel
        TextField {
            id: tagText
            width: parent.width
            placeholderText: "Comma-separated tags"
        }
        onAccepted: backend.action("tags", {
                "id": root.selectedItem.id,
                "tags": tagText.text.split(",").map(function (t) {
                        return t.trim();
                    }).filter(function (t) {
                        return t.length > 0;
                    })
            })
    }
    Dialog {
        id: addPlaylist
        anchors.centerIn: parent
        width: 440
        height: 400
        title: "Add to playlist"
        modal: true
        standardButtons: Dialog.Close
        ListView {
            anchors.fill: parent
            model: root.st.playlists || []
            delegate: ActionButton {
                required property var modelData
                width: ListView.view.width
                text: modelData.title || "Playlist"
                onClicked: {
                    backend.action("playlistAdd", {
                            "id": modelData.id,
                            "item": root.selectedItem
                        });
                    addPlaylist.close();
                }
            }
        }
    }
}
