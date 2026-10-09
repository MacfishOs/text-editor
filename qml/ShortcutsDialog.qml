// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI

FishUI.DialogWindow {
    id: dialog
    objectName: "shortcutsDialog"

    // Each row: what it does, and its keys, shown as the platform writes them.
    readonly property var sections: [
        { title: qsTr("Files and Tabs"), keys: [
            [qsTr("New tab"), ["Ctrl+T"]],
            [qsTr("New window"), ["Ctrl+N"]],
            [qsTr("Open a file"), [StandardKey.Open]],
            [qsTr("Save, save as"), [StandardKey.Save, "Ctrl+Shift+S"]],
            [qsTr("Save all"), ["Ctrl+Alt+S"]],
            [qsTr("Close the tab, the window"), ["Ctrl+W", "Ctrl+Shift+W"]],
            [qsTr("Reopen the closed tab"), ["Ctrl+Shift+T"]],
            [qsTr("Next or previous tab"), ["Ctrl+PgDown", "Ctrl+PgUp"]],
            [qsTr("Move the tab"), ["Ctrl+Shift+PgDown", "Ctrl+Shift+PgUp"]],
            [qsTr("Go to tab 1 to 9"), ["Alt+1", "Alt+9"]],
            [qsTr("Quit"), ["Ctrl+Q"]]
        ] },
        { title: qsTr("Editing"), keys: [
            [qsTr("Undo, redo"), [StandardKey.Undo, "Ctrl+Shift+Z"]],
            [qsTr("Cut, copy, paste"), [StandardKey.Cut, StandardKey.Copy, StandardKey.Paste]],
            [qsTr("Delete the word before or after"), ["Ctrl+Backspace", "Ctrl+Del"]],
            [qsTr("Indent or unindent"), ["Tab", "Shift+Tab"]],
            [qsTr("Move the line up or down"), ["Alt+Up", "Alt+Down"]],
            [qsTr("Duplicate the line"), ["Ctrl+Alt+D"]],
            [qsTr("Delete the line"), ["Ctrl+D"]],
            [qsTr("Uppercase, lowercase"), ["Ctrl+U", "Ctrl+Shift+U"]],
            [qsTr("Overwrite mode"), ["Ins"]]
        ] },
        { title: qsTr("Find and Move"), keys: [
            [qsTr("Find, replace"), [StandardKey.Find, "Ctrl+H"]],
            [qsTr("Next or previous match"), ["F3", "Shift+F3"]],
            [qsTr("Go to line"), ["Ctrl+I", "Ctrl+L"]],
            [qsTr("Word left or right"), ["Ctrl+Left", "Ctrl+Right"]],
            [qsTr("Start or end of the document"), ["Ctrl+Home", "Ctrl+End"]]
        ] },
        { title: qsTr("View"), keys: [
            [qsTr("Zoom in, out, reset"), ["Ctrl+=", "Ctrl+-", "Ctrl+0"]],
            [qsTr("Word wrap"), ["Alt+W"]],
            [qsTr("Full screen"), ["F11"]],
            [qsTr("Settings"), ["Ctrl+,"]],
            [qsTr("Keyboard shortcuts"), ["Ctrl+?"]]
        ] }
    ]

    title: qsTr("Keyboard Shortcuts")
    width: 480
    contentHeight: 600 - header.height
    blurEnabled: false
    background.color: FishUI.Theme.backgroundColor
    background.opacity: 1

    Flickable {
        id: flickable
        anchors.fill: parent
        contentHeight: layout.implicitHeight + 20
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        FishUI.WheelHandler {
            target: flickable
        }
        FishUI.ScrollBar.vertical: FishUI.ScrollBar {}

        ColumnLayout {
            id: layout
            x: 16
            y: 4
            width: flickable.width - 32
            spacing: 12

            Repeater {
                model: dialog.sections

                ColumnLayout {
                    required property var modelData
                    Layout.fillWidth: true
                    spacing: 8

                    SectionLabel {
                        text: modelData.title
                    }

                    ListCard {
                        Repeater {
                            model: modelData.keys

                            InfoRow {
                                required property var modelData
                                text: modelData[0]
                                value: modelData[1].map(key => FishUI.KeySequence.nativeText(key)).join(", ")
                            }
                        }
                    }
                }
            }
        }
    }

    Shortcut {
        sequence: "Escape"
        onActivated: dialog.close()
    }
}
