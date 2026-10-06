// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SEARCHER_H
#define SEARCHER_H

#include "textbuffer.h"

#include <QRegularExpression>

// Finds a pattern line by line: plain text, whole words or a regular
// expression. A match never spans a line break.
class Searcher
{
public:
    struct Options
    {
        bool caseSensitive = false;
        bool wholeWords = false;
        bool regularExpression = false;
    };

    void setPattern(const QString &pattern, const Options &options);
    const QString &pattern() const { return m_pattern; }
    const Options &options() const { return m_options; }
    bool isEmpty() const { return m_pattern.isEmpty(); }
    bool isValid() const { return !m_pattern.isEmpty() && m_expression.isValid(); }
    QString errorString() const;

    // Matches within one line, in order, limited to [from, to).
    QList<TextRange> matchesInLine(const QString &text, int line, int from = 0, int to = -1) const;

    // The next match at or after `from` (forwards) or ending at or before it
    // (backwards), inside `scope` when it is not empty. Wraps around the
    // scope; `wrapped` tells whether it did. Returns an empty range at {-1,-1}
    // when there is none.
    TextRange find(const TextBuffer &buffer, const TextPosition &from, bool backwards,
                   const TextRange &scope = {}, bool *wrapped = nullptr) const;

    // Matches in the whole buffer or the scope; stops counting at limit.
    int count(const TextBuffer &buffer, const TextRange &scope = {}, int limit = 100000) const;

    // What the match at `range` is replaced with: captures expand for
    // regular expressions (\1 or $1, \n and \t too).
    QString replacement(const TextBuffer &buffer, const TextRange &range, const QString &replace) const;

    static bool isNotFound(const TextRange &range) { return range.start.line < 0; }

private:
    QRegularExpressionMatch matchAt(const QString &text, int from) const;

    QString m_pattern;
    Options m_options;
    QRegularExpression m_expression;
};

#endif
