import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    ColumnLayout {
        anchors.centerIn: parent
        spacing: 14
        visible: !backend.busy

        Label {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Open a FASTA or FASTQ file")
            font.pixelSize: 22
            color: win.textColor
        }
        Label {
            Layout.alignment: Qt.AlignHCenter
            Layout.maximumWidth: 460
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            text: qsTr("Plain or gzipped. The file is only read, never changed. Drop it here, or press Ctrl+O.")
            color: win.dim
            font.pixelSize: 14
        }
        Button {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Open file")
            highlighted: true
            Material.roundedScale: theme.radius
            Material.foreground: theme.accentForeground
            onClicked: backend.open()
        }
    }

    Label {
        anchors.centerIn: parent
        visible: backend.busy
        text: qsTr("Indexing… %1%").arg(win.num(backend.progress * 100))
        color: win.dim
        font.pixelSize: 16
    }
}
