import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

RowLayout {
    id: row
    property string text: ""
    property var choices: []
    property var values: choices
    property var value
    signal selected(var value)
    Layout.fillWidth: true
    spacing: 24
    Text {
        text: row.text
        color: "#eeeeee"
        wrapMode: Text.WordWrap
        Layout.fillWidth: true
    }
    ComboBox {
        model: row.choices
        Layout.preferredWidth: 205
        currentIndex: Math.max(0, row.values.indexOf(row.value))
        Accessible.name: row.text
        onActivated: row.selected(row.values[currentIndex])
    }
}
