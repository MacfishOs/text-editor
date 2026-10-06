// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI

// Save, Don't Save or Cancel for documents with unsaved changes. Several
// documents are listed with a check box each; Save saves the checked ones.
FishUI.Window {
    id: control

    property var pages: []
    property var checkedPages: []
    property var action: null

    signal saveRequested(var page, var done)

    flags: Qt.Dialog | Qt.FramelessWindowHint | Qt.WindowStaysOnTopHint
    modality: Qt.WindowModal
    visible: false
    minimizeButtonVisible: false
    contentTopMargin: 0
    windowButtonsTopMargin: 8
    windowButtonsRightMargin: 8
    header.height: 44
    background.color: FishUI.Theme.secondBackgroundColor
    color: background.color
    restorationBehavior: FishUI.Window.Disabled

    readonly property int dialogWidth: 380
    readonly property int dialogHeight: _layout.implicitHeight + 16 + 12
    width: dialogWidth
    height: dialogHeight
    minimumWidth: dialogWidth
    maximumWidth: dialogWidth
    minimumHeight: dialogHeight
    maximumHeight: dialogHeight

    function ask(list, then) {
        pages = list
        checkedPages = list.slice()
        action = then
        title = list.length === 1 ? qsTr("Save changes to “%1”?").arg(list[0].title)
                                  : qsTr("Save changes to %n documents?", "", list.length)
        if (parentWindow) {
            x = parentWindow.x + Math.round((parentWindow.width - width) / 2)
            y = parentWindow.y + Math.round((parentWindow.height - height) / 3)
        }
        show()
        raise()
        requestActivate()
        _saveButton.forceActiveFocus()
    }

    function finish(proceed) {
        const then = action
        action = null
        close()
        if (proceed && then)
            then()
    }

    // Saves the checked documents one after another; a cancelled Save As stops it.
    function saveChecked() {
        const queue = checkedPages.slice()
        const unchecked = pages.filter(page => checkedPages.indexOf(page) < 0)
        const next = () => {
            const page = queue.shift()
            if (!page) {
                for (const discarded of unchecked)
                    discarded.document.removeDraft()
                finish(true)
                return
            }
            control.saveRequested(page, saved => {
                if (saved)
                    next()
            })
        }
        hide()
        next()
    }

    Shortcut {
        sequence: "Escape"
        onActivated: control.finish(false)
    }

    // Return saves, as the highlighted button says.
    Shortcut {
        sequences: ["Return", "Enter"]
        enabled: control.checkedPages.length > 0
        onActivated: control.saveChecked()
    }

    onClosing: action = null

    DragHandler {
        parent: control.contentItem
        target: null
        acceptedDevices: PointerDevice.Mouse | PointerDevice.TouchPad | PointerDevice.TouchScreen
        grabPermissions: PointerHandler.CanTakeOverFromItems
                         | PointerHandler.CanTakeOverFromHandlersOfDifferentType
                         | PointerHandler.ApprovesTakeOverByAnything
        onActiveChanged: if (active) control.controller.startSystemMove()
    }

    ColumnLayout {
        id: _layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 16
        spacing: 16

        Column {
            Layout.fillWidth: true
            Layout.rightMargin: 32
            spacing: 4

            FishUI.Label {
                width: parent.width
                text: control.title
                textFormat: Text.PlainText
                font.pixelSize: 14
                font.weight: Font.DemiBold
                wrapMode: Text.Wrap
            }

            FishUI.Label {
                width: parent.width
                text: qsTr("If you don't save, your changes will be lost.")
                font.pixelSize: 14
                color: FishUI.Theme.disabledTextColor
                wrapMode: Text.Wrap
            }
        }

        Column {
            Layout.fillWidth: true
            visible: control.pages.length > 1
            spacing: 4

            Repeater {
                model: control.pages.length > 1 ? control.pages : []

                FishUI.CheckBox {
                    text: modelData.title + (modelData.document.folder ? "  —  " + modelData.document.folder : "")
                    checked: control.checkedPages.indexOf(modelData) >= 0
                    onToggled: {
                        const list = control.checkedPages.filter(page => page !== modelData)
                        if (checked)
                            list.push(modelData)
                        control.checkedPages = list
                    }
                }
            }
        }

        RowLayout {
            Layout.alignment: Qt.AlignRight
            spacing: 8

            FishUI.ConfirmDialogButton {
                text: qsTr("Cancel")
                onClicked: control.finish(false)
            }

            FishUI.ConfirmDialogButton {
                text: control.pages.length > 1 ? qsTr("Discard All") : qsTr("Don't Save")
                destructive: true
                onClicked: {
                    for (const page of control.pages)
                        page.document.removeDraft()
                    control.finish(true)
                }
            }

            FishUI.ConfirmDialogButton {
                id: _saveButton
                text: qsTr("Save")
                primary: true
                enabled: control.checkedPages.length > 0
                onClicked: control.saveChecked()
            }
        }
    }
}
