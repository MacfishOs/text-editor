// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SYNTAX_H
#define SYNTAX_H

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>

enum class SyntaxStyle : quint8 {
    Normal,
    Keyword,
    Type,
    Constant,
    String,
    Escape,
    Number,
    Comment,
    Preprocessor,
    Function,
    Variable,
    Heading,
    Emphasis,
    Strong,
    Code,
    Link,
    Tag,
    Attribute,
    Key,
    Section,
    Added,
    Removed,
    DiffHeader,
    Error,
    Warning,
    Count,
};

struct SyntaxSpan
{
    int start;
    int length;
    SyntaxStyle style;
};

struct Language;

namespace Syntax
{
// Line states carried from one line to the next; 0 is the plain state.
using State = int;

const QList<const Language *> &languages();
const Language *language(const QString &id);
QString id(const Language *language);
QString name(const QString &id);

// From the file name, then the first line (a shebang or an XML prolog),
// then the MIME database. Empty for plain text.
QString detect(const QString &fileName, const QString &firstLine);

// Colours `text` given the state at the end of the previous line, and
// returns the state at the end of this one.
State highlight(const Language *language, const QString &text, State state, QList<SyntaxSpan> *spans);
}

#endif
