// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import FishUI as FishUI
import Cutefish.TextEditor

// Find, and with the disclosure open, replace: a card floating over the top
// right of the text, which stays where it is. Matches are found while typing;
// the options sit inside the field.
FishUI.FloatingCard {
    id: control
    objectName: "findBar"

    property var page
    readonly property Document document: page.document
    property bool replacing: false
    property bool inSelection: false
    readonly property bool notFound: _field.text.length > 0 && control.document.searchValid
                                     && control.document.matchCount === 0
    readonly property bool invalid: _field.text.length > 0 && !control.document.searchValid
    // The round buttons' plates are inset in their boxes by this much.
    readonly property int plateInset: Math.round(28 * 0.1)

    visible: false
    implicitHeight: _layout.implicitHeight + padding * 2
    // Concentric with the fields inside; too small a card for continuous corners.
    radius: FishUI.Theme.smallRadius + padding
    continuousCorners: false

    function open(text, replace) {
        replacing = replace
        visible = true
        if (text.length > 0)
            _field.text = text
        inSelection = false
        update()
        if (replace && _field.text.length > 0)
            _replaceField.forceActiveFocus()
        else
            _field.forceActiveFocus()
        _field.selectAll()
    }

    function close(focusEditor) {
        if (!visible)
            return
        visible = false
        control.document.clearSearch()
        if (focusEditor !== false)
            control.page.focusEditor()
    }

    function update() {
        control.document.setSearch(_field.text, Prefs.findCaseSensitive, Prefs.findWholeWords, Prefs.findRegex,
                                   control.inSelection)
        if (_field.text.length > 0 && !control.inSelection)
            control.document.findIncremental()
    }

    function find(backwards) {
        if (control.document.searchText !== _field.text || !visible)
            update()
        if (control.document.findNext(backwards) && control.document.lastSearchWrapped())
            _wrapped.restart()
    }

    // A two-state option inside the field, drawn as its own short label.
    component Toggle: FishUI.RoundImageButton {
        id: _toggle
        property alias label: _toggleLabel.text
        property string tip
        property bool on: false
        size: 24
        backgroundColor: on ? Qt.rgba(FishUI.Theme.highlightColor.r, FishUI.Theme.highlightColor.g,
                                      FishUI.Theme.highlightColor.b, 0.18) : "transparent"

        FishUI.Label {
            id: _toggleLabel
            anchors.centerIn: parent
            font.pixelSize: 11
            font.weight: Font.DemiBold
            color: parent.on ? FishUI.Theme.highlightColor : FishUI.Theme.textColor
        }

        HoverHandler {
            id: _hover
        }

        FishUI.ToolTip {
            text: _toggle.tip
            delay: 600
            visible: _hover.hovered && text.length > 0
        }
    }

    ColumnLayout {
        id: _layout
        anchors.fill: parent
        spacing: 6

        RowLayout {
            Layout.fillWidth: true
            spacing: 2

            FishUI.RoundImageButton {
                objectName: "replaceToggle"
                size: 28
                iconName: control.replacing ? "chevron-down" : "chevron-right"
                iconSize: 16
                onClicked: {
                    control.replacing = !control.replacing
                    if (control.replacing)
                        _replaceField.forceActiveFocus()
                }
                Accessible.name: qsTr("Replace")
            }

            FishUI.TextField {
                id: _field
                objectName: "findField"
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                placeholderText: qsTr("Find")
                rightPadding: _inside.width + 12
                color: control.notFound || control.invalid ? FishUI.Theme.redColor : FishUI.Theme.textColor

                onTextEdited: control.update()
                Keys.onReturnPressed: (event) => control.find(event.modifiers & Qt.ShiftModifier)
                Keys.onEnterPressed: (event) => control.find(event.modifiers & Qt.ShiftModifier)
                Keys.onEscapePressed: control.close()

                FishUI.ToolTip {
                    text: control.document.searchError
                    visible: control.invalid && _field.hovered
                }

                Row {
                    id: _inside
                    anchors.right: parent.right
                    anchors.rightMargin: 4
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 0

                    FishUI.Label {
                        anchors.verticalCenter: parent.verticalCenter
                        rightPadding: 4
                        font.pixelSize: 12
                        visible: text.length > 0
                        color: control.invalid || control.notFound ? FishUI.Theme.redColor : FishUI.Theme.disabledTextColor
                        text: {
                            if (_field.text.length === 0)
                                return ""
                            if (control.invalid)
                                return qsTr("Invalid")
                            if (_wrapped.running)
                                return qsTr("Wrapped")
                            if (control.document.matchCount === 0)
                                return qsTr("No matches")
                            if (control.document.matchIndex > 0)
                                return qsTr("%1 of %2").arg(control.document.matchIndex).arg(control.document.matchCount)
                            return qsTr("%n match(es)", "", control.document.matchCount)
                        }
                    }

                    Toggle {
                        label: "Aa"
                        on: Prefs.findCaseSensitive
                        tip: qsTr("Match Case")
                        onClicked: {
                            Prefs.findCaseSensitive = !Prefs.findCaseSensitive
                            control.update()
                        }
                    }

                    Toggle {
                        label: "W"
                        on: Prefs.findWholeWords
                        tip: qsTr("Whole Words")
                        onClicked: {
                            Prefs.findWholeWords = !Prefs.findWholeWords
                            control.update()
                        }
                    }

                    Toggle {
                        label: ".*"
                        on: Prefs.findRegex
                        tip: qsTr("Regular Expression")
                        onClicked: {
                            Prefs.findRegex = !Prefs.findRegex
                            control.update()
                        }
                    }

                    Toggle {
                        label: "[ ]"
                        on: control.inSelection
                        enabled: control.inSelection || control.document.hasSelection
                        tip: qsTr("In Selection")
                        onClicked: {
                            control.inSelection = !control.inSelection
                            control.update()
                        }
                    }
                }
            }

            FishUI.RoundImageButton {
                objectName: "previousMatchButton"
                size: 28
                iconName: "chevron-up"
                iconSize: 16
                enabled: control.document.matchCount > 0
                onClicked: control.find(true)
                Accessible.name: qsTr("Previous Match")
            }

            FishUI.RoundImageButton {
                size: 28
                iconName: "chevron-down"
                iconSize: 16
                enabled: control.document.matchCount > 0
                onClicked: control.find(false)
                Accessible.name: qsTr("Next Match")
            }

            FishUI.RoundImageButton {
                objectName: "closeFindButton"
                size: 28
                iconName: "close"
                iconSize: 16
                onClicked: control.close()
                Accessible.name: qsTr("Close")
            }
        }

        RowLayout {
            visible: control.replacing
            Layout.fillWidth: true
            spacing: 6
            // Ends where the close button's round plate does, not its box.
            Layout.rightMargin: control.plateInset

            // Under the disclosure, so both fields start on one edge.
            Item {
                Layout.preferredWidth: 28 + 2 - 6
            }

            FishUI.TextField {
                id: _replaceField
                objectName: "replaceField"
                Layout.fillWidth: true
                Layout.preferredHeight: 32
                placeholderText: qsTr("Replace with")
                Keys.onReturnPressed: control.document.replaceCurrent(text)
                Keys.onEnterPressed: control.document.replaceCurrent(text)
                Keys.onEscapePressed: control.close()
            }

            FishUI.Button {
                objectName: "replaceButton"
                text: qsTr("Replace")
                enabled: control.document.matchCount > 0 && !control.document.locked
                onClicked: control.document.replaceCurrent(_replaceField.text)
            }

            FishUI.Button {
                objectName: "replaceAllButton"
                text: qsTr("Replace All")
                enabled: control.document.matchCount > 0 && !control.document.locked
                onClicked: {
                    const count = control.document.replaceAll(_replaceField.text)
                    control.page.root.showPassiveNotification(qsTr("Replaced %n occurrence(s)", "", count), 2000)
                }
            }
        }
    }

    Timer {
        id: _wrapped
        interval: 1200
    }
}
