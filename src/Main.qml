import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts
import QtQuick.Window

ApplicationWindow {
    id: win
    width: 1280
    height: 820
    minimumWidth: 720
    minimumHeight: 520
    visible: true
    title: (focusInfo.modified ? "* " : "") + focusInfo.fileName + " - Omawrite"

    readonly property var focusInfo: {
        var list = backend.paneInfo;
        var i = Math.min(backend.focusedPane, list.length - 1);
        return i >= 0 ? list[i] : ({ fileName: "Untitled.md", modified: false });
    }

    readonly property bool darkMode: backend.darkMode
    readonly property color pageColor: backend.themeBackground
    readonly property color textColor: backend.themeForeground
    readonly property color strongTextColor: backend.themeForeground
    readonly property color mutedColor: darkMode ? "#909191" : "#aeb1b5"
    readonly property color selectionFill: backend.themeSelection
    // The desktop's text size knob (GNOME's text-scaling-factor, which
    // `omarchy display text size` drives) anchored so its 12px default leaves
    // the app at the sizes it was designed around.
    readonly property real textScale: backend.textScale
    readonly property int editorFontPixelSize: scaledSize(20)
    readonly property int editorWidth: Math.min(
        Math.round(writerFontMetrics.averageCharacterWidth * 65),
        Math.max(360, (backend.splitView ? width / 2 : width) - Math.round(writerFontMetrics.averageCharacterWidth * 20)))
    property bool closeConfirmed: false
    property int pendingCloseTabIndex: -1
    property bool closingForQuit: false
    // -1 when no save-then-close is pending, else the pane index whose save
    // we are waiting on before finishing a tab close.
    property int awaitingPendingSave: -1
    readonly property int tabStripHeight: scaledSize(34)

    Material.theme: darkMode ? Material.Dark : Material.Light
    Material.accent: backend.themeAccent
    color: pageColor

    onClosing: function(close) {
        if (closeConfirmed || !backend.anyModified) {
            closeConfirmed = true;
            return;
        }

        close.accepted = false;
        closingForQuit = true;
        closeNextDirtyTab();
    }

    // Closes tabs with unsaved changes one at a time (each one may prompt via
    // unsavedChangesDialog through backend's tabCloseNeedsConfirmation signal),
    // then actually closes the window once none are left.
    function closeNextDirtyTab() {
        var tabs = backend.paneInfo.length > 0 ? backend.paneInfo[0].tabs : [];
        for (var i = 0; i < tabs.length; i++) {
            if (tabs[i].modified) {
                backend.closeTab(i);
                return;
            }
        }
        closeConfirmed = true;
        close();
    }

    function finishPendingTabClose() {
        var index = pendingCloseTabIndex;
        pendingCloseTabIndex = -1;
        if (index < 0)
            return;
        backend.forceCloseTab(index);
        if (closingForQuit)
            closeNextDirtyTab();
    }

    FontMetrics {
        id: writerFontMetrics
        font.family: "iA Writer Mono S"
        font.pixelSize: win.editorFontPixelSize
    }

    // Every hardcoded size in the interface is expressed at text scale 1.
    function scaledSize(pixels) {
        return Math.max(1, Math.round(pixels * win.textScale));
    }

    function toggleFullScreen() {
        win.visibility = win.visibility === Window.FullScreen
            ? Window.Windowed
            : Window.FullScreen;
    }

    Shortcut {
        sequence: "Ctrl+?"
        context: Qt.ApplicationShortcut
        onActivated: shortcutsDialog.open()
    }

    Shortcut {
        sequence: "Ctrl+N"
        context: Qt.ApplicationShortcut
        onActivated: backend.newWindow()
    }

    Shortcut {
        sequences: ["Meta+F", "F11"]
        context: Qt.ApplicationShortcut
        onActivated: toggleFullScreen()
    }

    Shortcut {
        sequence: "Ctrl+\\"
        context: Qt.ApplicationShortcut
        onActivated: backend.setSplitView(!backend.splitView)
    }

    Dialog {
        id: shortcutsDialog
        modal: true
        title: "Keyboard shortcuts"
        standardButtons: Dialog.Close
        anchors.centerIn: parent
        contentItem: Label {
            text: "Ctrl+S  Save\nCtrl+Shift+S  Save As\nCtrl+O  Open\nCtrl+N  New Window\nCtrl+T  New Tab\nCtrl+W  Close Tab\nCtrl+Tab  Next Tab\nCtrl+Shift+Tab  Previous Tab\nCtrl+\\  Toggle Split View\nCtrl+F  Find\nCtrl+H  Find and Replace\nCtrl+B  Bold\nCtrl+I  Italic\nCtrl+K  Link\nCtrl+E  Toggle formatted view\nCtrl+P  Print\nF11 / Super+F  Fullscreen\nCtrl+?  Shortcuts"
            lineHeight: 1.5
        }
    }

    Row {
        anchors.fill: parent
        spacing: 0

        EditorPaneView {
            id: pane0
            hostWindow: win
            paneIndex: 0
            width: backend.splitView ? parent.width / 2 : parent.width
            height: parent.height
        }

        Rectangle {
            width: 1
            height: parent.height
            visible: backend.splitView
            color: win.darkMode ? "#33332e" : "#e4e3d6"
        }

        Loader {
            id: pane1Loader
            active: backend.splitView
            width: active ? parent.width - pane0.width - 1 : 0
            height: parent.height
            sourceComponent: EditorPaneView {
                hostWindow: win
                paneIndex: 1
                width: pane1Loader.width
                height: pane1Loader.height
            }
        }
    }

    Component.onCompleted: {
        var geometry = backend.windowGeometry();
        if (geometry.x >= 0) x = geometry.x;
        if (geometry.y >= 0) y = geometry.y;
        width = geometry.width;
        height = geometry.height;
        if (geometry.maximized) showMaximized();
    }

    Component.onDestruction: backend.saveWindowGeometry(x, y, width, height, visibility === Window.Maximized)

}
