// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SYNTAXTHEME_H
#define SYNTAXTHEME_H

#include "syntax.h"

#include <QColor>
#include <QObject>
#include <QVariantList>

// A colour scheme: the text area's own colours and one per syntax style,
// each in a light and a dark variant that follow the system appearance.
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

    static SyntaxTheme theme(const QString &scheme, bool dark, const QColor &accent);
};

// The schemes offered in the menu, for QML.
class SyntaxThemes : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QVariantList schemes READ schemes CONSTANT)

public:
    using QObject::QObject;
    QVariantList schemes() const;
    // The background a scheme gives the text area.
    Q_INVOKABLE QColor background(const QString &scheme, bool dark) const;
};

#endif
