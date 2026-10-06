// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef TEXTBUFFER_H
#define TEXTBUFFER_H

#include <QList>
#include <QString>

// A place in the text: a line and a UTF-16 offset into it.
struct TextPosition
{
    int line = 0;
    int column = 0;

    friend bool operator==(const TextPosition &a, const TextPosition &b)
    {
        return a.line == b.line && a.column == b.column;
    }
    friend bool operator!=(const TextPosition &a, const TextPosition &b) { return !(a == b); }
    friend bool operator<(const TextPosition &a, const TextPosition &b)
    {
        return a.line < b.line || (a.line == b.line && a.column < b.column);
    }
    friend bool operator<=(const TextPosition &a, const TextPosition &b) { return !(b < a); }
    friend bool operator>(const TextPosition &a, const TextPosition &b) { return b < a; }
    friend bool operator>=(const TextPosition &a, const TextPosition &b) { return !(a < b); }
};

struct TextRange
{
    TextPosition start;
    TextPosition end;

    bool isEmpty() const { return start == end; }
    TextRange normalized() const { return end < start ? TextRange{end, start} : *this; }
    friend bool operator==(const TextRange &a, const TextRange &b)
    {
        return a.start == b.start && a.end == b.end;
    }
};

// The text as a list of lines without their line breaks; there is always at
// least one line. Lines rather than one string keep an edit in the middle of
// a large file from moving the rest of it.
class TextBuffer
{
public:
    TextBuffer();

    void setText(const QString &text);
    void setLines(QList<QString> lines);

    int lineCount() const { return m_lines.size(); }
    const QString &line(int index) const { return m_lines.at(index); }
    const QList<QString> &lines() const { return m_lines; }

    QString text() const;
    QString text(const TextRange &range) const;
    // Characters including one per line break.
    qint64 length() const { return m_length; }

    TextPosition clamp(const TextPosition &position) const;
    TextPosition end() const;

    // Returns where the inserted text ends.
    TextPosition insert(const TextPosition &position, const QString &text);
    void remove(const TextRange &range);

    qint64 offset(const TextPosition &position) const;
    TextPosition position(qint64 offset) const;

    // Where the given text ends when inserted at position.
    static TextPosition endOf(const TextPosition &position, const QString &text);

private:
    QList<QString> m_lines;
    qint64 m_length = 0;
};

#endif
