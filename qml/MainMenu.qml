// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import FishUI as FishUI
import Cutefish.TextEditor

// The header's menu: files, finding, editing and view options.
FishUI.DesktopMenu {
    id: control
    objectName: "mainMenu"

    property var window
    readonly property var page: window ? window.currentPage : null
    readonly property Document document: page ? page.document : null

    // Let the compositor deliver pointer events to the submenu under the cursor.
    pointerGrabEnabled: false

    FishUI.MenuItem {
        text: qsTr("Open…")
        shortcut: StandardKey.Open
        onTriggered: control.window.openDialog()
    }

    FishUI.MenuItem {
        objectName: "openRecentItem"
        text: qsTr("Open Recent")
        enabled: App.recentFiles.files.length > 0
        childMenu: control._recentMenu
    }

    FishUI.MenuSeparator {}

    FishUI.MenuItem {
        text: qsTr("New Tab")
        shortcut: "Ctrl+T"
        onTriggered: control.window.newTab({})
    }

    FishUI.MenuItem {
        text: qsTr("New Window")
        shortcut: "Ctrl+N"
        onTriggered: App.newWindow({})
    }

    FishUI.MenuItem {
        text: qsTr("Reopen Closed Tab")
        shortcut: "Ctrl+Shift+T"
        enabled: App.canReopenClosedTab
        onTriggered: control.window.reopenClosedTab()
    }

    FishUI.MenuSeparator {}

    FishUI.MenuItem {
        text: qsTr("Save")
        shortcut: StandardKey.Save
        enabled: !!control.document
        onTriggered: control.window.savePage(control.page)
    }

    FishUI.MenuItem {
        text: qsTr("Save As…")
        shortcut: "Ctrl+Shift+S"
        enabled: !!control.document
        onTriggered: control.window.saveAs(control.page)
    }

    FishUI.MenuItem {
        text: qsTr("Save All")
        shortcut: "Ctrl+Alt+S"
        onTriggered: control.window.saveAll()
    }

    FishUI.MenuItem {
        text: qsTr("Revert to Saved…")
        enabled: !!control.document && !control.document.untitled && control.document.modified
        onTriggered: control.page.revert()
    }

    FishUI.MenuItem {
        text: qsTr("Close Tab")
        shortcut: "Ctrl+W"
        onTriggered: control.window.closeCurrentTab()
    }

    FishUI.MenuSeparator {}

    FishUI.MenuItem {
        text: qsTr("Find…")
        shortcut: StandardKey.Find
        onTriggered: control.page.showFind(false)
    }

    FishUI.MenuItem {
        text: qsTr("Replace…")
        shortcut: "Ctrl+H"
        onTriggered: control.page.showFind(true)
    }

    FishUI.MenuItem {
        text: qsTr("Go to Line…")
        shortcut: "Ctrl+I"
        onTriggered: control.page.showGoToLine()
    }

    FishUI.MenuSeparator {}

    FishUI.MenuItem {
        text: qsTr("Edit")
        childMenu: control._editMenu
    }

    FishUI.MenuItem {
        text: qsTr("View")
        childMenu: control._viewMenu
    }

    FishUI.MenuSeparator {}

    FishUI.MenuItem {
        text: qsTr("Document Statistics")
        onTriggered: control.window.showStatistics()
    }

    FishUI.MenuSeparator {}

    FishUI.MenuItem {
        text: qsTr("Settings")
        shortcut: "Ctrl+,"
        onTriggered: control.window.showPreferences()
    }

    FishUI.MenuItem {
        text: qsTr("Keyboard Shortcuts")
        shortcut: "Ctrl+?"
        onTriggered: control.window.showShortcuts()
    }

    FishUI.MenuItem {
        text: qsTr("About Text Editor")
        onTriggered: control.window.showAbout()
    }

    property FishUI.DesktopMenu _recentMenu: FishUI.DesktopMenu {
        Repeater {
            model: App.recentFiles.files

            FishUI.MenuItem {
                text: modelData.substring(modelData.lastIndexOf("/") + 1)
                onTriggered: App.openPaths([modelData], 0, control.window)
            }
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Clear Recent Files")
            onTriggered: App.recentFiles.clear()
        }
    }

    property FishUI.DesktopMenu _editMenu: FishUI.DesktopMenu {

        FishUI.MenuItem {
            text: qsTr("Undo")
            shortcut: StandardKey.Undo
            enabled: control.document && control.document.canUndo
            onTriggered: control.document.undo()
        }

        FishUI.MenuItem {
            text: qsTr("Redo")
            shortcut: "Ctrl+Shift+Z"
            enabled: control.document && control.document.canRedo
            onTriggered: control.document.redo()
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Duplicate Line")
            shortcut: "Ctrl+Alt+D"
            onTriggered: control.document.duplicateLines()
        }

        FishUI.MenuItem {
            text: qsTr("Delete Line")
            shortcut: "Ctrl+D"
            onTriggered: control.document.deleteLines()
        }

        FishUI.MenuItem {
            text: qsTr("Move Line Up")
            shortcut: "Alt+Up"
            onTriggered: control.document.moveLines(true)
        }

        FishUI.MenuItem {
            text: qsTr("Move Line Down")
            shortcut: "Alt+Down"
            onTriggered: control.document.moveLines(false)
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("UPPERCASE")
            shortcut: "Ctrl+U"
            onTriggered: control.document.changeCase(Document.Upper)
        }

        FishUI.MenuItem {
            text: qsTr("lowercase")
            shortcut: "Ctrl+Shift+U"
            onTriggered: control.document.changeCase(Document.Lower)
        }

        FishUI.MenuItem {
            text: qsTr("Title Case")
            onTriggered: control.document.changeCase(Document.Title)
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Indent")
            shortcut: "Tab"
            onTriggered: control.document.indentSelection(false)
        }

        FishUI.MenuItem {
            text: qsTr("Unindent")
            shortcut: "Shift+Tab"
            onTriggered: control.document.indentSelection(true)
        }
    }

    property FishUI.DesktopMenu _viewMenu: FishUI.DesktopMenu {

        FishUI.MenuItem {
            text: qsTr("Word Wrap")
            shortcut: "Alt+W"
            checked: Prefs.wordWrap
            onTriggered: Prefs.wordWrap = !Prefs.wordWrap
        }

        FishUI.MenuItem {
            text: qsTr("Line Numbers")
            checked: Prefs.lineNumbers
            onTriggered: Prefs.lineNumbers = !Prefs.lineNumbers
        }

        FishUI.MenuItem {
            text: qsTr("Highlight Current Line")
            checked: Prefs.highlightCurrentLine
            onTriggered: Prefs.highlightCurrentLine = !Prefs.highlightCurrentLine
        }

        FishUI.MenuItem {
            text: qsTr("Show Whitespace")
            checked: Prefs.showWhitespace
            onTriggered: Prefs.showWhitespace = !Prefs.showWhitespace
        }

        FishUI.MenuItem {
            text: qsTr("Match Brackets")
            checked: Prefs.matchBrackets
            onTriggered: Prefs.matchBrackets = !Prefs.matchBrackets
        }

        FishUI.MenuSeparator {}

        Repeater {
            model: Themes.schemes

            FishUI.MenuItem {
                text: modelData.name
                checked: Prefs.colorScheme === modelData.id
                onTriggered: Prefs.colorScheme = modelData.id
            }
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Zoom In")
            shortcut: "Ctrl+="
            onTriggered: control.window.zoomBy(1)
        }

        FishUI.MenuItem {
            text: qsTr("Zoom Out")
            shortcut: "Ctrl+-"
            onTriggered: control.window.zoomBy(-1)
        }

        FishUI.MenuItem {
            text: qsTr("Actual Size")
            shortcut: "Ctrl+0"
            enabled: control.window && control.window.zoom !== 1
            onTriggered: control.window.zoomBy(0)
        }

        FishUI.MenuItem {
            text: qsTr("Full Screen")
            shortcut: "F11"
            checked: control.window && control.window.isFullScreen
            onTriggered: control.window.isFullScreen ? control.window.showNormal() : control.window.showFullScreen()
        }
    }
}
