// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SYNTAXTHEME_H
#define SYNTAXTHEME_H

#include "syntax.h"

#include <QColor>
#include <QObject>

// The text area's own colours and one per syntax style, in a light and a dark
// variant that follow the system appearance.
struct SyntaxTheme
{
    QColor background;
    QColor text;
    QColor gutterText;
    QColor currentLine;
    QColor selection;
    QColor match;
    QColor currentMatch;
    QColor bracket;
    QColor whitespace;
    QColor styles[int(SyntaxStyle::Count)];

    static SyntaxTheme theme(bool dark, const QColor &accent);
};

// The theme's colours that QML needs.
class SyntaxThemes : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;
    // The background the theme gives the text area.
    Q_INVOKABLE QColor background(bool dark) const;
};

#endif
