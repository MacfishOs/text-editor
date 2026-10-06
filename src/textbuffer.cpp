// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "textbuffer.h"

TextBuffer::TextBuffer()
    : m_lines(1)
{
}

void TextBuffer::setText(const QString &text)
{
    setLines(text.split(QLatin1Char('\n')));
}

void TextBuffer::setLines(QList<QString> lines)
{
    if (lines.isEmpty())
        lines.append(QString());
    m_lines = std::move(lines);
    m_length = m_lines.size() - 1;
    for (const QString &line : std::as_const(m_lines))
        m_length += line.size();
}

QString TextBuffer::text() const
{
    QString result;
    result.reserve(m_length);
    for (int i = 0; i < m_lines.size(); ++i) {
        if (i > 0)
            result += QLatin1Char('\n');
        result += m_lines.at(i);
    }
    return result;
}

QString TextBuffer::text(const TextRange &range) const
{
    const TextRange r{clamp(range.normalized().start), clamp(range.normalized().end)};
    if (r.start.line == r.end.line)
        return m_lines.at(r.start.line).mid(r.start.column, r.end.column - r.start.column);

    QString result = m_lines.at(r.start.line).mid(r.start.column);
    for (int i = r.start.line + 1; i < r.end.line; ++i) {
        result += QLatin1Char('\n');
        result += m_lines.at(i);
    }
    result += QLatin1Char('\n');
    result += QStringView(m_lines.at(r.end.line)).left(r.end.column);
    return result;
}

TextPosition TextBuffer::clamp(const TextPosition &position) const
{
    if (position.line < 0)
        return {0, 0};
    if (position.line >= m_lines.size())
        return end();
    return {position.line, qBound(0, position.column, int(m_lines.at(position.line).size()))};
}

TextPosition TextBuffer::end() const
{
    return {int(m_lines.size()) - 1, int(m_lines.last().size())};
}

TextPosition TextBuffer::endOf(const TextPosition &position, const QString &text)
{
    const int breaks = text.count(QLatin1Char('\n'));
    if (breaks == 0)
        return {position.line, position.column + int(text.size())};
    return {position.line + breaks, int(text.size() - text.lastIndexOf(QLatin1Char('\n')) - 1)};
}

TextPosition TextBuffer::insert(const TextPosition &at, const QString &text)
{
    const TextPosition position = clamp(at);
    if (text.isEmpty())
        return position;

    m_length += text.size();
    QString &first = m_lines[position.line];
    if (!text.contains(QLatin1Char('\n'))) {
        first.insert(position.column, text);
        return {position.line, position.column + int(text.size())};
    }

    QList<QString> pieces = text.split(QLatin1Char('\n'));
    const QString tail = first.mid(position.column);
    first.truncate(position.column);
    first += pieces.first();
    const TextPosition result{position.line + int(pieces.size()) - 1, int(pieces.last().size())};
    pieces.last() += tail;
    m_lines.insert(position.line + 1, pieces.size() - 1, QString());
    for (int i = 1; i < pieces.size(); ++i)
        m_lines[position.line + i] = std::move(pieces[i]);
    return result;
}

void TextBuffer::remove(const TextRange &range)
{
    const TextPosition start = clamp(range.normalized().start);
    const TextPosition end = clamp(range.normalized().end);
    if (start == end)
        return;

    if (start.line == end.line) {
        m_lines[start.line].remove(start.column, end.column - start.column);
        m_length -= end.column - start.column;
        return;
    }

    qint64 removed = m_lines.at(start.line).size() - start.column + end.column + (end.line - start.line);
    for (int i = start.line + 1; i < end.line; ++i)
        removed += m_lines.at(i).size();
    m_length -= removed;

    QString &first = m_lines[start.line];
    first.truncate(start.column);
    first += QStringView(m_lines.at(end.line)).mid(end.column);
    m_lines.remove(start.line + 1, end.line - start.line);
}

qint64 TextBuffer::offset(const TextPosition &position) const
{
    const TextPosition p = clamp(position);
    qint64 result = p.column;
    for (int i = 0; i < p.line; ++i)
        result += m_lines.at(i).size() + 1;
    return result;
}

TextPosition TextBuffer::position(qint64 offset) const
{
    if (offset <= 0)
        return {0, 0};
    for (int i = 0; i < m_lines.size(); ++i) {
        const qint64 size = m_lines.at(i).size();
        if (offset <= size)
            return {i, int(offset)};
        offset -= size + 1;
    }
    return end();
}
