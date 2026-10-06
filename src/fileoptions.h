// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef FILEOPTIONS_H
#define FILEOPTIONS_H

#include "textcodec.h"

#include <optional>

// The properties of the .editorconfig files above a file.
struct EditorConfig
{
    std::optional<bool> insertSpaces;
    std::optional<int> indentSize;
    std::optional<int> tabWidth;
    QByteArray charset;
    bool charsetBom = false;
    std::optional<LineEnding> lineEnding;
    std::optional<bool> trimTrailingWhitespace;
    std::optional<bool> insertFinalNewline;

    static EditorConfig forFile(const QString &path);
    // One file's sections applied to `fileName`, relative to that file's directory.
    void apply(const QString &configText, const QString &relativePath, bool *root);
    static bool globMatches(const QString &glob, const QString &relativePath);
};

struct Indentation
{
    bool insertSpaces = true;
    int width = 4;
};

namespace FileOptions
{
// The indentation most lines use, or nothing when too few lines are indented.
std::optional<Indentation> detectIndentation(const QList<QString> &lines);
}

#endif
