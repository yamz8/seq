import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Popup {
    id: popup
    anchors.centerIn: Overlay.overlay
    width: Math.min(460, win.width - 40)
    modal: true
    padding: 20
    Material.roundedScale: theme.radius
    background: Rectangle { color: "#1a1a1f"; radius: theme.radius; border.color: win.line }
    Overlay.modal: Rectangle { color: "#99000000" }

    readonly property var choices: [
        { kind: "selected", label: qsTr("Selected record"), detail: backend.current.id || "", enabled: backend.currentRow >= 0 },
        { kind: "filtered", label: backend.searchActive ? qsTr("Matching records") : qsTr("All records"),
          detail: qsTr("%n records", "", backend.records.count), enabled: backend.records.count > 0 },
        { kind: "json", label: qsTr("Summary as JSON"), detail: "", enabled: true },
        { kind: "csv", label: qsTr("Summary as CSV"), detail: "", enabled: true },
        { kind: "length-svg", label: qsTr("Length chart as SVG"), detail: "", enabled: true },
        { kind: "quality-svg", label: qsTr("Quality chart as SVG"), detail: "", enabled: backend.fastq }
    ]

    function choose(i) {
        const c = choices[i]
        if (!c || !c.enabled)
            return
        close()
        backend.exportAs(c.kind)
    }

    onOpened: { list.currentIndex = 0; list.forceActiveFocus() }

    contentItem: ColumnLayout {
        spacing: 10
        Label {
            text: qsTr("Export")
            font.pixelSize: 18
            font.bold: true
            color: win.textColor
        }
        Label {
            Layout.fillWidth: true
            text: qsTr("Always to a new file. The open file is never changed.")
            color: win.dim
            font.pixelSize: 12
            wrapMode: Text.Wrap
        }
        ListView {
            id: list
            Layout.fillWidth: true
            implicitHeight: contentHeight
            interactive: false
            model: popup.choices
            keyNavigationEnabled: true
            Keys.onReturnPressed: popup.choose(currentIndex)
            Keys.onEnterPressed: popup.choose(currentIndex)
            Keys.onPressed: event => {
                const n = event.key - Qt.Key_1
                if (n >= 0 && n < popup.choices.length) {
                    popup.choose(n)
                    event.accepted = true
                }
            }
            delegate: Rectangle {
                required property int index
                required property var modelData
                width: list.width
                height: 40
                radius: Math.min(4, theme.radius)
                color: ListView.isCurrentItem ? Qt.alpha(theme.accent, 0.16) : "transparent"
                opacity: modelData.enabled ? 1 : 0.4
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 10
                    spacing: 12
                    Label { text: index + 1; font.family: win.mono; color: theme.accent }
                    Label { text: modelData.label; color: win.textColor; Layout.fillWidth: true; elide: Text.ElideRight }
                    Label { text: modelData.detail; color: win.dim; font.pixelSize: 12; elide: Text.ElideLeft; Layout.maximumWidth: 160 }
                }
                MouseArea {
                    anchors.fill: parent
                    onClicked: popup.choose(index)
                }
            }
        }
    }
}
