import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: row
    property string text: ""
    property string description: ""
    property bool checked: false
    signal toggled(bool value)
    Layout.fillWidth: true
    spacing: 24
    ColumnLayout {
        Layout.fillWidth: true
        spacing: 4
        Text {
            text: row.text
            color: row.enabled ? "#eeeeee" : "#777777"
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
        Text {
            text: row.description
            visible: text.length > 0
            color: "#828282"
            font.pixelSize: 12
            wrapMode: Text.WordWrap
            Layout.fillWidth: true
        }
    }
    Switch {
        checked: row.checked
        Accessible.name: row.text
        onClicked: row.toggled(checked)
    }
}
