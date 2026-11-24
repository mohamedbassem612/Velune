import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: row
    property var entry: ({})
    property int number: 0
    property bool current: false
    signal activated
    signal menuRequested
    implicitHeight: 70
    radius: 10
    color: hover.hovered ? "#1b1b1b" : current ? "#151515" : "transparent"
    Behavior on color  {
        ColorAnimation {
            duration: 120
        }
    }
    HoverHandler {
        id: hover
    }
    TapHandler {
        onTapped: row.activated()
    }
    TapHandler {
        acceptedButtons: Qt.RightButton
        onTapped: row.menuRequested()
    }
    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        spacing: 14
        Text {
            text: hover.hovered ? "▶" : (row.number + 1).toString()
            color: row.current ? "white" : "#777777"
            font.pixelSize: 13
            Layout.preferredWidth: 22
        }
        Artwork {
            source: row.entry.artwork || backend.artwork(row.entry)
            Layout.preferredWidth: 48
            Layout.preferredHeight: 48
            radius: 8
        }
        ColumnLayout {
            Layout.fillWidth: true
            spacing: 5
            Text {
                text: row.entry.title || row.entry.name || "Untitled"
                color: "white"
                font.pixelSize: 14
                font.weight: Font.DemiBold
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
            Text {
                text: row.entry.artist || row.entry.subtitle || (row.entry.artists || []).map(function (a) {
                        return typeof a === "string" ? a : a.name || "";
                    }).join(", ") || row.entry.kind || ""
                color: "#868686"
                font.pixelSize: 12
                Layout.fillWidth: true
                elide: Text.ElideRight
            }
        }
        Text {
            text: "↓"
            visible: row.entry.downloaded === true
            color: "#94cba8"
            font.pixelSize: 17
        }
        Text {
            text: row.entry.duration ? Math.floor(row.entry.duration / 60) + ":" + ("0" + Math.floor(row.entry.duration % 60)).slice(-2) : ""
            color: "#858585"
            font.pixelSize: 12
        }
        ActionButton {
            text: row.entry.liked ? "♥" : "♡"
            hint: "Like"
            onClicked: backend.action("like", {
                    "item": row.entry
                })
            visible: !row.entry.kind || row.entry.kind === "song"
        }
        ActionButton {
            text: "···"
            hint: "More"
            onClicked: row.menuRequested()
        }
    }
}
