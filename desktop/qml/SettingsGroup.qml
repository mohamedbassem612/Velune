import QtQuick
import QtQuick.Layouts

Rectangle {
    id: group
    property string title: ""
    property string description: ""
    default property alias content: body.data
    Layout.fillWidth: true
    implicitHeight: body.implicitHeight + 36
    color: "#111111"
    border.color: "#252525"
    radius: 16
    ColumnLayout {
        id: body
        x: 20
        y: 18
        width: parent.width - 40
        spacing: 14
        Text {
            text: group.title
            color: "white"
            font.pixelSize: 20
            font.weight: Font.DemiBold
            Layout.fillWidth: true
        }
        Text {
            text: group.description
            visible: text.length > 0
            color: "#858585"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
            font.pixelSize: 12
        }
    }
}
