// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtQuick.Layouts
import QtQuick.Window
import FishUI as FishUI

// The global defaults; per-document settings live in the status bar.
FishUI.Window {
    id: control
    objectName: "preferencesDialog"

    title: qsTr("Settings")

    readonly property int dialogWidth: 460
    readonly property int dialogHeight: _mainLayout.implicitHeight + header.height + FishUI.Units.largeSpacing * 1.5

    width: dialogWidth
    height: dialogHeight
    minimumWidth: dialogWidth
    maximumWidth: dialogWidth
    minimumHeight: dialogHeight
    maximumHeight: dialogHeight

    flags: Qt.Dialog | Qt.FramelessWindowHint
    modality: Qt.WindowModal
    visible: false
    minimizeButtonVisible: false
    header.height: 44
    windowButtonsAlignment: Qt.AlignVCenter
    windowButtonsTopMargin: 0
    windowButtonsRightMargin: FishUI.Units.smallSpacing
    restorationBehavior: FishUI.Window.Disabled

    background.color: FishUI.Theme.backgroundColor

    readonly property color separatorColor: Qt.rgba(FishUI.Theme.textColor.r, FishUI.Theme.textColor.g,
                                                     FishUI.Theme.textColor.b, 0.08)
    readonly property var fontFamilies: [qsTr("System Monospace")].concat(App.monospaceFamilies)
    readonly property var fontSizes: [0, 8, 9, 10, 11, 12, 13, 14, 16, 18, 20, 24]
    readonly property var tabWidths: [2, 3, 4, 8]

    component Card: FishUI.RoundedRectangle {
        default property alias rows: _rows.data

        Layout.fillWidth: true
        implicitHeight: _rows.implicitHeight
        radius: FishUI.Theme.bigRadius
        color: FishUI.Theme.secondBackgroundColor

        ColumnLayout {
            id: _rows
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.top: parent.top
            spacing: 0
        }
    }

    // A label on the left, its control on the right, a hairline below unless last.
    component SettingRow: Item {
        property alias text: _label.text
        property bool last: false
        default property alias content: _content.data

        Layout.fillWidth: true
        implicitHeight: 44

        FishUI.Label {
            id: _label
            anchors.left: parent.left
            anchors.leftMargin: 14
            anchors.verticalCenter: parent.verticalCenter
        }

        Row {
            id: _content
            anchors.right: parent.right
            anchors.rightMargin: 14
            anchors.verticalCenter: parent.verticalCenter
            spacing: FishUI.Units.largeSpacing
        }

        FishUI.RoundedRectangle {
            anchors.left: parent.left
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            anchors.leftMargin: 14
            anchors.rightMargin: 14
            height: 1 / FishUI.Dpi.ratio
            continuousCorners: false
            color: control.separatorColor
            visible: !parent.last
        }
    }

    component SwitchRow: SettingRow {
        property alias checked: _switch.checked
        signal toggled(bool checked)

        FishUI.Switch {
            id: _switch
            anchors.verticalCenter: parent.verticalCenter
            onToggled: parent.parent.toggled(checked)
        }
    }

    headerItem: Item {
        FishUI.Label {
            anchors.centerIn: parent
            anchors.horizontalCenterOffset: (control.width - parent.width) / 2
            text: control.title
            font.pixelSize: 13
            font.weight: Font.Medium
        }
    }

    ColumnLayout {
        id: _mainLayout
        anchors.left: parent.left
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.leftMargin: FishUI.Units.largeSpacing * 1.5
        anchors.rightMargin: FishUI.Units.largeSpacing * 1.5
        spacing: FishUI.Units.largeSpacing

        Card {
            SettingRow {
                text: qsTr("Font")

                FishUI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 200
                    model: control.fontFamilies
                    currentIndex: Prefs.fontFamily.length > 0 ? Math.max(0, control.fontFamilies.indexOf(Prefs.fontFamily)) : 0
                    onActivated: (index) => Prefs.fontFamily = index === 0 ? "" : control.fontFamilies[index]
                }
            }

            SettingRow {
                text: qsTr("Size")

                FishUI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 200
                    model: control.fontSizes.map(size => size === 0 ? qsTr("System") : size + " pt")
                    currentIndex: Math.max(0, control.fontSizes.indexOf(Prefs.fontSize))
                    onActivated: (index) => Prefs.fontSize = control.fontSizes[index]
                }
            }

            SettingRow {
                text: qsTr("Color Scheme")
                last: true

                FishUI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 200
                    model: Themes.schemes.map(scheme => scheme.name)
                    currentIndex: Math.max(0, Themes.schemes.findIndex(scheme => scheme.id === Prefs.colorScheme))
                    onActivated: (index) => Prefs.colorScheme = Themes.schemes[index].id
                }
            }
        }

        Card {
            SettingRow {
                text: qsTr("Tab Width")

                FishUI.ComboBox {
                    anchors.verticalCenter: parent.verticalCenter
                    width: 200
                    model: control.tabWidths
                    currentIndex: Math.max(0, control.tabWidths.indexOf(Prefs.tabWidth))
                    onActivated: (index) => Prefs.tabWidth = control.tabWidths[index]
                }
            }

            SwitchRow {
                text: qsTr("Insert Spaces Instead of Tabs")
                checked: Prefs.insertSpaces
                onToggled: (checked) => Prefs.insertSpaces = checked
            }

            SwitchRow {
                text: qsTr("Automatic Indentation")
                checked: Prefs.autoIndent
                onToggled: (checked) => Prefs.autoIndent = checked
                last: true
            }
        }

        Card {
            SwitchRow {
                text: qsTr("Wrap Lines")
                checked: Prefs.wordWrap
                onToggled: (checked) => Prefs.wordWrap = checked
            }

            SwitchRow {
                text: qsTr("Line Numbers")
                checked: Prefs.lineNumbers
                onToggled: (checked) => Prefs.lineNumbers = checked
            }

            SwitchRow {
                text: qsTr("Highlight Current Line")
                checked: Prefs.highlightCurrentLine
                onToggled: (checked) => Prefs.highlightCurrentLine = checked
            }

            SwitchRow {
                text: qsTr("Show Whitespace")
                checked: Prefs.showWhitespace
                onToggled: (checked) => Prefs.showWhitespace = checked
            }

            SwitchRow {
                text: qsTr("Match Brackets")
                checked: Prefs.matchBrackets
                onToggled: (checked) => Prefs.matchBrackets = checked
                last: true
            }
        }

        Card {
            SwitchRow {
                text: qsTr("Restore Session on Start")
                checked: Prefs.restoreSession
                onToggled: (checked) => Prefs.restoreSession = checked
                last: true
            }
        }
    }
}
