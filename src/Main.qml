import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1180
    height: 760
    minimumWidth: 760
    minimumHeight: 480
    visible: true
    title: backend.loaded ? "Seq — " + backend.fileName : "Seq"
    color: "#0e0e10"

    Material.theme: Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    readonly property color panel: "#16161a"
    readonly property color line: "#26262b"
    readonly property color textColor: "#e6e6ea"
    readonly property color dim: "#8a8a90"
    readonly property string mono: "monospace"

    property bool showSummary: false
    property int detailTab: 0
    readonly property bool typing: activeFocusItem !== null && activeFocusItem.hasOwnProperty("cursorPosition")
    readonly property bool popupOpen: help.opened || exportPopup.opened
    readonly property bool keys: !typing && !popupOpen

    Binding { target: backend; property: "chartColor"; value: theme.accent }

    // Numbers take the language's separators; reading translator.language
    // re-renders them when it changes.
    function num(value, decimals) {
        translator.language
        return Number(value).toLocaleString(Qt.locale(), 'f', decimals === undefined ? 0 : decimals)
    }
    function unit() { return backend.protein ? qsTr("aa") : qsTr("bp") }
    function focusSearch() {
        if (!backend.loaded)
            return
        topBar.searchField.forceActiveFocus()
        topBar.searchField.selectAll()
    }
    function showTab(tab) {
        if (!backend.loaded || backend.protein && tab > 0)
            return
        showSummary = false
        detailTab = detailTab === tab ? 0 : tab
    }
    // The backend keeps the shown tab's translation or ORFs up to date.
    onDetailTabChanged: backend.setDetailTab(detailTab)

    Shortcut { sequence: "?"; context: Qt.ApplicationShortcut; enabled: !win.typing; onActivated: help.opened ? help.close() : help.open() }
    Shortcut { sequence: "Q"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+Q"; context: Qt.ApplicationShortcut; onActivated: Qt.quit() }
    Shortcut { sequence: "Ctrl+O"; context: Qt.ApplicationShortcut; enabled: !win.popupOpen; onActivated: backend.open() }
    Shortcut { sequence: "/"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: win.focusSearch() }
    Shortcut { sequence: "Ctrl+F"; context: Qt.ApplicationShortcut; enabled: !win.popupOpen; onActivated: win.focusSearch() }
    Shortcut { sequence: "N"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.nextMatch() }
    Shortcut { sequence: "Shift+N"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.previousMatch() }
    Shortcut { sequences: ["J", "Down"]; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.move(1) }
    Shortcut { sequences: ["K", "Up"]; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.move(-1) }
    Shortcut { sequence: "Home"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.select(0) }
    Shortcut { sequence: "End"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.select(backend.records.count - 1) }
    Shortcut { sequence: "PgDown"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: detail.page(1) }
    Shortcut { sequence: "PgUp"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: detail.page(-1) }
    Shortcut { sequence: "Space"; context: Qt.ApplicationShortcut; enabled: win.keys && backend.loaded; onActivated: win.showSummary = !win.showSummary }
    Shortcut { sequence: "R"; context: Qt.ApplicationShortcut; enabled: win.keys && !backend.protein
        onActivated: { win.showSummary = false; win.detailTab = 0; backend.toggleReverse() } }
    Shortcut { sequence: "T"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: win.showTab(1) }
    Shortcut { sequence: "O"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: win.showTab(2) }
    Shortcut { sequence: "W"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.toggleLineWidth() }
    Shortcut { sequence: "Ctrl+E"; context: Qt.ApplicationShortcut; enabled: !win.popupOpen && backend.loaded && !backend.busy; onActivated: exportPopup.open() }
    Shortcut { sequence: "Ctrl+C"; context: Qt.ApplicationShortcut; enabled: win.keys; onActivated: backend.copyCurrent() }
    Shortcut { sequence: "Escape"; context: Qt.ApplicationShortcut; enabled: win.keys
        onActivated: backend.error ? backend.dismissError() : backend.clearSearch() }

    // Right-to-left languages mirror the window; the Translator sets the
    // application's layout direction.
    Item {
        id: root
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            TopBar {
                id: topBar
                Layout.fillWidth: true
            }

            ProgressBar {
                Layout.fillWidth: true
                visible: backend.busy
                value: backend.progress
                indeterminate: backend.progress <= 0
            }

            Banner {
                Layout.fillWidth: true
                visible: backend.error !== ""
                text: backend.error
                warning: true
                onDismissed: backend.dismissError()
            }
            Banner {
                Layout.fillWidth: true
                visible: backend.notice !== "" && backend.error === ""
                text: backend.notice
                onDismissed: backend.dismissNotice()
            }

            EmptyState {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: !backend.loaded
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                visible: backend.loaded
                spacing: 0

                RecordList {
                    Layout.preferredWidth: Math.max(320, Math.min(440, win.width * 0.32))
                    Layout.fillHeight: true
                }
                Rectangle { Layout.fillHeight: true; width: 1; color: win.line }
                StackLayout {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    currentIndex: win.showSummary ? 1 : 0
                    DetailPane { id: detail }
                    SummaryPane {}
                }
            }
        }

        DropArea {
            anchors.fill: parent
            onDropped: drop => {
                if (drop.hasUrls)
                    backend.openUrl(drop.urls[0])
            }
        }
    }

    HelpOverlay { id: help }
    ExportPopup { id: exportPopup }
}
