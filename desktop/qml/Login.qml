import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtWebEngine

ApplicationWindow {
    id: window
    visible: true
    width: 1000
    height: 760
    minimumWidth: 650
    minimumHeight: 500
    title: "Velune — Sign in to YouTube Music"
    color: "#080808"
    palette.window: "#111111"
    palette.windowText: "white"
    palette.buttonText: "white"
    palette.button: "#222222"
    ColumnLayout {
        anchors.fill: parent
        spacing: 0
        Label {
            Layout.fillWidth: true
            padding: 14
            color: "white"
            text: String(browser.url).split("/").slice(0, 3).join("/")
            elide: Text.ElideRight
        }
        WebEngineView {
            id: browser
            Layout.fillWidth: true
            Layout.fillHeight: true
            profile: loginProfile
            url: "https://accounts.google.com/ServiceLogin?continue=https%3A%2F%2Fmusic.youtube.com"
            settings.pluginsEnabled: false
            settings.javascriptCanOpenWindows: false
            onNewWindowRequested: function (request) {}
            onFeaturePermissionRequested: function (origin, feature) {
                grantFeaturePermission(origin, feature, false);
            }
            onCertificateError: function (error) {
                error.rejectCertificate();
            }
        }
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 14
            Label {
                Layout.fillWidth: true
                wrapMode: Text.WordWrap
                color: "#bbbbbb"
                text: "Complete Google sign-in here. This window closes after Velune verifies your YouTube Music account. If Google refuses the embedded browser, your account stays disconnected."
            }
            Button {
                text: "Cancel"
                onClicked: window.close()
            }
        }
    }
}
