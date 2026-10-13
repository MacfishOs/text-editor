// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

import QtQuick
import QtCore

// One instance for the process, shared by every window.
Settings {
    location: StandardPaths.writableLocation(StandardPaths.GenericConfigLocation)
              + "/cutefish/text-editor.conf"
    category: "Editor"

    // Empty: the system's monospace font.
    property string fontFamily: ""
    // 0: the system font's size.
    property real fontSize: 0
    property bool wordWrap: true
    property bool lineNumbers: true
    property bool highlightCurrentLine: false
    property bool showWhitespace: false
    property bool matchBrackets: true
    property bool autoIndent: true
    property int tabWidth: 4
    property bool insertSpaces: true
    property bool restoreSession: true

    property bool findCaseSensitive: false
    property bool findWholeWords: false
    property bool findRegex: false
    property string lastFolder: ""
}
