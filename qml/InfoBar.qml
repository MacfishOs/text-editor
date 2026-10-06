// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI
import Cutefish.TextEditor

// A problem with the document, shown above its text with what can be done
// about it: changed or deleted on disk, unreadable characters, permissions.
Rectangle {
    id: control
    objectName: "infoBar"

    property var page
    readonly property Document document: page.document
    readonly property int notice: document.notice
    readonly property bool warning: notice !== Document.Restored && notice !== Document.LargeFile
                                    && notice !== Document.MixedLineEndings

    visible: notice !== Document.NoNotice
    implicitHeight: Math.max(48, _layout.implicitHeight + 16)
    color: Qt.tint(FishUI.Theme.backgroundColor,
                   warning ? Qt.rgba(FishUI.Theme.orangeColor.r, FishUI.Theme.orangeColor.g, FishUI.Theme.orangeColor.b, 0.14)
                           : Qt.rgba(FishUI.Theme.highlightColor.r, FishUI.Theme.highlightColor.g, FishUI.Theme.highlightColor.b, 0.1))

    readonly property string message: {
        switch (notice) {
        case Document.Restored:
            return qsTr("Unsaved changes from your last session were restored.")
        case Document.ChangedOnDisk:
            return qsTr("The file has changed on disk. Saving now would overwrite that change.")
        case Document.Deleted:
            return qsTr("The file was deleted or moved. Your text is still here.")
        case Document.Malformed:
            return qsTr("Some characters could not be read as %1 and show as �.").arg(App.encodingLabel(document.encoding))
        case Document.Binary:
            return qsTr("This file does not look like text. Editing it could damage it.")
        case Document.ReadOnly:
            return qsTr("You do not have permission to save this file.")
        case Document.PermissionDenied:
            return qsTr("You do not have permission to open this file.")
        case Document.LoadFailed:
            return qsTr("The file could not be opened.") + (document.noticeDetail ? " " + document.noticeDetail : "")
        case Document.TooLarge:
            return qsTr("The file is too large to open (more than 256 MB).")
        case Document.SaveFailed:
            return qsTr("The file could not be saved.") + (document.noticeDetail ? " " + document.noticeDetail : "")
        case Document.LossyEncoding:
            return qsTr("Some characters cannot be saved as %1. Nothing was saved.").arg(App.encodingLabel(document.noticeDetail))
        case Document.LargeFile:
            return qsTr("Syntax highlighting is off for this large file.")
        case Document.MalformedSave:
            return qsTr("Saving would replace the characters that could not be read as %1.").arg(App.encodingLabel(document.noticeDetail))
        case Document.MixedLineEndings:
            return qsTr("This file mixes line endings. Saving will use %1 throughout.")
                    .arg(["LF", "CRLF", "CR"][document.lineEnding])
        }
        return ""
    }

    // Each action is [label, function]; the last one is the default.
    readonly property var actions: {
        const doc = document
        const root = page.root
        switch (notice) {
        case Document.Restored:
            return doc.untitled ? [[qsTr("OK"), () => doc.dismissNotice()]]
                                : [[qsTr("Discard Changes"), () => page.revert()], [qsTr("OK"), () => doc.dismissNotice()]]
        case Document.ChangedOnDisk:
            return [[qsTr("Keep My Version"), () => doc.keepLocalVersion()], [qsTr("Reload"), () => doc.reload()]]
        case Document.Deleted:
            return [[qsTr("Close"), () => root.closeTab(root.indexOfPage(page), true)], [qsTr("Save"), () => root.savePage(page)]]
        case Document.Malformed:
            return [[qsTr("Dismiss"), () => doc.dismissNotice()], [qsTr("Choose Encoding…"), () => page.showEncodingMenu()]]
        case Document.Binary:
            return [[qsTr("Close"), () => root.closeTab(root.indexOfPage(page), false)], [qsTr("Edit Anyway"), () => doc.locked = false]]
        case Document.ReadOnly:
            return [[qsTr("Dismiss"), () => doc.dismissNotice()], [qsTr("Save As…"), () => root.saveAs(page)]]
        case Document.PermissionDenied:
        case Document.LoadFailed:
            return [[qsTr("Close"), () => root.closeTab(root.indexOfPage(page), false)], [qsTr("Retry"), () => doc.reload()]]
        case Document.TooLarge:
            return [[qsTr("Close"), () => root.closeTab(root.indexOfPage(page), false)]]
        case Document.SaveFailed:
            return [[qsTr("Dismiss"), () => doc.dismissNotice()], [qsTr("Save As…"), () => root.saveAs(page)]]
        case Document.LossyEncoding:
            return [[qsTr("Choose Encoding…"), () => page.showEncodingMenu()], [qsTr("Save as UTF-8"), () => doc.saveAsUtf8()]]
        case Document.LargeFile:
            return [[qsTr("Dismiss"), () => doc.dismissNotice()], [qsTr("Highlight Anyway"), () => doc.language = doc.language]]
        case Document.MalformedSave:
            return [[qsTr("Choose Encoding…"), () => page.showEncodingMenu()], [qsTr("Save Anyway"), () => doc.saveReplacingMalformed()]]
        case Document.MixedLineEndings:
            return [[qsTr("OK"), () => doc.dismissNotice()]]
        }
        return []
    }

    FishUI.RoundedRectangle {
        anchors.left: parent.left
        anchors.right: parent.right
        continuousCorners: false
        anchors.bottom: parent.bottom
        height: 1 / FishUI.Dpi.ratio
        color: Qt.rgba(FishUI.Theme.textColor.r, FishUI.Theme.textColor.g, FishUI.Theme.textColor.b, 0.1)
    }

    RowLayout {
        id: _layout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.verticalCenter: parent.verticalCenter
        anchors.leftMargin: 14
        anchors.rightMargin: 10
        spacing: 8

        FishUI.Label {
            Layout.fillWidth: true
            text: control.message
            wrapMode: Text.Wrap
        }

        Repeater {
            model: control.actions.length

            FishUI.Button {
                text: control.actions[index][0]
                flat: index < control.actions.length - 1
                onClicked: control.actions[index][1]()
            }
        }
    }
}
