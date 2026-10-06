// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI
import Cutefish.TextEditor

// One line under the text: where the cursor is, and the document's
// indentation, language, line endings and encoding, each a menu to change it.
Rectangle {
    id: control
    objectName: "statusBar"

    property var page
    readonly property Document document: page.document
    readonly property var lineEndingNames: [qsTr("Unix (LF)"), qsTr("Windows (CRLF)"), qsTr("Classic Mac (CR)")]

    implicitHeight: 28
    color: FishUI.Theme.backgroundColor

    function openEncodingMenu() {
        _encodingMenu.popup()
    }

    FishUI.RoundedRectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        continuousCorners: false
        anchors.top: parent.top
        height: 1 / FishUI.Dpi.ratio
        color: Qt.rgba(FishUI.Theme.textColor.r, FishUI.Theme.textColor.g, FishUI.Theme.textColor.b, 0.1)
    }

    // A status item: plain text with a hover plate, opening its menu.
    component Item_: FishUI.RoundImageButton {
        id: _item
        property alias text: _label.text
        property bool active: true
        // Layouts size items by their implicit size.
        implicitWidth: _label.implicitWidth + 16
        implicitHeight: 22
        width: implicitWidth
        size: 22
        enabled: active

        FishUI.Label {
            id: _label
            anchors.centerIn: parent
            font.pixelSize: 12
            color: FishUI.Theme.disabledTextColor
        }
    }

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: 8
        anchors.rightMargin: 8
        spacing: 2

        Item_ {
            objectName: "positionItem"
            text: {
                const position = qsTr("Ln %1, Col %2").arg(control.document.cursorLine + 1).arg(control.document.visualColumn)
                if (!control.document.hasSelection)
                    return position
                return position + "  " + qsTr("(%n selected)", "", control.document.selectionLength)
            }
            onClicked: control.page.showGoToLine()
        }

        Item {
            Layout.fillWidth: true
        }

        Item_ {
            visible: control.page.root && control.page.root.zoom !== 1
            text: control.page.root ? Math.round(control.page.root.zoom * 100) + "%" : ""
            onClicked: control.page.root.zoomBy(0)
        }

        Item_ {
            visible: control.page.editor.overwriteMode
            text: qsTr("OVR")
            onClicked: control.page.editor.overwriteMode = false
        }

        Item_ {
            visible: control.document.locked
            text: qsTr("Read-Only")
            onClicked: control.document.locked = false
        }

        Item_ {
            objectName: "indentationItem"
            text: control.document.insertSpaces ? qsTr("Spaces: %1").arg(control.document.indentWidth)
                                                : qsTr("Tab Width: %1").arg(control.document.tabWidth)
            onClicked: _indentMenu.popup()
        }

        Item_ {
            objectName: "languageItem"
            text: control.document.languageName
            onClicked: _languageMenu.popup()
        }

        Item_ {
            objectName: "lineEndingItem"
            text: ["LF", "CRLF", "CR"][control.document.lineEnding]
            onClicked: _lineEndingMenu.popup()
        }

        Item_ {
            objectName: "encodingItem"
            text: control.document.encoding + (control.document.bom ? " BOM" : "")
            onClicked: _encodingMenu.popup()
        }
    }

    FishUI.DesktopMenu {
        id: _indentMenu
        pointerGrabEnabled: true

        FishUI.MenuItem {
            text: qsTr("Indent with Spaces")
            checked: control.document.insertSpaces
            onTriggered: control.document.insertSpaces = true
        }

        FishUI.MenuItem {
            text: qsTr("Indent with Tabs")
            checked: !control.document.insertSpaces
            onTriggered: control.document.insertSpaces = false
        }

        FishUI.MenuSeparator {}

        Repeater {
            model: [2, 3, 4, 8]

            FishUI.MenuItem {
                text: qsTr("Width: %1").arg(modelData)
                checked: (control.document.insertSpaces ? control.document.indentWidth : control.document.tabWidth) === modelData
                onTriggered: {
                    control.document.indentWidth = modelData
                    control.document.tabWidth = modelData
                }
            }
        }
    }

    FishUI.DesktopMenu {
        id: _languageMenu
        pointerGrabEnabled: true

        Repeater {
            model: App.languages

            FishUI.MenuItem {
                text: modelData.name
                checked: control.document.language === modelData.id
                onTriggered: {
                    control.document.language = modelData.id
                    control.page.chosenLanguage = modelData.id
                }
            }
        }
    }

    FishUI.DesktopMenu {
        id: _lineEndingMenu
        pointerGrabEnabled: true

        Repeater {
            model: 3

            FishUI.MenuItem {
                text: control.lineEndingNames[index]
                checked: control.document.lineEnding === index
                onTriggered: control.document.lineEnding = index
            }
        }
    }

    FishUI.DesktopMenu {
        id: _encodingMenu
        pointerGrabEnabled: true

        FishUI.MenuItem {
            text: qsTr("Reopen with Encoding")
            enabled: !control.document.untitled
            childMenu: _reopenMenu
        }

        FishUI.MenuItem {
            text: qsTr("Save with Encoding")
            childMenu: _saveMenu
        }

        FishUI.MenuSeparator {}

        FishUI.MenuItem {
            text: qsTr("Byte Order Mark")
            enabled: control.document.encoding.startsWith("UTF-")
            checked: control.document.bom
            onTriggered: control.document.bom = !control.document.bom
        }
    }

    FishUI.DesktopMenu {
        id: _reopenMenu

        Repeater {
            model: App.encodings

            FishUI.MenuItem {
                text: modelData.label
                checked: control.document.encoding === modelData.name
                onTriggered: control.page.setEncodingForReload(modelData.name)
            }
        }
    }

    FishUI.DesktopMenu {
        id: _saveMenu

        Repeater {
            model: App.encodings

            FishUI.MenuItem {
                text: modelData.label
                checked: control.document.encoding === modelData.name
                onTriggered: control.document.setEncoding(modelData.name)
            }
        }
    }
}
