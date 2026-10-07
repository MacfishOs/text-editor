// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import FishUI as FishUI
import Cutefish.TextEditor

// One tab: a document, the bars that belong to it and its view.
FocusScope {
    id: page

    property var root
    // How the tab starts: a file, text from standard input, or a saved state.
    property string initialPath: ""
    property int initialLine: 0
    property var initialText: undefined
    property var restoreState: null

    readonly property Document document: _document
    readonly property alias editor: _editor
    readonly property string title: document.title
    readonly property bool modified: document.modified
    // Chosen by the user, so kept in the session.
    property string chosenEncoding: ""
    property string chosenLanguage: ""


    property Document _document: App.createDocument(page)
    readonly property bool darkSurface: {
        const c = _editor.backgroundColor
        return 0.2126 * c.r + 0.7152 * c.g + 0.0722 * c.b < 0.5
    }

    // The card floating over the text, if one is open.
    readonly property Item card: _findBar.visible ? _findBar : _goToLine.visible ? _goToLine : null
    // From the editor's edges, as the other margins of the window.
    readonly property int cardInset: 12

    // Scrolled above the first line for a card that is gone: back to it.
    function settleScroll() {
        if (_flick.contentY < 0)
            _flick.contentY = 0
    }

    function focusEditor() {
        _editor.forceActiveFocus()
    }

    function open(path, line) {
        initialLine = line > 0 ? line - 1 : 0
        _pendingCursor = { line: initialLine, column: 0, scroll: -1 }
        document.load(path)
    }

    function sessionState() {
        return {
            path: document.filePath,
            draftId: document.draftId,
            unsaved: document.modified || (document.untitled && !document.empty),
            line: document.cursorLine,
            column: document.cursorColumn,
            scroll: _editor.topLine(),
            encoding: chosenEncoding,
            language: chosenLanguage
        }
    }

    function showFind(replace) {
        _goToLine.close()
        _findBar.open(document.hasSelection && document.selectionLength < 200 && document.selectedText().indexOf("\n") < 0
                      ? document.selectedText() : "", replace)
    }

    function findNext(backwards) {
        if (!_findBar.visible && document.searchText.length === 0) {
            showFind(false)
            return
        }
        _findBar.find(backwards)
    }

    function showGoToLine() {
        _findBar.close(false)
        _goToLine.open()
    }

    function setEncodingForReload(encoding) {
        chosenEncoding = encoding
        if (document.modified && !document.untitled) {
            _reloadConfirm.encoding = encoding
            _reloadConfirm.open()
            return
        }
        document.reloadWithEncoding(encoding)
    }

    function showEncodingMenu() {
        _statusBar.openEncodingMenu()
    }

    function revert() {
        if (!document.modified) {
            document.reload()
            return
        }
        _reloadConfirm.encoding = ""
        _reloadConfirm.open()
    }

    // Where the cursor and the view go once the file is loaded.
    property var _pendingCursor: null

    Connections {
        target: page.document

        function onLoaded() {
            if (!page._pendingCursor)
                return
            const state = page._pendingCursor
            page._pendingCursor = null
            page.document.goToLine(state.line + 1, state.column + 1)
            if (state.scroll >= 0)
                Qt.callLater(() => _editor.scrollToLine(state.scroll))
            else
                Qt.callLater(() => _editor.ensureCursorVisible(true))
        }

        function onModifiedChanged() {
            App.scheduleSessionSave()
        }
    }

    Component.onCompleted: {
        document.setIndentationDefaults(Prefs.insertSpaces, Prefs.tabWidth)
        if (restoreState) {
            if (restoreState.draftId)
                document.draftId = restoreState.draftId
            chosenEncoding = restoreState.encoding || ""
            chosenLanguage = restoreState.language || ""
            if (chosenLanguage.length > 0)
                document.language = chosenLanguage
            _pendingCursor = { line: restoreState.line || 0, column: restoreState.column || 0,
                               scroll: restoreState.scroll || 0 }
            if (restoreState.path && restoreState.path.length > 0) {
                document.load(restoreState.path, chosenEncoding)
                if (restoreState.unsaved)
                    document.restoreDraft()
            } else if (restoreState.unsaved) {
                document.restoreDraft()
                _pendingCursor = null
                document.goToLine(restoreState.line + 1, restoreState.column + 1)
                Qt.callLater(() => _editor.scrollToLine(restoreState.scroll || 0))
            }
        } else if (initialPath.length > 0) {
            _pendingCursor = { line: initialLine, column: 0, scroll: -1 }
            document.load(initialPath)
        } else if (initialText !== undefined) {
            document.setText(initialText)
            document.goToLine(1, 1)
        }
        focusEditor()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        InfoBar {
            Layout.fillWidth: true
            page: page
        }

        Item {
            id: _editorArea
            Layout.fillWidth: true
            Layout.fillHeight: true

            Rectangle {
                anchors.fill: parent
                color: _editor.backgroundColor
            }

            Flickable {
                id: _flick
                anchors.fill: parent
                // The view handles presses itself; this only scrolls.
                interactive: false
                clip: true
                contentWidth: _editor.contentWidth
                contentHeight: _editor.contentHeight
                boundsBehavior: Flickable.StopAtBounds
                visible: !page.document.loading || _loadingDelay.running

                FishUI.WheelHandler {
                    target: _flick
                }

                ScrollBar.vertical: FishUI.ScrollBar {
                    darkMode: page.darkSurface
                }
                ScrollBar.horizontal: FishUI.ScrollBar {
                    darkMode: page.darkSurface
                    policy: Prefs.wordWrap ? ScrollBar.AlwaysOff : ScrollBar.AsNeeded
                }

                EditorView {
                    id: _editor
                    objectName: "editorView"
                    parent: _flick
                    anchors.fill: parent
                    focus: true
                    document: page.document
                    font: page.root ? page.root.editorFont : Qt.font({ family: "monospace", pointSize: 10 })
                    wordWrap: Prefs.wordWrap
                    lineNumbers: Prefs.lineNumbers
                    highlightCurrentLine: Prefs.highlightCurrentLine
                    showWhitespace: Prefs.showWhitespace
                    matchBrackets: Prefs.matchBrackets
                    autoIndent: Prefs.autoIndent
                    colorScheme: Prefs.colorScheme
                    darkMode: FishUI.Theme.darkMode
                    accentColor: FishUI.Theme.highlightColor
                    sidePadding: 14
                    contentX: _flick.contentX
                    contentY: _flick.contentY
                    occludedLeft: page.card ? page.card.x - 8 : 0
                    occludedHeight: page.card ? page.card.y + page.card.height + 6 : 0

                    onScrollRequested: (x, y) => {
                        _flick.contentX = x
                        _flick.contentY = y
                    }
                    onContextMenuRequested: (x, y) => _contextMenu.popup()
                    onZoomRequested: (step) => page.root.zoomBy(step)
                    onFilesDropped: (urls) => page.root.openUrls(urls)
                }
            }

            // A file that takes a moment shows its progress; a quick one shows nothing.
            Timer {
                id: _loadingDelay
                interval: 300
                running: page.document.loading
            }

            ColumnLayout {
                anchors.centerIn: parent
                width: Math.min(parent.width - 64, 320)
                spacing: 12
                visible: page.document.loading && !_loadingDelay.running

                FishUI.Label {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Opening “%1”…").arg(page.document.fileName)
                    elide: Text.ElideMiddle
                    Layout.maximumWidth: parent.width
                }

                FishUI.ProgressBar {
                    Layout.fillWidth: true
                    from: 0
                    to: 1
                    value: page.document.loadProgress
                }

                FishUI.Button {
                    Layout.alignment: Qt.AlignHCenter
                    text: qsTr("Cancel")
                    onClicked: {
                        page.document.cancelLoading()
                        page.root.closeTab(page.root.indexOfPage(page), false)
                    }
                }
            }

            FindBar {
                id: _findBar
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: page.cardInset
                width: Math.min(460, parent.width - page.cardInset * 2)
                z: 2
                page: page
                onVisibleChanged: if (!visible) page.settleScroll()
            }

            GoToLineBar {
                id: _goToLine
                anchors.top: parent.top
                anchors.right: parent.right
                anchors.margins: page.cardInset
                width: Math.min(implicitWidth, parent.width - page.cardInset * 2)
                z: 2
                page: page
                onVisibleChanged: if (!visible) page.settleScroll()
            }
        }

        StatusBar {
            id: _statusBar
            Layout.fillWidth: true
            page: page
        }
    }

    FishUI.DesktopMenu {
        id: _contextMenu
        pointerGrabEnabled: true

        FishUI.MenuItem {
            text: qsTr("Undo")
            shortcut: StandardKey.Undo
            enabled: page.document.canUndo
            onTriggered: page.document.undo()
        }

        FishUI.MenuItem {
            text: qsTr("Redo")
            shortcut: "Ctrl+Shift+Z"
            enabled: page.document.canRedo
            onTriggered: page.document.redo()
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Cut")
            shortcut: StandardKey.Cut
            enabled: page.document.hasSelection && !page.document.locked
            onTriggered: _editor.cut()
        }

        FishUI.MenuItem {
            text: qsTr("Copy")
            shortcut: StandardKey.Copy
            enabled: page.document.hasSelection
            onTriggered: _editor.copy()
        }

        FishUI.MenuItem {
            text: qsTr("Paste")
            shortcut: StandardKey.Paste
            enabled: !page.document.locked
            onTriggered: _editor.paste()
        }

        FishUI.MenuItem {
            text: qsTr("Delete")
            shortcut: StandardKey.Delete
            enabled: page.document.hasSelection && !page.document.locked
            onTriggered: page.document.deleteSelection()
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Select All")
            shortcut: StandardKey.SelectAll
            onTriggered: page.document.selectAll()
        }
    }

    FishUI.ConfirmDialog {
        id: _reloadConfirm
        property string encoding: ""
        parentWindow: page.root
        showOnCompleted: false
        title: encoding.length > 0 ? qsTr("Reopen with %1?").arg(App.encodingLabel(encoding))
                                   : qsTr("Revert “%1”?").arg(page.title)
        text: qsTr("Your unsaved changes will be lost.")
        confirmText: encoding.length > 0 ? qsTr("Reopen") : qsTr("Revert")
        destructive: true
        onAccepted: {
            if (encoding.length > 0)
                page.document.reloadWithEncoding(encoding)
            else
                page.document.reload()
        }
    }
}
