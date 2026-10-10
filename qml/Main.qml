// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Controls
import QtQuick.Dialogs
import QtQuick.Layouts
import FishUI as FishUI
import Cutefish.TextEditor

FishUI.Window {
    id: root
    objectName: "editorWindow"
    title: currentPage ? currentPage.title : qsTr("Text Editor")
    width: 900
    height: 640
    minimumWidth: 480
    minimumHeight: 320
    header.height: 40
    windowButtonsAlignment: Qt.AlignVCenter
    windowButtonsTopMargin: 0
    windowButtonsRightMargin: 4
    blurEnabled: false
    background.color: Themes.background(Prefs.colorScheme, FishUI.Theme.darkMode)
    background.opacity: 1

    // Tabs to open with, from a restored session: maps as sessionTabs() makes them.
    property var initialTabs: []
    property int initialIndex: 0
    // A tab pulled out of another window, which this window opens with.
    property Item detachedPage: null

    readonly property Item currentPage: _tabView.currentItem
    readonly property int tabCount: _tabView.count
    readonly property string tabMimeType: "application/x-cutefish-text-editor-tab"
    property bool detaching: false

    // Zoom is per window and not kept.
    property real zoom: 1
    readonly property font editorFont: {
        const system = App.systemFont
        const size = Prefs.fontSize > 0 ? Prefs.fontSize : (system.pointSizeF > 0 ? system.pointSizeF : 10)
        return Qt.font({ family: Prefs.fontFamily.length > 0 ? Prefs.fontFamily : system.family,
                         pointSize: Math.max(4, Math.round(size * zoom * 2) / 2) })
    }

    Binding {
        target: App
        property: "restoreSession"
        value: Prefs.restoreSession
    }

    function zoomBy(step) {
        const steps = [0.5, 0.67, 0.8, 0.9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 3]
        if (step === 0) {
            zoom = 1
            return
        }
        let index = steps.findIndex(value => value >= zoom - 0.001)
        if (index < 0)
            index = steps.length - 1
        index = Math.max(0, Math.min(steps.length - 1, index + step))
        zoom = steps[index]
    }

    // Pages ------------------------------------------------------------------

    function pages() {
        const result = []
        for (let i = 0; i < _tabView.contentModel.count; ++i)
            result.push(_tabView.contentModel.get(i))
        return result
    }

    function indexOfPage(page) {
        for (let i = 0; i < _tabView.contentModel.count; ++i) {
            if (_tabView.contentModel.get(i) === page)
                return i
        }
        return -1
    }

    function newTab(properties, index) {
        const component = Qt.createComponent("qrc:/qml/EditorPage.qml")
        if (component.status !== Component.Ready) {
            console.warn(component.errorString())
            return null
        }
        const props = Object.assign({ root: root }, properties || {})
        const page = _tabView.addTab(component, props, index)
        App.scheduleSessionSave()
        return page
    }

    function selectTab(index) {
        if (index < 0 || index >= _tabView.count)
            return
        _tabView.currentIndex = index
        _tabView.currentItem.focusEditor()
    }

    function cycleTab(step) {
        if (_tabView.count > 1)
            selectTab((_tabView.currentIndex + step + _tabView.count) % _tabView.count)
    }

    function moveTab(from, to) {
        if (from === to || from < 0 || to < 0 || from >= _tabView.count || to >= _tabView.count)
            return
        const page = _tabView.contentModel.get(from)
        _tabView.moveItem(from, to)
        _tabView.currentIndex = indexOfPage(page)
        page.focusEditor()
        App.scheduleSessionSave()
    }

    // Called by the application -----------------------------------------------

    function indexOfPath(absolute, canonical) {
        const list = pages()
        for (let i = 0; i < list.length; ++i) {
            const path = list[i].document.filePath
            if (path.length > 0 && (path === absolute || path === canonical))
                return i
        }
        return -1
    }

    // A pristine untitled tab is reused instead of left behind.
    function isPristine(page) {
        return page && page.document.untitled && page.document.empty && !page.document.modified
                && !page.document.loading
    }

    function openFile(path, line) {
        const existing = indexOfPath(path, path)
        if (existing >= 0) {
            selectTab(existing)
            if (line > 0)
                currentPage.document.goToLine(line, 1)
            return
        }
        if (isPristine(currentPage)) {
            currentPage.open(path, line)
        } else {
            newTab({ initialPath: path, initialLine: line > 0 ? line - 1 : 0 })
        }
        if (Prefs.lastFolder !== currentPage.document.folder)
            Prefs.lastFolder = path.substring(0, path.lastIndexOf("/"))
    }

    function openText(text) {
        newTab({ initialText: text })
    }

    function sessionTabs() {
        return pages().map(page => page.sessionState())
    }

    function sessionCurrentIndex() {
        return _tabView.currentIndex
    }

    // Closing ------------------------------------------------------------------

    // Asks about unsaved pages, then runs `action` unless the user cancels.
    function confirmDiscard(dirty, action) {
        if (dirty.length === 0) {
            action()
            return
        }
        if (!_saveDialog)
            _saveDialog = _saveDialogComponent.createObject(root)
        _saveDialog.ask(dirty, action)
    }

    function closeTab(index, ask) {
        const page = _tabView.contentModel.get(index)
        if (!page)
            return
        if (ask !== false && page.document.modified) {
            confirmDiscard([page], () => closeTab(indexOfPage(page), false))
            return
        }
        // Kept for Reopen Closed Tab; an unsaved text stays in its draft.
        if (!isPristine(page)) {
            const state = page.sessionState()
            if (page.document.modified || (page.document.untitled && !page.document.empty)) {
                page.document.writeDraftNow()
                state.unsaved = true
            } else {
                page.document.removeDraft()
            }
            App.pushClosedTab(state)
        }
        _tabView.closeTab(index)
        App.scheduleSessionSave()
        if (_tabView.count === 0) {
            _closeConfirmed = true
            root.close()
        }
    }

    function closeCurrentTab() {
        closeTab(_tabView.currentIndex)
    }

    function closeOtherTabs(index) {
        const keep = _tabView.contentModel.get(index)
        const others = pages().filter(page => page !== keep)
        confirmDiscard(others.filter(page => page.document.modified), () => {
            for (const page of others)
                closeTab(indexOfPage(page), false)
            _tabView.currentIndex = indexOfPage(keep)
        })
    }

    function closeTabsToRight(index) {
        const right = pages().slice(index + 1)
        confirmDiscard(right.filter(page => page.document.modified), () => {
            for (const page of right)
                closeTab(indexOfPage(page), false)
        })
    }

    function reopenClosedTab() {
        const state = App.takeClosedTab()
        if (!state.draftId)
            return
        if (state.path && indexOfPath(state.path, state.path) >= 0) {
            selectTab(indexOfPath(state.path, state.path))
            return
        }
        newTab({ restoreState: state })
    }

    function quitApplication() {
        if (App.restoreSession) {
            App.quit()
            return
        }
        let dirty = []
        for (const window of App.windowList())
            dirty = dirty.concat(window.pages().filter(page => page.document.modified))
        confirmDiscard(dirty, () => App.quit())
    }

    property bool _closeConfirmed: false
    property bool _quitting: false

    onClosing: (close) => {
        if (App.quitting)
            return
        if (_closeConfirmed) {
            App.releaseWindow(root)
            return
        }
        close.accepted = false
        // The last window going is the application quitting.
        if (App.windowCount <= 1) {
            quitApplication()
            return
        }
        confirmDiscard(pages().filter(page => page.document.modified), () => {
            for (const page of pages())
                page.document.removeDraft()
            _closeConfirmed = true
            root.close()
        })
    }

    // Saving -------------------------------------------------------------------

    // Saves `page`, through Save As when it has no file; `done(saved, cancelled)` follows.
    function savePage(page, done) {
        if (page.document.untitled || !page.document.writable && !App.fileExists(page.document.filePath)) {
            saveAs(page, done)
            return
        }
        const saved = page.document.save()
        if (!saved && page.document.notice === Document.ReadOnly) {
            saveAs(page, done)
            return
        }
        if (done)
            done(saved)
    }

    function saveAs(page, done) {
        _saveFileDialog.page = page
        _saveFileDialog.done = done || null
        const folder = page.document.untitled ? (Prefs.lastFolder.length > 0 ? Prefs.lastFolder : "")
                                              : page.document.filePath.substring(0, page.document.filePath.lastIndexOf("/"))
        if (folder.length > 0)
            _saveFileDialog.currentFolder = App.pathToUrl(folder)
        const name = page.document.untitled ? page.document.title.replace(/[\/\\:]/g, " ").trim() || qsTr("Untitled")
                                            : page.document.fileName
        _saveFileDialog.selectedFile = App.pathToUrl((folder.length > 0 ? folder : "") + "/"
                                                     + (page.document.untitled && name.indexOf(".") < 0 ? name + ".txt" : name))
        _saveFileDialog.open()
    }

    function saveAll() {
        const dirty = pages().filter(page => page.document.modified)
        // A failed save shows its notice on its page; only a cancelled dialog stops the rest.
        const next = () => {
            const page = dirty.shift()
            if (page)
                savePage(page, (saved, cancelled) => { if (!cancelled) next() })
        }
        next()
    }

    function openDialog() {
        if (currentPage && !currentPage.document.untitled)
            _openDialog.currentFolder = App.pathToUrl(currentPage.document.filePath.substring(0, currentPage.document.filePath.lastIndexOf("/")))
        else if (Prefs.lastFolder.length > 0)
            _openDialog.currentFolder = App.pathToUrl(Prefs.lastFolder)
        _openDialog.open()
    }

    FileDialog {
        id: _openDialog
        title: qsTr("Open")
        fileMode: FileDialog.OpenFiles
        onAccepted: App.openPaths(selectedFiles.map(url => App.urlToPath(url)), 0, root)
    }

    FileDialog {
        id: _saveFileDialog
        property Item page: null
        property var done: null
        title: qsTr("Save As")
        fileMode: FileDialog.SaveFile
        // `done` may open this dialog again for the next page, so it runs last.
        onAccepted: {
            const path = App.urlToPath(selectedFile)
            const saved = page && page.document.saveAs(path)
            if (saved)
                Prefs.lastFolder = path.substring(0, path.lastIndexOf("/"))
            const finished = done
            page = null
            done = null
            if (finished)
                finished(saved)
        }
        onRejected: {
            const finished = done
            page = null
            done = null
            if (finished)
                finished(false, true)
        }
    }

    property QtObject _saveDialog: null

    Component {
        id: _saveDialogComponent

        SaveChangesDialog {
            parentWindow: root
            onSaveRequested: (page, done) => root.savePage(page, done)
        }
    }

    // Header -------------------------------------------------------------------

    headerItem: Item {
        id: _header

        EditorTabBar {
            id: _tabBar
            view: _tabView
            anchors.left: parent.left
            anchors.right: _menuButton.left
            // The terminal's inset.
            anchors.leftMargin: FishUI.Units.smallSpacing
            // Past the new tab button, a stretch of title bar stays free to move the window by.
            anchors.rightMargin: 4 + _newTabButton.width + 40
            anchors.verticalCenter: parent.verticalCenter

            onCloseRequested: (index) => root.closeTab(index)
            onCloseOthersRequested: (index) => root.closeOtherTabs(index)
            onCloseToRightRequested: (index) => root.closeTabsToRight(index)
            onMoveRequested: (from, to) => root.moveTab(from, to)
            onNewWindowRequested: (index) => Qt.callLater(root.moveToNewWindow, index)
            onDetachRequested: (index, position) => Qt.callLater(root.detachTab, index, position)
        }

        // Tabs dragged from another window of this process, and files.
        DropArea {
            anchors.fill: parent
            keys: [root.tabMimeType, "text/uri-list"]

            onEntered: (drag) => {
                if (drag.hasUrls) {
                    drag.accepted = true
                    return
                }
                drag.accepted = drag.source !== root && drag.source && drag.source.tabMimeType === root.tabMimeType
                if (drag.accepted)
                    _tabBar.beginIncoming(drag.source.currentPage ? drag.source.currentPage.title : "",
                                          mapToItem(_tabBar, drag.x, drag.y).x)
            }
            onPositionChanged: (drag) => {
                if (_tabBar.incoming)
                    _tabBar.incomingX = mapToItem(_tabBar, drag.x, drag.y).x
            }
            onExited: _tabBar.endIncoming(false)
            onDropped: (drop) => {
                if (drop.hasUrls && !_tabBar.incoming) {
                    root.openUrls(drop.urls)
                    drop.accept(Qt.CopyAction)
                    return
                }
                const index = _tabBar.incoming ? _tabBar.incomingIndex : _tabView.count
                _tabBar.endIncoming(true)
                root.insertTab(drop.source.takePage(), index)
                root.requestActivate()
                drop.accept(Qt.MoveAction)
            }
        }

        FishUI.HeaderButton {
            id: _newTabButton
            objectName: "newTabButton"
            // Follows the last tab.
            x: _tabBar.x + _tabBar.trackWidth + 4
            anchors.verticalCenter: parent.verticalCenter
            size: 32
            artwork: "add"
            onClicked: root.newTab({})
            Accessible.name: qsTr("New Tab")
        }

        FishUI.HeaderButton {
            id: _menuButton
            objectName: "menuButton"
            anchors.right: parent.right
            anchors.rightMargin: root.windowButtonsSpacing
            anchors.verticalCenter: parent.verticalCenter
            size: 32
            artwork: "menu"
            checked: _mainMenu.visible
            onClicked: _mainMenu.showDropDown(_menuButton, 4)
            Accessible.name: qsTr("Main Menu")
        }
    }

    function openUrls(urls) {
        const paths = []
        for (const url of urls) {
            const path = App.urlToPath(url)
            if (path.length > 0 && path.indexOf("://") < 0)
                paths.push(path)
        }
        if (paths.length > 0)
            App.openPaths(paths, 0, root)
    }

    FishUI.TabView {
        id: _tabView
        anchors.fill: parent
        onCurrentIndexChanged: App.scheduleSessionSave()
    }

    // Moving tabs between windows ----------------------------------------------

    function insertTab(page, index) {
        _tabView.insertTab(page, index)
        page.root = root
        page.focusEditor()
        App.scheduleSessionSave()
    }

    function takePage() {
        const page = _tabView.takeTab(0)
        root.hide()
        return page
    }

    function moveToNewWindow(index) {
        if (index < 0 || index >= _tabView.count || _tabView.count < 2)
            return
        const page = _tabView.takeTab(index)
        const window = App.newWindow({ detachedPage: page, width: root.width, height: root.height,
                                       restorationBehavior: FishUI.Window.Disabled })
        if (!window)
            insertTab(page, index)
    }

    function detachTab(index, position) {
        if (index < 0 || index >= _tabView.count || _tabView.count < 2)
            return
        detaching = true
        const page = _tabView.takeTab(index)
        const window = App.newWindow({ detachedPage: page, width: root.width, height: root.height,
                                       restorationBehavior: FishUI.Window.Disabled, zoom: root.zoom })
        if (!window) {
            detaching = false
            insertTab(page, index)
            return
        }
        const offset = Qt.point(Math.min(position.x, window.width - 1), window.header.height / 2)
        const action = FishUI.TabTransfer.drag(window, offset, tabMimeType)
        detaching = false
        if (action === Qt.IgnoreAction && window.tabCount === 1)
            insertTab(window.takePage(), index)
        if (window.tabCount === 0) {
            window._closeConfirmed = true
            window.close()
        } else {
            window.requestActivate()
        }
    }

    // Menus ----------------------------------------------------------------------

    MainMenu {
        id: _mainMenu
        window: root
    }

    property QtObject _preferences: null

    function showPreferences() {
        if (!_preferences)
            _preferences = Qt.createComponent("qrc:/qml/PreferencesDialog.qml").createObject(root, { parentWindow: root })
        _preferences.show()
        _preferences.raise()
        _preferences.requestActivate()
    }

    property QtObject _shortcuts: null

    function showShortcuts() {
        if (!_shortcuts) {
            const component = Qt.createComponent("qrc:/qml/ShortcutsDialog.qml")
            if (component.status !== Component.Ready) {
                console.warn(component.errorString())
                return
            }
            _shortcuts = component.createObject(root, { parentWindow: root })
        }
        _shortcuts.open()
    }

    property QtObject _statistics: null

    function showStatistics() {
        if (!currentPage)
            return
        if (!_statistics)
            _statistics = _statisticsComponent.createObject(root)
        const stats = currentPage.document.statistics()
        let text = qsTr("Lines: %1").arg(stats.lines.toLocaleString(Qt.locale(), "f", 0)) + "\n"
                + qsTr("Words: %1").arg(stats.words.toLocaleString(Qt.locale(), "f", 0)) + "\n"
                + qsTr("Characters: %1").arg(stats.characters.toLocaleString(Qt.locale(), "f", 0)) + "\n"
                + qsTr("Characters without spaces: %1").arg(stats.charactersWithoutSpaces.toLocaleString(Qt.locale(), "f", 0))
        if (stats.selectionCharacters !== undefined)
            text += "\n\n" + qsTr("Selection: %1 lines, %2 words, %3 characters")
                    .arg(stats.selectionLines).arg(stats.selectionWords).arg(stats.selectionCharacters)
        _statistics.title = currentPage.title
        _statistics.text = text
        _statistics.open()
    }

    Component {
        id: _statisticsComponent

        FishUI.ConfirmDialog {
            parentWindow: root
            showOnCompleted: false
            cancelButtonVisible: false
            confirmText: qsTr("OK")
        }
    }

    property QtObject _about: null

    function showAbout() {
        if (!_about)
            _about = _aboutComponent.createObject(root)
        _about.show()
    }

    Component {
        id: _aboutComponent

        FishUI.AboutDialog {
            parentWindow: root
            iconSource: "image://icontheme/accessories-text-editor"
            name: qsTr("Text Editor")
            description: qsTr("A simple editor for plain text")
            link: "https://github.com/cutefishos/text-editor"
        }
    }

    // Shortcuts ------------------------------------------------------------------

    Shortcut { sequences: ["Ctrl+T"]; onActivated: root.newTab({}) }
    Shortcut { sequences: ["Ctrl+N"]; onActivated: App.newWindow({}) }
    Shortcut { sequences: [StandardKey.Open]; onActivated: root.openDialog() }
    Shortcut { sequences: [StandardKey.Save]; onActivated: if (root.currentPage) root.savePage(root.currentPage) }
    Shortcut { sequences: ["Ctrl+Shift+S"]; onActivated: if (root.currentPage) root.saveAs(root.currentPage) }
    Shortcut { sequences: ["Ctrl+Alt+S"]; onActivated: root.saveAll() }
    Shortcut { sequences: ["Ctrl+W"]; onActivated: root.closeCurrentTab() }
    Shortcut { sequences: ["Ctrl+Shift+W"]; onActivated: root.close() }
    Shortcut { sequences: ["Ctrl+Q"]; onActivated: root.quitApplication() }
    Shortcut { sequences: ["Ctrl+Shift+T"]; onActivated: root.reopenClosedTab() }
    Shortcut { sequences: ["Ctrl+PgDown", "Ctrl+Tab"]; onActivated: root.cycleTab(1) }
    Shortcut { sequences: ["Ctrl+PgUp", "Ctrl+Shift+Tab", "Ctrl+Backtab"]; onActivated: root.cycleTab(-1) }
    Shortcut { sequences: ["Ctrl+Shift+PgDown"]; onActivated: root.moveTab(_tabView.currentIndex, _tabView.currentIndex + 1) }
    Shortcut { sequences: ["Ctrl+Shift+PgUp"]; onActivated: root.moveTab(_tabView.currentIndex, _tabView.currentIndex - 1) }

    Repeater {
        model: 9

        Item {
            Shortcut {
                sequence: "Alt+" + (index + 1)
                onActivated: root.selectTab(index === 8 ? _tabView.count - 1 : index)
            }
        }
    }

    Shortcut { sequences: [StandardKey.Find]; onActivated: if (root.currentPage) root.currentPage.showFind(false) }
    Shortcut { sequences: ["Ctrl+H"]; onActivated: if (root.currentPage) root.currentPage.showFind(true) }
    Shortcut { sequences: ["F3", "Ctrl+G"]; onActivated: if (root.currentPage) root.currentPage.findNext(false) }
    Shortcut { sequences: ["Shift+F3", "Ctrl+Shift+G"]; onActivated: if (root.currentPage) root.currentPage.findNext(true) }
    Shortcut { sequences: ["Ctrl+I", "Ctrl+L"]; onActivated: if (root.currentPage) root.currentPage.showGoToLine() }
    Shortcut { sequences: ["Ctrl+=", "Ctrl++", StandardKey.ZoomIn]; onActivated: root.zoomBy(1) }
    Shortcut { sequences: ["Ctrl+-", StandardKey.ZoomOut]; onActivated: root.zoomBy(-1) }
    Shortcut { sequences: ["Ctrl+0"]; onActivated: root.zoomBy(0) }
    Shortcut { sequences: ["Alt+W"]; onActivated: Prefs.wordWrap = !Prefs.wordWrap }
    Shortcut { sequences: ["F11"]; onActivated: root.visibility === Window.FullScreen ? root.showNormal() : root.showFullScreen() }
    Shortcut { sequences: ["Ctrl+,"]; onActivated: root.showPreferences() }
    Shortcut { sequences: ["Ctrl+?", "Ctrl+Shift+/"]; onActivated: root.showShortcuts() }
    Shortcut { sequences: ["Ctrl+D"]; onActivated: if (root.currentPage) root.currentPage.document.deleteLines() }
    Shortcut { sequences: ["Ctrl+Alt+D"]; onActivated: if (root.currentPage) root.currentPage.document.duplicateLines() }
    Shortcut { sequences: ["Ctrl+U"]; onActivated: if (root.currentPage) root.currentPage.document.changeCase(Document.Upper) }
    Shortcut { sequences: ["Ctrl+Shift+U"]; onActivated: if (root.currentPage) root.currentPage.document.changeCase(Document.Lower) }

    onActiveChanged: {
        if (active && currentPage)
            currentPage.document.checkDisk()
    }

    Component.onCompleted: {
        if (detachedPage) {
            insertTab(detachedPage, 0)
            detachedPage = null
            return
        }
        for (const state of initialTabs)
            newTab({ restoreState: state })
        if (_tabView.count === 0)
            newTab({})
        selectTab(Math.max(0, Math.min(initialIndex, _tabView.count - 1)))
    }
}
