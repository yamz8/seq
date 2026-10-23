import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: bar
    implicitHeight: 60
    color: win.panel
    property alias searchField: field

    function run() {
        backend.search(field.text, mode.currentIndex === 1, strands.checked)
        root.forceActiveFocus()
    }

    Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 14
        anchors.rightMargin: 14
        spacing: 10

        Button {
            text: qsTr("Open")
            flat: true
            Material.roundedScale: theme.radius
            onClicked: backend.open()
        }

        ColumnLayout {
            spacing: 0
            Layout.fillWidth: true
            Layout.minimumWidth: 80
            Label {
                Layout.fillWidth: true
                text: backend.loaded ? backend.fileName : qsTr("No file open")
                font.pixelSize: 15
                font.bold: true
                elide: Text.ElideMiddle
                color: win.textColor
            }
            Label {
                Layout.fillWidth: true
                visible: backend.loaded
                text: [backend.format, backend.protein ? qsTr("protein") : qsTr("nucleotide"),
                       qsTr("%n records", "", backend.totalRecords)].concat(backend.compressed ? [qsTr("gzipped")] : []).join(" · ")
                font.pixelSize: 12
                color: win.dim
                elide: Text.ElideRight
            }
        }

        ComboBox {
            id: mode
            objectName: "searchMode"
            visible: backend.loaded
            Layout.preferredWidth: 120
            model: [qsTr("ID"), qsTr("Motif")]
            Material.roundedScale: theme.radius
            focusPolicy: Qt.NoFocus
        }

        TextField {
            id: field
            visible: backend.loaded
            Layout.preferredWidth: Math.min(320, bar.width * 0.28)
            placeholderText: mode.currentIndex === 0 ? qsTr("Search IDs  ( / )") : qsTr("Motif, e.g. GAATTC or TATAWAW")
            font.family: mode.currentIndex === 1 ? win.mono : Qt.application.font.family
            Material.roundedScale: theme.radius
            enabled: !backend.busy
            onAccepted: bar.run()
            Keys.onEscapePressed: {
                if (text === "")
                    backend.clearSearch()
                root.forceActiveFocus()
            }
        }

        CheckBox {
            id: strands
            visible: backend.loaded && mode.currentIndex === 1 && !backend.protein
            text: qsTr("Both strands")
            checked: true
            focusPolicy: Qt.NoFocus
        }

        Label {
            visible: backend.searchActive
            text: backend.matchCount === 0 ? qsTr("No matches")
                  : qsTr("%1 of %2").arg(win.num(backend.matchIndex + 1)).arg(win.num(backend.matchCount))
                    + (backend.matchesTruncated ? "+" : "")
            color: backend.matchCount === 0 ? "#e69a9a" : win.textColor
            font.pixelSize: 13
        }
        ToolButton {
            visible: backend.searchActive && backend.matchCount > 1
            text: "‹"
            font.pixelSize: 18
            onClicked: backend.previousMatch()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Previous match (Shift+N)")
        }
        ToolButton {
            visible: backend.searchActive && backend.matchCount > 1
            text: "›"
            font.pixelSize: 18
            onClicked: backend.nextMatch()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Next match (N)")
        }
        ToolButton {
            visible: backend.searchActive
            text: "✕"
            onClicked: { field.text = ""; backend.clearSearch() }
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Clear search (Esc)")
        }

        ToolButton {
            visible: backend.loaded
            text: win.showSummary ? qsTr("Record") : qsTr("Summary")
            onClicked: win.showSummary = !win.showSummary
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Toggle summary (Space)")
        }
        ToolButton {
            text: "?"
            font.bold: true
            onClicked: help.open()
            ToolTip.visible: hovered
            ToolTip.text: qsTr("Keyboard shortcuts (?)")
        }
    }
}
