// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI
import Cutefish.TextEditor

// "Line" or "line:column" in a small card where the find card floats; Enter
// jumps, Escape goes back to the text.
FishUI.FloatingCard {
    id: control
    objectName: "goToLineBar"

    property var page
    readonly property Document document: page.document
    readonly property var target: {
        const match = /^\s*(\d+)\s*(?:[:,]\s*(\d+))?\s*$/.exec(_field.text)
        return match ? { line: parseInt(match[1]), column: match[2] ? parseInt(match[2]) : 1 } : null
    }
    readonly property bool valid: target !== null && target.line >= 1 && target.line <= document.lineCount

    visible: false
    implicitWidth: 300
    implicitHeight: 32 + padding * 2

    function open() {
        visible = true
        _field.text = ""
        _field.forceActiveFocus()
    }

    function close() {
        if (!visible)
            return
        visible = false
        page.focusEditor()
    }

    function accept() {
        if (!valid)
            return
        document.goToLine(target.line, target.column)
        close()
    }

    RowLayout {
        anchors.fill: parent
        spacing: 2

        FishUI.TextField {
            id: _field
            objectName: "goToLineField"
            Layout.fillWidth: true
            Layout.preferredHeight: 32
            placeholderText: qsTr("Go to line (1–%1), :column").arg(control.document.lineCount)
            color: _field.text.length > 0 && !control.valid ? FishUI.Theme.redColor : FishUI.Theme.textColor
            validator: RegularExpressionValidator { regularExpression: /[0-9:, ]*/ }
            Keys.onReturnPressed: control.accept()
            Keys.onEnterPressed: control.accept()
            Keys.onEscapePressed: control.close()
        }

        FishUI.RoundImageButton {
            size: 28
            iconName: "close"
            iconSize: 16
            onClicked: control.close()
            Accessible.name: qsTr("Close")
        }
    }
}
