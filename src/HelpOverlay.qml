import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: help
    anchors.centerIn: Overlay.overlay
    width: Math.min(560, win.width - 40)
    modal: true
    padding: 24
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#1a1a1f"; radius: theme.radius; border.color: win.line }
    Overlay.modal: Rectangle { color: "#99000000" }

    readonly property var keys: [
        ["Ctrl+O", qsTr("Open a file")],
        ["/", qsTr("Search IDs or a motif")],
        ["N  /  Shift+N", qsTr("Next and previous match")],
        ["J K  ↓ ↑", qsTr("Next and previous record")],
        ["Home  End", qsTr("First and last record")],
        ["PgDn  PgUp", qsTr("Scroll the sequence")],
        ["Space", qsTr("Switch between record and summary")],
        ["R", qsTr("Reverse complement")],
        ["T", qsTr("Translate in six frames")],
        ["O", qsTr("Find open reading frames")],
        ["W", qsTr("Wrap at 60 or 80 columns")],
        ["Ctrl+C", qsTr("Copy the record")],
        ["Ctrl+E", qsTr("Export")],
        ["Esc", qsTr("Clear the search")],
        ["?", qsTr("Show this overlay")],
        ["Q", qsTr("Quit")]
    ]

    contentItem: ColumnLayout {
        spacing: 12
        Label {
            text: qsTr("Keyboard shortcuts")
            font.pixelSize: 18
            font.bold: true
            color: win.textColor
        }
        GridLayout {
            columns: 2
            columnSpacing: 24
            rowSpacing: 6
            Repeater {
                model: help.keys.length * 2
                delegate: Label {
                    required property int index
                    readonly property var entry: help.keys[Math.floor(index / 2)]
                    text: index % 2 === 0 ? entry[0] : entry[1]
                    font.family: index % 2 === 0 ? win.mono : Qt.application.font.family
                    color: index % 2 === 0 ? theme.accent : win.textColor
                    Layout.fillWidth: index % 2 === 1
                    wrapMode: Text.Wrap
                }
            }
        }
    }
}
