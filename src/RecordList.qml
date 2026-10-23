import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    color: win.panel

    // Column widths, shared by the header and every row.
    readonly property int numberWidth: 58
    readonly property int lengthWidth: 76
    readonly property int valueWidth: 64
    readonly property int qualityWidth: backend.fastq ? 54 : 0

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        Item {
            Layout.fillWidth: true
            implicitHeight: 30
            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 10
                anchors.rightMargin: 14
                spacing: 6
                HeaderLabel { Layout.preferredWidth: numberWidth; text: "#"; horizontalAlignment: Text.AlignRight }
                HeaderLabel { Layout.fillWidth: true; text: qsTr("ID") }
                HeaderLabel { Layout.preferredWidth: lengthWidth; text: qsTr("Length"); horizontalAlignment: Text.AlignRight }
                HeaderLabel {
                    Layout.preferredWidth: valueWidth
                    text: backend.protein ? qsTr("kDa") : qsTr("GC %")
                    horizontalAlignment: Text.AlignRight
                }
                HeaderLabel {
                    Layout.preferredWidth: qualityWidth
                    visible: backend.fastq
                    text: qsTr("Q")
                    horizontalAlignment: Text.AlignRight
                }
            }
            Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: win.line }
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: backend.records
            currentIndex: backend.currentRow
            boundsBehavior: Flickable.StopAtBounds
            reuseItems: true
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            Connections {
                target: backend
                function onCurrentChanged() {
                    if (backend.currentRow >= 0)
                        list.positionViewAtIndex(backend.currentRow, ListView.Contain)
                }
            }

            delegate: Rectangle {
                id: row
                required property int index
                required property string recordId
                required property var recordLength
                required property real recordGc
                required property real recordWeight
                required property real recordQuality
                required property var recordNumber
                readonly property bool selected: index === backend.currentRow

                width: list.width
                height: 26
                color: selected ? Qt.alpha(theme.accent, 0.18) : mouse.containsMouse ? "#1d1d22" : "transparent"

                Rectangle {
                    visible: row.selected
                    width: 3
                    height: parent.height
                    color: theme.accent
                }

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 14
                    spacing: 6
                    CellLabel { Layout.preferredWidth: numberWidth; text: win.num(row.recordNumber); color: win.dim; horizontalAlignment: Text.AlignRight }
                    CellLabel {
                        Layout.fillWidth: true
                        text: row.recordId
                        font.family: win.mono
                        elide: Text.ElideRight
                        color: row.selected ? win.textColor : "#cfcfd4"
                    }
                    CellLabel { Layout.preferredWidth: lengthWidth; text: win.num(row.recordLength); horizontalAlignment: Text.AlignRight }
                    CellLabel {
                        Layout.preferredWidth: valueWidth
                        text: backend.protein ? win.num(row.recordWeight / 1000, 1) : win.num(row.recordGc, 1)
                        horizontalAlignment: Text.AlignRight
                    }
                    CellLabel {
                        Layout.preferredWidth: qualityWidth
                        visible: backend.fastq
                        text: row.recordQuality >= 0 ? win.num(row.recordQuality, 1) : ""
                        horizontalAlignment: Text.AlignRight
                        color: row.recordQuality < 20 ? "#e69a9a" : row.recordQuality < 30 ? "#e6cf8a" : "#cfcfd4"
                    }
                }

                MouseArea {
                    id: mouse
                    anchors.fill: parent
                    hoverEnabled: true
                    onClicked: {
                        root.forceActiveFocus()
                        backend.select(row.index)
                    }
                }
            }

            Label {
                anchors.centerIn: parent
                visible: list.count === 0 && backend.searchActive
                text: qsTr("No records match")
                color: win.dim
            }
        }
    }

    component HeaderLabel: Label {
        font.pixelSize: 11
        font.bold: true
        color: win.dim
        elide: Text.ElideRight
    }
    component CellLabel: Label {
        font.pixelSize: 12
        color: "#cfcfd4"
        elide: Text.ElideRight
    }
}
