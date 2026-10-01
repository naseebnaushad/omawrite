import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Dialogs as Dialogs
import QtQuick.Layouts
import QtQuick.Window
import "EditorMutations.js" as EditorMutations

// One editor pane: its own tab strip, editor, formatted-preview, footer, and
// find/replace UI, all scoped to backend.paneInfo[paneIndex]. Exactly two of
// these can exist at once (split view); paneIndex picks which live document
// slot in Backend this instance drives. A FocusScope so pane-local shortcuts
// (Ctrl+S, Ctrl+B, Ctrl+F, ...) can be gated on this pane currently holding
// focus, whether that's the editor itself or its find/replace fields.
FocusScope {
    id: paneRoot

    required property var hostWindow
    required property int paneIndex
    readonly property bool isOtherPane: paneIndex !== 0

    readonly property var info: {
        var list = backend.paneInfo;
        return paneIndex < list.length ? list[paneIndex] : ({
            fileName: "Untitled.md", modified: false, wordCount: 0,
            status: "", activeTabIndex: -1, tabs: []
        });
    }
    readonly property bool isFocused: backend.focusedPane === paneIndex

    property bool searchOpen: false
    property bool searchUpdating: false
    property var searchMatches: []
    property int searchMatchIndex: -1
    property bool replaceOpen: false
    property bool previewMode: false

    onActiveFocusChanged: {
        if (activeFocus)
            backend.setFocusedPane(paneIndex);
    }

    Shortcut {
        sequence: "Ctrl+S"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: backend.save(paneRoot.paneIndex)
    }

    Shortcut {
        sequence: "Ctrl+Shift+S"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: backend.saveAsDialog(paneRoot.paneIndex)
    }

    Shortcut {
        sequence: "Ctrl+O"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: backend.openDialog(paneRoot.paneIndex)
    }

    Shortcut {
        sequence: "Ctrl+P"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: backend.printDocument(paneRoot.paneIndex)
    }

    Shortcut {
        sequence: "Ctrl+B"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: editor.wrapSelection("**", "**")
    }

    Shortcut {
        sequence: "Ctrl+I"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: editor.wrapSelection("*", "*")
    }

    Shortcut {
        sequence: "Ctrl+K"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: editor.insertLink()
    }

    Shortcut {
        sequence: "Ctrl+Z"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: editor.undo()
    }

    Shortcut {
        sequences: ["Ctrl+Shift+Z", "Ctrl+Y"]
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: editor.redo()
    }

    Shortcut {
        sequence: "Ctrl+F"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: paneRoot.openSearch(false)
    }

    Shortcut {
        sequence: "Ctrl+H"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: paneRoot.openSearch(true)
    }

    Shortcut {
        sequence: "Ctrl+G"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus && paneRoot.searchOpen
        onActivated: paneRoot.moveSearch(1)
    }

    Shortcut {
        sequence: "Ctrl+E"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: paneRoot.previewMode = !paneRoot.previewMode
    }

    Shortcut {
        sequence: "Ctrl+T"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: backend.newTab(paneRoot.paneIndex)
    }

    Shortcut {
        sequence: "Ctrl+W"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: backend.closeTab(paneRoot.info.activeTabIndex)
    }

    Shortcut {
        sequence: "Ctrl+Tab"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: {
            var count = paneRoot.info.tabs.length;
            if (count > 0)
                backend.switchTab(paneRoot.paneIndex, (paneRoot.info.activeTabIndex + 1) % count);
        }
    }

    Shortcut {
        sequence: "Ctrl+Shift+Tab"
        context: Qt.WindowShortcut
        enabled: paneRoot.activeFocus
        onActivated: {
            var count = paneRoot.info.tabs.length;
            if (count > 0)
                backend.switchTab(paneRoot.paneIndex, (paneRoot.info.activeTabIndex - 1 + count) % count);
        }
    }

    function openSearch(withReplace) {
        searchOpen = true;
        replaceOpen = !!withReplace;
        searchField.forceActiveFocus();
        searchField.selectAll();
    }

    function updateSearch() {
        var matches = [];
        var query = searchField.text;
        if (query.length > 0) {
            var haystack = editor.text.toLocaleLowerCase();
            var needle = query.toLocaleLowerCase();
            var position = 0;
            while ((position = haystack.indexOf(needle, position)) !== -1) {
                matches.push(position);
                position += Math.max(1, needle.length);
            }
        }
        searchMatches = matches;
        searchMatchIndex = matches.length > 0 ? 0 : -1;
        showSearchMatch();
    }

    function showSearchMatch() {
        var start = searchMatchIndex >= 0 ? searchMatches[searchMatchIndex] : -1;
        searchUpdating = true;
        backend.setSearchHighlight(paneIndex, searchField.text, start);
        if (start >= 0) {
            editor.select(start, start + searchField.text.length);
            editorFlick.ensureCursorVisible();
        }
        searchUpdating = false;
    }

    function moveSearch(direction) {
        if (searchMatches.length === 0)
            return;
        searchMatchIndex = (searchMatchIndex + direction + searchMatches.length)
                           % searchMatches.length;
        showSearchMatch();
    }

    function closeSearch() {
        searchOpen = false;
        searchUpdating = true;
        backend.setSearchHighlight(paneIndex, "", -1);
        editor.deselect();
        searchUpdating = false;
        replaceOpen = false;
        editor.forceActiveFocus();
    }

    Rectangle {
        id: tabStrip
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        height: hostWindow.tabStripHeight
        color: "transparent"

        Flickable {
            id: tabStripFlick
            anchors.left: parent.left
            anchors.right: paneButtons.left
            anchors.top: parent.top
            anchors.bottom: parent.bottom
            anchors.leftMargin: 8
            contentWidth: tabRow.width
            contentHeight: height
            clip: true
            boundsBehavior: Flickable.StopAtBounds

            Row {
                id: tabRow
                height: parent.height
                spacing: 2

                Repeater {
                    model: paneRoot.info.tabs

                    Rectangle {
                        id: tabDelegate
                        required property var modelData
                        required property int index
                        readonly property bool isActive: modelData.active

                        width: Math.min(220, Math.max(90, tabLabel.implicitWidth + 34))
                        height: hostWindow.scaledSize(28)
                        anchors.verticalCenter: parent.verticalCenter
                        radius: 6
                        color: isActive
                            ? (hostWindow.darkMode ? "#2a2a26" : "#efeee6")
                            : "transparent"

                        MouseArea {
                            anchors.fill: parent
                            anchors.rightMargin: 20
                            cursorShape: Qt.PointingHandCursor
                            onClicked: {
                                backend.setFocusedPane(paneRoot.paneIndex);
                                backend.switchTab(paneRoot.paneIndex, tabDelegate.index);
                            }
                        }

                        Label {
                            id: tabLabel
                            anchors.left: parent.left
                            anchors.right: closeTabButton.left
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.leftMargin: 12
                            anchors.rightMargin: 4
                            elide: Text.ElideRight
                            text: (tabDelegate.modelData.modified ? "• " : "")
                                + tabDelegate.modelData.fileName
                            color: tabDelegate.isActive ? hostWindow.strongTextColor : hostWindow.mutedColor
                            font.family: "iA Writer Mono S"
                            font.pixelSize: hostWindow.scaledSize(12)
                        }

                        Label {
                            id: closeTabButton
                            anchors.right: parent.right
                            anchors.verticalCenter: parent.verticalCenter
                            anchors.rightMargin: 8
                            text: "×"
                            color: hostWindow.mutedColor
                            font.pixelSize: hostWindow.scaledSize(14)

                            MouseArea {
                                anchors.fill: parent
                                anchors.margins: -6
                                cursorShape: Qt.PointingHandCursor
                                onClicked: backend.closeTab(tabDelegate.index)
                            }
                        }
                    }
                }
            }
        }

        Row {
            id: paneButtons
            anchors.right: parent.right
            anchors.verticalCenter: parent.verticalCenter
            anchors.rightMargin: 24
            spacing: 14

            FooterIconButton {
                iconName: "newTab"
                iconColor: hostWindow.mutedColor
                tooltip: "New tab"
                onClicked: {
                    backend.setFocusedPane(paneRoot.paneIndex);
                    backend.newTab(paneRoot.paneIndex);
                }
            }

            FooterIconButton {
                iconName: paneRoot.isOtherPane ? "closeSplit" : "split"
                iconColor: hostWindow.mutedColor
                tooltip: paneRoot.isOtherPane ? "Close split" : "Split right"
                onClicked: backend.setSplitView(!paneRoot.isOtherPane)
            }
        }
    }

    Flickable {
        id: editorFlick
        visible: !paneRoot.previewMode
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: hostWindow.tabStripHeight
        clip: true
        contentWidth: width
        contentHeight: Math.max(height, editor.y + editor.implicitHeight + 220)
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar {
            policy: ScrollBar.AsNeeded
            // Wheel scrolling moves contentY directly rather than
            // flicking the Flickable, so the bar has to be told about
            // that activity; linger briefly after the last event.
            active: hovered || pressed || wheelScroll.running || scrollLinger.running
            // Stop above the footer strip so the bar doesn't overlap
            // the word count in the bottom-right corner. Padding and
            // inset, not anchors: the attached-ScrollBar layout overrides
            // anchors. Padding stops the thumb, the inset the track.
            bottomPadding: hostWindow.scaledSize(32)
            bottomInset: hostWindow.scaledSize(32)
        }

        Timer {
            id: scrollLinger
            interval: 600
        }

        // Flickable turns a wheel notch into a flick sized by the small
        // application font, which crawls next to a browser. Reproduce
        // Chromium's wheel physics instead (cc::ScrollOffsetAnimationCurve):
        // each notch moves 3 lines of 40px towards a running target, the
        // animation gets shorter as the outstanding distance grows, and a
        // notch landing mid-animation carries the current velocity into
        // the new curve, so sustained spinning keeps picking up speed.
        readonly property real wheelStep: hostWindow.scaledSize(120)

        FrameAnimation {
            id: wheelScroll
            running: false

            property real startY: 0
            property real targetY: 0
            property real duration: 0.2
            // Cubic bezier easing; ease-in-out (0.42, 0, 0.58, 1) for a
            // fresh scroll, with y1 tilted on retarget so the curve's
            // initial slope matches the velocity it inherits.
            property real cx1: 0.42
            property real cy1: 0
            readonly property real cx2: 0.58
            readonly property real cy2: 1

            onTriggered: {
                var x = elapsedTime / duration;
                if (x >= 1) {
                    editorFlick.contentY = editorFlick.snapToPixel(targetY);
                    stop();
                    return;
                }
                editorFlick.contentY = editorFlick.snapToPixel(
                    startY + (targetY - startY) * curveY(solveCurve(x)));
            }

            function begin(from, to, dur, slope) {
                startY = from;
                targetY = to;
                duration = dur;
                cx1 = 0.42;
                cy1 = 0.42 * Math.max(-1000, Math.min(1000, slope));
                restart();
            }

            function retarget(newTarget) {
                var s = solveCurve(Math.min(1, elapsedTime / duration));
                var pos = startY + (targetY - startY) * curveY(s);
                var delta = newTarget - pos;
                if (Math.abs(delta) < 0.5) {
                    editorFlick.contentY = newTarget;
                    stop();
                    return;
                }

                var velocity = curveDY(s) / Math.max(1e-6, curveDX(s))
                    * (targetY - startY) / duration;
                var dur = editorFlick.wheelDuration(delta);
                // When already moving faster than the eased curve would,
                // bound the duration by the time to target at the current
                // velocity; the 2.5x covers the ease-out tail.
                if (velocity !== 0 && delta / velocity > 0)
                    dur = Math.min(dur, delta / velocity * 2.5);
                begin(pos, newTarget, dur, velocity * dur / delta);
            }

            // Cubic bezier through (0,0), (cx1,cy1), (cx2,cy2), (1,1),
            // evaluated by Newton-solving the curve parameter from x.
            function curveX(s) { return 3 * s * (1 - s) * ((1 - s) * cx1 + s * cx2) + s * s * s; }
            function curveY(s) { return 3 * s * (1 - s) * ((1 - s) * cy1 + s * cy2) + s * s * s; }
            function curveDX(s) { return 3 * (1 - s) * (1 - s) * cx1 + 6 * (1 - s) * s * (cx2 - cx1) + 3 * s * s * (1 - cx2); }
            function curveDY(s) { return 3 * (1 - s) * (1 - s) * cy1 + 6 * (1 - s) * s * (cy2 - cy1) + 3 * s * s * (1 - cy2); }

            function solveCurve(x) {
                var s = x;
                for (var i = 0; i < 8; ++i) {
                    var error = curveX(s) - x;
                    if (Math.abs(error) < 0.001)
                        break;
                    var d = curveDX(s);
                    if (Math.abs(d) < 1e-6)
                        break;
                    s = Math.max(0, Math.min(1, s - error / d));
                }
                return s;
            }
        }

        WheelHandler {
            // Wayland compositors route every pointer's scroll through
            // one seat device that Qt classifies as a touchpad, so the
            // device type cannot tell a mouse wheel from two-finger
            // scrolling. Distinguish by event shape instead: discrete
            // wheel notches arrive with only angleDelta set, while
            // finger scrolling carries pixel-precise pixelDelta.
            acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad
            onWheel: function(wheel) {
                scrollLinger.restart();
                if (wheel.pixelDelta.y !== 0)
                    editorFlick.scrollTo(editorFlick.clampContentY(editorFlick.contentY - wheel.pixelDelta.y));
                else
                    editorFlick.scrollByWheel(wheel);
                wheel.accepted = true;
            }
        }

        onMovementStarted: wheelScroll.stop()

        function scrollByWheel(wheel) {
            // High-resolution wheels report fractional notches; feed
            // those through the same animated path, like Chromium does
            // for every wheel-source event.
            var notches = wheel.angleDelta.y / 120;
            if (notches === 0)
                return;

            if (wheelScroll.running) {
                wheelScroll.retarget(clampContentY(wheelScroll.targetY - notches * wheelStep));
                return;
            }

            var target = clampContentY(contentY - notches * wheelStep);
            if (target !== contentY)
                wheelScroll.begin(contentY, target, wheelDuration(target - contentY), 0);
        }

        // Chromium's inverse-delta duration: 200ms for a single notch,
        // ramping down to 100ms once 480px are outstanding.
        function wheelDuration(delta) {
            var pixels = Math.abs(delta) / hostWindow.textScale;
            return Math.max(6, Math.min(12, 14 - pixels / 60)) / 60;
        }

        function clampContentY(y) {
            return Math.max(0, Math.min(Math.max(0, contentHeight - height), y));
        }

        // Whole device pixels keep natively hinted glyphs from
        // re-rasterizing mid-animation, which reads as shimmer.
        function snapToPixel(y) {
            return Math.round(y * Screen.devicePixelRatio) / Screen.devicePixelRatio;
        }

        // Jump to a position, abandoning any wheel animation still running.
        function scrollTo(y) {
            wheelScroll.stop();
            contentY = snapToPixel(y);
        }

        // Keep the editing caret within the viewport so writing past the
        // bottom edge scrolls the page along with the text.
        function ensureCursorVisible() {
            var margin = hostWindow.editorFontPixelSize * 2;
            var cursorTop = editor.y + editor.cursorRectangle.y;
            var cursorBottom = cursorTop + editor.cursorRectangle.height;
            var maxContentY = Math.max(0, contentHeight - height);

            if (cursorBottom + margin > contentY + height)
                scrollTo(Math.min(maxContentY, cursorBottom + margin - height));
            else if (cursorTop - margin < contentY)
                scrollTo(Math.max(0, cursorTop - margin));
        }

        TextEdit {
            id: editor
            objectName: "sourceEditor" + paneRoot.paneIndex
            x: Math.round((editorFlick.width - width) / 2)
            y: Math.max(42, Math.round(hostWindow.height * 0.05))
            width: hostWindow.editorWidth
            height: Math.max(editorFlick.height - y - 96, implicitHeight + 20)
            text: ""
            textFormat: TextEdit.PlainText
            wrapMode: TextEdit.Wrap
            selectByMouse: true
            persistentSelection: true
            activeFocusOnPress: true
            color: hostWindow.textColor
            selectedTextColor: hostWindow.strongTextColor
            selectionColor: hostWindow.selectionFill
            font.family: "iA Writer Mono S"
            font.pixelSize: hostWindow.editorFontPixelSize
            font.weight: Font.Normal
            // Native rendering hints glyphs to the pixel grid, which is
            // crispest at whole scale factors but misplaces and unevenly
            // rasterizes glyphs at fractional ones (and goes stale when
            // the compositor delivers the fractional scale after the
            // first frame). Fall back to Qt's scalable renderer there.
            renderType: Screen.devicePixelRatio % 1 === 0 ? TextEdit.NativeRendering : TextEdit.QtRendering
            cursorDelegate: Rectangle {
                width: 1
                color: hostWindow.strongTextColor
            }
            onCursorRectangleChanged: editorFlick.ensureCursorVisible()

            function replaceSelectionWith(replacement) {
                var start = Math.min(selectionStart, selectionEnd);
                var end = Math.max(selectionStart, selectionEnd);
                EditorMutations.replaceRange(editor, start, end, replacement);
            }

            function wrapSelection(before, after) {
                forceActiveFocus();
                var start = Math.min(selectionStart, selectionEnd);
                var end = Math.max(selectionStart, selectionEnd);
                var selected = text.slice(start, end);
                EditorMutations.replaceRange(editor, start, end,
                                             before + selected + after,
                                             before.length,
                                             before.length + selected.length);
            }

            function insertLink() {
                var start = Math.min(selectionStart, selectionEnd);
                var end = Math.max(selectionStart, selectionEnd);
                var selected = text.slice(start, end);
                var url = backend.clipboardUrl();
                var label = selected.length > 0 ? selected : "link text";
                var destination = url.length > 0 ? url : "https://";
                var escapedLabel = escapeMarkdownLinkText(label);
                var markdown = "[" + escapedLabel + "](" + escapeMarkdownLinkDestination(destination) + ")";
                if (selected.length === 0) {
                    EditorMutations.replaceRange(editor, start, end, markdown,
                                                 1, 1 + escapedLabel.length);
                } else if (url.length === 0) {
                    EditorMutations.replaceRange(editor, start, end, markdown,
                                                 escapedLabel.length + 3,
                                                 markdown.length - 1);
                } else {
                    EditorMutations.replaceRange(editor, start, end, markdown);
                }
            }

            function smartReturn(softBreak) {
                if (softBreak) {
                    replaceSelectionWith("\n");
                    return;
                }
                var lineStart = text.lastIndexOf("\n", cursorPosition - 1) + 1;
                var line = text.slice(lineStart, cursorPosition);
                var before = text.slice(0, cursorPosition);
                var fences = (before.match(/^\s*```/gm) || []).length;
                if ((fences % 2) === 1) {
                    replaceSelectionWith("\n");
                    return;
                }
                var match = line.match(/^(\s*)([-+*]|\d+[.)]|>+)\s+(.*)$/);
                if (match) {
                    if (match[3].length === 0) {
                        EditorMutations.replaceRange(editor, lineStart,
                                                     cursorPosition, "\n");
                    } else {
                        var marker = match[2];
                        if (/^\d/.test(marker))
                            marker = (parseInt(marker) + 1) + marker.slice(-1);
                        replaceSelectionWith("\n" + match[1] + marker + " ");
                    }
                    return;
                }
                replaceSelectionWith("\n\n");
            }

            function escapeMarkdownLinkText(linkText) {
                return linkText.replace(/\\/g, "\\\\")
                               .replace(/\[/g, "\\[")
                               .replace(/\]/g, "\\]");
            }

            function escapeMarkdownLinkDestination(linkUrl) {
                return linkUrl.replace(/\\/g, "\\\\")
                              .replace(/\(/g, "\\(")
                              .replace(/\)/g, "\\)");
            }

            function pasteClipboardUrlAsMarkdownLink() {
                var start = Math.min(selectionStart, selectionEnd);
                var end = Math.max(selectionStart, selectionEnd);
                if (start === end)
                    return false;

                var url = backend.clipboardUrl();
                if (url === "")
                    return false;

                var selected = text.slice(start, end);
                var leading = selected.match(/^\s*/)[0];
                var trailing = selected.match(/\s*$/)[0];
                var linkText = selected.slice(leading.length,
                                              selected.length - trailing.length);
                if (linkText === "")
                    return false;

                replaceSelectionWith(leading + "[" + escapeMarkdownLinkText(linkText) + "]("
                                     + escapeMarkdownLinkDestination(url) + ")" + trailing);
                return true;
            }

            function pasteClipboardAsPlainText() {
                var pastedText = backend.clipboardText();
                if (pastedText.length > 0)
                    replaceSelectionWith(pastedText);
            }

            function skipHiddenForward(position) {
                var pos = position;
                var ranges = backend.hiddenRangesAt(paneRoot.paneIndex, pos);
                for (var i = 0; i < ranges.length; i++) {
                    if (pos >= ranges[i].start && pos < ranges[i].end) {
                        pos = ranges[i].end;
                        i = -1;
                    }
                }
                return pos;
            }

            function skipHiddenBackward(position) {
                var pos = position;
                var ranges = backend.hiddenRangesAt(paneRoot.paneIndex, pos);
                for (var i = ranges.length - 1; i >= 0; i--) {
                    if (pos > ranges[i].start && pos <= ranges[i].end) {
                        pos = ranges[i].start;
                        i = ranges.length;
                    }
                }
                return pos;
            }

            function moveCursorVisibly(direction) {
                if (selectionStart !== selectionEnd) {
                    cursorPosition = direction > 0
                        ? Math.max(selectionStart, selectionEnd)
                        : Math.min(selectionStart, selectionEnd);
                    return;
                }

                var pos = Math.max(0, Math.min(text.length, cursorPosition + direction));
                cursorPosition = direction > 0
                    ? skipHiddenForward(pos)
                    : skipHiddenBackward(pos);
            }

            function movePage(direction, extendSelection) {
                var pageStep = Math.max(hostWindow.editorFontPixelSize,
                                        editorFlick.height - hostWindow.editorFontPixelSize * 2);
                var rect = cursorRectangle;
                var targetY = rect.y + rect.height / 2 + direction * pageStep;
                var target = positionAt(rect.x, Math.max(0, targetY));
                if (extendSelection)
                    moveCursorSelection(target, TextEdit.SelectCharacters);
                else
                    cursorPosition = target;
            }

            function deleteParagraphBreakBehindCursor() {
                if (selectionStart !== selectionEnd || cursorPosition < 2)
                    return false;

                if (text.slice(cursorPosition - 2, cursorPosition) !== "\n\n")
                    return false;

                var start = cursorPosition - 2;
                remove(start, cursorPosition);
                cursorPosition = start;
                return true;
            }

            Keys.priority: Keys.BeforeItem
            Keys.onPressed: function(event) {
                var pasteKey = (event.key === Qt.Key_V)
                    && (event.modifiers & Qt.ControlModifier)
                    && !(event.modifiers & (Qt.AltModifier | Qt.MetaModifier | Qt.ShiftModifier));
                var shiftInsert = (event.key === Qt.Key_Insert)
                    && (event.modifiers & Qt.ShiftModifier)
                    && !(event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier));
                if (pasteKey || shiftInsert) {
                    if (!pasteClipboardUrlAsMarkdownLink())
                        pasteClipboardAsPlainText();
                    event.accepted = true;
                    return;
                }

                var returnKey = event.key === Qt.Key_Return || event.key === Qt.Key_Enter;
                var commandModifier = event.modifiers & (Qt.ControlModifier | Qt.AltModifier | Qt.MetaModifier);
                if (returnKey && !commandModifier) {
                    smartReturn(event.modifiers & Qt.ShiftModifier);
                    event.accepted = true;
                } else if (!commandModifier && event.key === Qt.Key_Backspace
                           && deleteParagraphBreakBehindCursor()) {
                    event.accepted = true;
                } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                           && event.key === Qt.Key_Right) {
                    moveCursorVisibly(1);
                    event.accepted = true;
                } else if (!commandModifier && !(event.modifiers & Qt.ShiftModifier)
                           && event.key === Qt.Key_Left) {
                    moveCursorVisibly(-1);
                    event.accepted = true;
                } else if (!commandModifier
                           && (event.key === Qt.Key_PageDown || event.key === Qt.Key_PageUp)) {
                    movePage(event.key === Qt.Key_PageDown ? 1 : -1,
                             event.modifiers & Qt.ShiftModifier);
                    event.accepted = true;
                }
            }

            onTextChanged: {
                if (paneRoot.searchUpdating)
                    return;
                var contentChanged = backend.editorTextChanged(paneRoot.paneIndex);
                if (paneRoot.searchOpen && contentChanged)
                    paneRoot.updateSearch();
            }

            Text {
                anchors.left: parent.left
                anchors.top: parent.top
                text: "# Start writing"
                visible: editor.text.length === 0 && !editor.activeFocus
                color: hostWindow.mutedColor
                font.family: editor.font.family
                font.pixelSize: editor.font.pixelSize
                font.weight: editor.font.weight
            }

            Component.onCompleted: {
                backend.attachDocument(paneRoot.paneIndex, textDocument);
                if (paneRoot.paneIndex === backend.focusedPane)
                    forceActiveFocus();
            }
        }
    }

    ScrollView {
        id: previewScroll
        anchors.fill: parent
        anchors.leftMargin: 24
        anchors.rightMargin: 24
        anchors.topMargin: hostWindow.tabStripHeight
        anchors.bottomMargin: 32
        clip: true
        visible: paneRoot.previewMode

        TextEdit {
            id: previewText
            objectName: "previewEditor" + paneRoot.paneIndex
            x: Math.round((previewScroll.availableWidth - width) / 2)
            width: hostWindow.editorWidth
            topPadding: Math.max(42, Math.round(hostWindow.height * 0.05))
            bottomPadding: 96
            readOnly: true
            selectByMouse: true
            wrapMode: TextEdit.Wrap
            textFormat: TextEdit.MarkdownText
            text: editor.text
            color: hostWindow.textColor
            selectedTextColor: hostWindow.strongTextColor
            selectionColor: hostWindow.selectionFill
            font.family: "iA Writer Mono S"
            font.pixelSize: hostWindow.editorFontPixelSize
            font.weight: Font.Normal
            renderType: Screen.devicePixelRatio % 1 === 0 ? TextEdit.NativeRendering : TextEdit.QtRendering
        }
    }

    Row {
        id: footerStatus
        anchors.left: parent.left
        anchors.bottom: parent.bottom
        anchors.leftMargin: 12
        anchors.bottomMargin: 10
        spacing: 12
        opacity: 0.55

        FooterIconButton {
            objectName: "saveButton"
            iconName: "save"
            iconColor: hostWindow.mutedColor
            tooltip: "Save"
            onClicked: backend.save(paneRoot.paneIndex)
        }

        FooterIconButton {
            objectName: "openButton"
            iconName: "open"
            iconColor: hostWindow.mutedColor
            tooltip: "Open"
            onClicked: backend.openDialog(paneRoot.paneIndex)
        }

        FooterIconButton {
            iconName: "preview"
            iconColor: paneRoot.previewMode ? hostWindow.strongTextColor : hostWindow.mutedColor
            tooltip: paneRoot.previewMode ? "Edit" : "View formatted"
            onClicked: paneRoot.previewMode = !paneRoot.previewMode
        }

        Label {
            text: paneRoot.info.status
            color: hostWindow.mutedColor
            font.family: "iA Writer Mono S"
            font.pixelSize: hostWindow.scaledSize(11)
            visible: text !== ""
            elide: Text.ElideRight
            width: Math.min(360, paneRoot.width / 3)
            height: hostWindow.scaledSize(16)
            verticalAlignment: Text.AlignVCenter
        }
    }

    Label {
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.rightMargin: 12
        anchors.bottomMargin: 10
        text: paneRoot.info.wordCount + (paneRoot.info.wordCount === 1 ? " Word" : " Words")
        color: hostWindow.mutedColor
        opacity: 0.75
        font.family: "iA Writer Mono S"
        font.pixelSize: hostWindow.scaledSize(11)
    }

    Pane {
        anchors.top: parent.top
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.topMargin: hostWindow.tabStripHeight + 12
        anchors.leftMargin: 12
        anchors.rightMargin: 12
        height: hostWindow.scaledSize(paneRoot.replaceOpen ? 104 : 56)
        visible: paneRoot.searchOpen
        z: 10
        leftPadding: 16
        rightPadding: 8
        topPadding: 0
        bottomPadding: 0
        Material.elevation: 8

        background: Rectangle {
            radius: 9
            color: hostWindow.darkMode ? "#22221f" : "#fffef2"
        }

        RowLayout {
            anchors.fill: parent
            spacing: 8

            Item {
                Layout.fillWidth: true
                Layout.fillHeight: true

                TextInput {
                    id: searchField
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.top: parent.top
                    height: paneRoot.replaceOpen ? parent.height / 2 : parent.height
                    verticalAlignment: TextInput.AlignVCenter
                    selectByMouse: true
                    color: hostWindow.textColor
                    selectionColor: hostWindow.selectionFill
                    selectedTextColor: hostWindow.strongTextColor
                    font.pixelSize: hostWindow.scaledSize(17)
                    clip: true
                    onTextChanged: paneRoot.updateSearch()
                    Keys.onReturnPressed: function(event) {
                        paneRoot.moveSearch((event.modifiers & Qt.ShiftModifier) ? -1 : 1);
                        event.accepted = true;
                    }
                    Keys.onEscapePressed: function(event) {
                        paneRoot.closeSearch();
                        event.accepted = true;
                    }
                }

                TextInput {
                    id: replaceField
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: parent.height / 2
                    visible: paneRoot.replaceOpen
                    verticalAlignment: TextInput.AlignVCenter
                    color: hostWindow.textColor
                    selectionColor: hostWindow.selectionFill
                    selectedTextColor: hostWindow.strongTextColor
                    font.pixelSize: hostWindow.scaledSize(17)
                    Keys.onReturnPressed: replaceCurrentButton.clicked()
                }

                Label {
                    anchors.verticalCenter: replaceField.verticalCenter
                    text: "Replace with"
                    visible: paneRoot.replaceOpen && replaceField.text.length === 0
                    color: hostWindow.mutedColor
                    font.pixelSize: hostWindow.scaledSize(17)
                }

                Label {
                    anchors.verticalCenter: searchField.verticalCenter
                    text: "Find"
                    visible: searchField.text.length === 0
                    color: hostWindow.mutedColor
                    font.pixelSize: hostWindow.scaledSize(17)
                }
            }

            Label {
                Layout.preferredWidth: hostWindow.scaledSize(58)
                Layout.fillHeight: true
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
                text: paneRoot.searchMatches.length === 0
                    ? "0/0"
                    : (paneRoot.searchMatchIndex + 1) + "/" + paneRoot.searchMatches.length
                color: hostWindow.darkMode ? hostWindow.textColor : "#62635f"
                font.pixelSize: hostWindow.scaledSize(16)
            }

            Button {
                id: replaceCurrentButton
                visible: paneRoot.replaceOpen
                text: "Replace"
                onClicked: {
                    if (paneRoot.searchMatchIndex < 0) return;
                    var start = paneRoot.searchMatches[paneRoot.searchMatchIndex];
                    EditorMutations.replaceRange(editor, start,
                                                 start + searchField.text.length,
                                                 replaceField.text);
                    paneRoot.updateSearch();
                }
            }

            Button {
                visible: paneRoot.replaceOpen
                text: "All"
                onClicked: {
                    if (searchField.text.length === 0) return;
                    for (var i = paneRoot.searchMatches.length - 1; i >= 0; --i) {
                        var start = paneRoot.searchMatches[i];
                        EditorMutations.replaceRange(editor, start,
                                                     start + searchField.text.length,
                                                     replaceField.text);
                    }
                    paneRoot.updateSearch();
                }
            }

            Rectangle {
                Layout.preferredWidth: 1
                Layout.preferredHeight: 34
                color: hostWindow.darkMode ? "#6f6f62" : "#d5d56e"
            }

            SearchIconButton {
                iconName: "up"
                iconColor: hostWindow.darkMode ? hostWindow.textColor : "#62635f"
                onClicked: paneRoot.moveSearch(-1)
            }

            SearchIconButton {
                iconName: "down"
                iconColor: hostWindow.darkMode ? hostWindow.textColor : "#62635f"
                onClicked: paneRoot.moveSearch(1)
            }

            SearchIconButton {
                iconName: "close"
                iconColor: hostWindow.darkMode ? hostWindow.textColor : "#62635f"
                onClicked: paneRoot.closeSearch()
            }
        }
    }

    UnsavedChangesDialog {
        id: unsavedChangesDialog
        fileName: paneRoot.info.fileName
        darkMode: hostWindow.darkMode
        textScale: hostWindow.textScale
        textColor: hostWindow.textColor
        strongTextColor: hostWindow.strongTextColor
        activeButtonColor: backend.themeAccent
        containerWidth: paneRoot.width
        containerHeight: paneRoot.height

        onDiscardRequested: {
            backend.discardRecovery();
            hostWindow.finishPendingTabClose(paneRoot.paneIndex);
        }
        onSaveRequested: {
            hostWindow.awaitingPendingSave = paneRoot.paneIndex;
            backend.save(paneRoot.paneIndex);
        }
        onCancelRequested: {
            hostWindow.pendingCloseTabIndex = -1;
            hostWindow.closingForQuit = false;
        }
    }

    ExternalChangeDialog {
        id: externalChangeDialog
        darkMode: hostWindow.darkMode
        textScale: hostWindow.textScale
        textColor: hostWindow.textColor
        strongTextColor: hostWindow.strongTextColor
        containerWidth: paneRoot.width
        containerHeight: paneRoot.height

        onKeepRequested: backend.keepExternalVersion(paneRoot.paneIndex)
        onReloadRequested: backend.reloadFromDisk(paneRoot.paneIndex)
    }

    Connections {
        target: backend

        function onOpenDialogRequested(pane) {
            if (pane !== paneRoot.paneIndex)
                return;
            openFileDialog.open();
        }

        function onSaveDialogRequested(pane, suggestedUrl) {
            if (pane !== paneRoot.paneIndex)
                return;
            saveFileDialog.selectedFile = suggestedUrl;
            saveFileDialog.open();
        }

        function onSaveSucceeded(pane) {
            if (pane === hostWindow.awaitingPendingSave) {
                hostWindow.awaitingPendingSave = -1;
                hostWindow.finishPendingTabClose(pane);
            }
        }

        function onExternalChangeDetected(pane, deleted, locallyModified) {
            if (pane !== paneRoot.paneIndex)
                return;
            externalChangeDialog.deleted = deleted;
            externalChangeDialog.locallyModified = locallyModified;
            externalChangeDialog.open();
        }

        function onTabCloseNeedsConfirmation(pane, index) {
            if (pane !== paneRoot.paneIndex)
                return;
            hostWindow.pendingCloseTabIndex = index;
            if (index !== paneRoot.info.activeTabIndex)
                backend.switchTab(paneRoot.paneIndex, index);
            unsavedChangesDialog.open();
        }
    }

    Dialogs.FileDialog {
        id: openFileDialog
        title: "Open File"
        fileMode: Dialogs.FileDialog.OpenFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: backend.open(paneRoot.paneIndex, selectedFile)
    }

    Dialogs.FileDialog {
        id: saveFileDialog
        title: "Save File"
        fileMode: Dialogs.FileDialog.SaveFile
        nameFilters: ["Markdown files (*.md *.markdown)", "All files (*)"]
        onAccepted: backend.saveAs(paneRoot.paneIndex, selectedFile)
        onRejected: {
            backend.fileDialogCanceled(paneRoot.paneIndex);
            hostWindow.awaitingPendingSave = -1;
            hostWindow.pendingCloseTabIndex = -1;
            hostWindow.closingForQuit = false;
        }
    }
}
