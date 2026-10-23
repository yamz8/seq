import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Seq

Item {
    id: pane
    readonly property var record: backend.current
    readonly property bool hasRecord: record.id !== undefined

    function page(direction) {
        const view = win.detailTab === 1 ? translationView : win.detailTab === 2 ? orfView : sequenceView
        view.contentY = Math.max(0, Math.min(view.contentHeight - view.height,
                                             view.contentY + direction * view.height * 0.9))
    }

    Connections {
        target: backend
        function onJumpToLine(row) { sequenceView.positionViewAtIndex(row, ListView.Center) }
    }

    Label {
        anchors.centerIn: parent
        visible: !pane.hasRecord
        text: backend.searchActive ? qsTr("Nothing matches “%1”").arg(backend.searchText) : qsTr("No record selected")
        color: win.dim
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 18
        spacing: 10
        visible: pane.hasRecord

        Label {
            Layout.fillWidth: true
            text: pane.record.id || ""
            font.pixelSize: 18
            font.bold: true
            font.family: win.mono
            color: win.textColor
            elide: Text.ElideRight
        }
        Label {
            Layout.fillWidth: true
            visible: text !== ""
            text: pane.record.description || ""
            color: win.dim
            font.pixelSize: 13
            elide: Text.ElideRight
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Chip { text: qsTr("Record %1 of %2").arg(win.num(pane.record.number || 0)).arg(win.num(backend.totalRecords)) }
            Chip { text: win.num(pane.record.length || 0) + " " + win.unit() }
            Chip { visible: !backend.protein; text: qsTr("GC %1%").arg(win.num(pane.record.gc || 0, 1)) }
            Chip { visible: backend.protein; text: qsTr("%1 kDa").arg(win.num((pane.record.weight || 0) / 1000, 2)) }
            Chip { visible: backend.fastq; text: qsTr("Mean Q %1").arg(win.num(pane.record.quality || 0, 1)) }
            Chip { visible: backend.reversed; text: qsTr("Reverse complement"); accent: true }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 0
            TabBar {
                id: tabs
                Layout.fillWidth: true
                currentIndex: win.detailTab
                onCurrentIndexChanged: if (currentIndex !== win.detailTab) win.showTab(currentIndex)
                background: Item {}
                TabButton { text: qsTr("Sequence"); width: implicitWidth }
                TabButton { text: qsTr("Translation"); width: implicitWidth; visible: !backend.protein; enabled: visible }
                TabButton { text: qsTr("ORFs"); width: implicitWidth; visible: !backend.protein; enabled: visible }
            }
            ToolButton {
                visible: !backend.protein && win.detailTab === 0
                text: qsTr("Reverse complement")
                checkable: true
                checked: backend.reversed
                onClicked: backend.toggleReverse()
                ToolTip.visible: hovered
                ToolTip.text: "R"
            }
            ToolButton {
                text: qsTr("%1 columns").arg(backend.lineWidth)
                onClicked: backend.toggleLineWidth()
                ToolTip.visible: hovered
                ToolTip.text: "W"
            }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: win.detailTab

            // Sequence, with the per-base quality under it for reads. Bases
            // read left to right in every language, so this never mirrors.
            ColumnLayout {
                spacing: 8
                LayoutMirroring.enabled: false
                LayoutMirroring.childrenInherit: true
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: win.panel
                    radius: theme.radius
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 0
                        RowLayout {
                            spacing: 10
                            Item { Layout.preferredWidth: positionWidth.width }
                            SequenceLine { ruler: true; columns: backend.lineWidth; pixelSize: 13 }
                        }
                        ListView {
                            id: sequenceView
                            visible: !backend.recordLoading
                            Layout.fillWidth: true
                            Layout.fillHeight: true
                            clip: true
                            model: backend.sequenceLines
                            boundsBehavior: Flickable.StopAtBounds
                            reuseItems: true
                            ScrollBar.vertical: ScrollBar {}
                            delegate: RowLayout {
                                required property var start
                                required property string bases
                                required property var highlights
                                spacing: 10
                                Label {
                                    Layout.preferredWidth: positionWidth.width
                                    horizontalAlignment: Text.AlignRight
                                    text: start
                                    font.family: win.mono
                                    font.pixelSize: 12
                                    color: "#5e5e64"
                                }
                                SequenceLine {
                                    text: bases
                                    highlights: parent.highlights
                                    protein: backend.protein
                                    columns: backend.lineWidth
                                    accent: theme.accent
                                    pixelSize: 13
                                }
                            }
                        }
                    }
                    BusyLabel { visible: backend.recordLoading }
                    // Wide enough for the largest position in the record.
                    TextMetrics {
                        id: positionWidth
                        font.family: win.mono
                        font.pixelSize: 12
                        text: String(pane.record.length || 0)
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 150
                    visible: backend.fastq
                    color: win.panel
                    radius: theme.radius
                    BarChart {
                        anchors.fill: parent
                        anchors.margins: 10
                        values: backend.currentQualities
                        xMin: 1
                        xMax: pane.record.length || 0
                        title: qsTr("Phred quality per base")
                        xLabel: qsTr("Position")
                        color: theme.accent
                        phred: true
                    }
                }
            }

            // Six frames, standard genetic code.
            Rectangle {
                color: win.panel
                LayoutMirroring.enabled: false
                LayoutMirroring.childrenInherit: true
                radius: theme.radius
                ListView {
                    id: translationView
                    anchors.fill: parent
                    anchors.margins: 10
                    clip: true
                    model: backend.translationLines
                    boundsBehavior: Flickable.StopAtBounds
                    ScrollBar.vertical: ScrollBar {}
                    BusyLabel { visible: backend.recordLoading || backend.translating }
                    delegate: RowLayout {
                        required property string label
                        required property var start
                        required property string bases
                        required property bool first
                        spacing: 10
                        height: first ? 38 : 22
                        Label {
                            Layout.preferredWidth: 34
                            Layout.alignment: Qt.AlignBottom
                            text: first ? label : ""
                            font.bold: true
                            color: theme.accent
                        }
                        Label {
                            Layout.preferredWidth: positionWidth.width
                            Layout.alignment: Qt.AlignBottom
                            horizontalAlignment: Text.AlignRight
                            text: start
                            font.family: win.mono
                            font.pixelSize: 12
                            color: "#5e5e64"
                        }
                        SequenceLine {
                            Layout.alignment: Qt.AlignBottom
                            text: bases
                            protein: true
                            columns: backend.lineWidth
                            pixelSize: 13
                        }
                    }
                }
            }

            // Open reading frames, longest first.
            ColumnLayout {
                spacing: 8
                RowLayout {
                    spacing: 10
                    Label { text: qsTr("Minimum length"); color: win.dim }
                    SpinBox {
                        from: 30
                        to: 100000
                        stepSize: 30
                        editable: true
                        value: backend.orfMinLength
                        onValueModified: backend.orfMinLength = value
                        Material.roundedScale: theme.radius
                    }
                    Label { text: qsTr("bp"); color: win.dim }
                    Item { Layout.fillWidth: true }
                    Label {
                        text: qsTr("%n ORFs", "", backend.orfCount)
                        color: win.textColor
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    color: win.panel
                    radius: theme.radius
                    ListView {
                        id: orfView
                        anchors.fill: parent
                        anchors.margins: 6
                        clip: true
                        model: backend.orfs
                        boundsBehavior: Flickable.StopAtBounds
                        ScrollBar.vertical: ScrollBar {}
                        delegate: Rectangle {
                            required property int index
                            required property var modelData
                            width: orfView.width
                            height: 46
                            color: orfMouse.containsMouse ? "#1d1d22" : "transparent"
                            radius: Math.min(4, theme.radius)
                            ColumnLayout {
                                anchors.fill: parent
                                anchors.leftMargin: 10
                                anchors.rightMargin: 10
                                spacing: 0
                                RowLayout {
                                    spacing: 14
                                    Label { text: modelData.frame; font.bold: true; color: theme.accent; Layout.preferredWidth: 26 }
                                    Label {
                                        text: ("%1–%2").arg(win.num(modelData.start)).arg(win.num(modelData.end))
                                        color: win.textColor
                                        font.family: win.mono
                                    }
                                    Label { text: win.num(modelData.length) + " " + qsTr("bp"); color: win.dim }
                                    Label { text: win.num(modelData.residues) + " " + qsTr("aa"); color: win.dim }
                                }
                                Label {
                                    Layout.fillWidth: true
                                    text: modelData.protein
                                    font.family: win.mono
                                    font.pixelSize: 11
                                    color: "#6e6e74"
                                    elide: Text.ElideRight
                                }
                            }
                            MouseArea {
                                id: orfMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                onClicked: {
                                    win.detailTab = 0
                                    backend.showOrf(index)
                                }
                            }
                        }
                        BusyLabel { visible: backend.recordLoading || backend.findingOrfs }
                        Label {
                            anchors.centerIn: parent
                            visible: orfView.count === 0 && !backend.recordLoading && !backend.findingOrfs
                            text: qsTr("No ORFs of at least %1 bp").arg(win.num(backend.orfMinLength))
                            color: win.dim
                        }
                    }
                }
            }
        }
    }

    // A chromosome takes a moment to read, translate or search for ORFs.
    component BusyLabel: Label {
        anchors.centerIn: parent
        text: qsTr("Loading…")
        color: win.dim
        font.pixelSize: 14
    }

    component Chip: Rectangle {
        property alias text: chipLabel.text
        property bool accent: false
        implicitWidth: chipLabel.implicitWidth + 18
        implicitHeight: 24
        radius: Math.min(4, theme.radius)
        color: accent ? Qt.alpha(theme.accent, 0.2) : "#1f1f24"
        Label {
            id: chipLabel
            anchors.centerIn: parent
            font.pixelSize: 12
            color: parent.accent ? theme.accent : "#cfcfd4"
        }
    }
}
