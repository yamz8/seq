import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import Seq

Flickable {
    id: summary
    readonly property var s: backend.summary
    contentWidth: width
    contentHeight: content.implicitHeight + 36
    clip: true
    boundsBehavior: Flickable.StopAtBounds
    ScrollBar.vertical: ScrollBar {}

    ColumnLayout {
        id: content
        x: 18
        y: 18
        width: summary.width - 36
        spacing: 14

        Label {
            text: qsTr("Summary")
            font.pixelSize: 18
            font.bold: true
            color: win.textColor
        }

        GridLayout {
            Layout.fillWidth: true
            columns: Math.max(2, Math.floor(content.width / 170))
            columnSpacing: 10
            rowSpacing: 10
            Tile { label: qsTr("Records"); value: win.num(s.records || 0) }
            Tile { label: backend.protein ? qsTr("Total residues") : qsTr("Total bases"); value: win.num(s.totalLength || 0) }
            Tile { label: qsTr("Shortest"); value: win.num(s.minLength || 0) }
            Tile { label: qsTr("Longest"); value: win.num(s.maxLength || 0) }
            Tile { label: qsTr("Mean length"); value: win.num(s.meanLength || 0, 1) }
            Tile { label: "N50"; value: win.num(s.n50 || 0) }
            Tile { visible: !backend.protein; label: qsTr("GC"); value: win.num(s.gcPercent || 0, 2) + "%" }
            Tile { visible: backend.fastq; label: qsTr("Mean quality"); value: win.num(s.meanQuality || 0, 1) }
        }

        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            BarChart {
                anchors.fill: parent
                anchors.margins: 12
                values: s.lengthHistogram || []
                xMin: s.minLength || 0
                xMax: s.maxLength || 0
                title: qsTr("Sequence lengths")
                xLabel: qsTr("Length")
                color: theme.accent
            }
        }

        Panel {
            Layout.fillWidth: true
            Layout.preferredHeight: 240
            visible: backend.fastq
            BarChart {
                anchors.fill: parent
                anchors.margins: 12
                values: s.qualityHistogram || []
                xMin: 0
                xMax: s.qualityMax || 0
                title: qsTr("Reads by mean quality")
                xLabel: qsTr("Mean Phred quality")
                color: theme.accent
                phred: true
            }
        }

        Label {
            text: backend.protein ? qsTr("Amino acid composition") : qsTr("Base composition")
            font.bold: true
            color: win.textColor
        }
        Flow {
            Layout.fillWidth: true
            spacing: 6
            Repeater {
                model: s.composition || []
                delegate: Rectangle {
                    required property var modelData
                    width: 74
                    height: 48
                    radius: Math.min(4, theme.radius)
                    color: win.panel
                    Rectangle {
                        anchors.bottom: parent.bottom
                        width: parent.width * Math.min(1, modelData.percent / (backend.protein ? 15 : 40))
                        height: 3
                        color: theme.accent
                    }
                    Column {
                        anchors.centerIn: parent
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: modelData.letter
                            font.family: win.mono
                            font.bold: true
                            font.pixelSize: 15
                            color: win.textColor
                        }
                        Label {
                            anchors.horizontalCenter: parent.horizontalCenter
                            text: win.num(modelData.percent, 1) + "%"
                            font.pixelSize: 11
                            color: win.dim
                        }
                    }
                }
            }
        }

        Flow {
            Layout.fillWidth: true
            spacing: 8
            Button { text: qsTr("Export JSON"); flat: true; onClicked: backend.exportAs("json") }
            Button { text: qsTr("Export CSV"); flat: true; onClicked: backend.exportAs("csv") }
            Button { text: qsTr("Length chart as SVG"); flat: true; onClicked: backend.exportAs("length-svg") }
            Button { visible: backend.fastq; text: qsTr("Quality chart as SVG"); flat: true; onClicked: backend.exportAs("quality-svg") }
        }
    }

    component Panel: Rectangle {
        color: win.panel
        radius: theme.radius
    }
    component Tile: Rectangle {
        property string label
        property string value
        Layout.fillWidth: true
        implicitHeight: 66
        color: win.panel
        radius: theme.radius
        Column {
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: 2
            Label { text: label; color: win.dim; font.pixelSize: 12 }
            Label { text: value; color: win.textColor; font.pixelSize: 20; font.bold: true }
        }
    }
}
