import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: banner
    property alias text: label.text
    property bool warning: false
    signal dismissed()

    implicitHeight: label.implicitHeight + 16
    color: warning ? "#3a1c1f" : "#1c2a22"

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 16
        anchors.rightMargin: 8
        Label {
            id: label
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            color: banner.warning ? "#f0b4b4" : "#b4e0c4"
            font.pixelSize: 13
        }
        ToolButton {
            text: "✕"
            implicitHeight: 28
            onClicked: banner.dismissed()
        }
    }
}
