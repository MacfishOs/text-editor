// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "searcher.h"

static const TextRange notFound{{-1, -1}, {-1, -1}};

void Searcher::setPattern(const QString &pattern, const Options &options)
{
    m_pattern = pattern;
    m_options = options;

    QString source = options.regularExpression ? pattern : QRegularExpression::escape(pattern);
    if (options.wholeWords)
        source = QStringLiteral("(?<!\\w)(?:%1)(?!\\w)").arg(source);
    QRegularExpression::PatternOptions flags = QRegularExpression::UseUnicodePropertiesOption;
    if (!options.caseSensitive)
        flags |= QRegularExpression::CaseInsensitiveOption;
    m_expression = QRegularExpression(source, flags);
    m_expression.optimize();
}

QString Searcher::errorString() const
{
    return m_expression.isValid() ? QString() : m_expression.errorString();
}

QList<TextRange> Searcher::matchesInLine(const QString &text, int line, int from, int to) const
{
    QList<TextRange> result;
    if (!isValid())
        return result;
    if (to < 0)
        to = text.size();

    // Plain text needs no regular expression.
    if (!m_options.regularExpression && !m_options.wholeWords) {
        const Qt::CaseSensitivity cs = m_options.caseSensitive ? Qt::CaseSensitive : Qt::CaseInsensitive;
        qsizetype at = text.indexOf(m_pattern, from, cs);
        while (at >= 0 && at + m_pattern.size() <= to) {
            result.append(TextRange{{line, int(at)}, {line, int(at + m_pattern.size())}});
            at = text.indexOf(m_pattern, at + m_pattern.size(), cs);
        }
        return result;
    }

    auto it = m_expression.globalMatch(text, 0);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        const int start = int(match.capturedStart());
        const int end = int(match.capturedEnd());
        if (end > to)
            break;
        if (start < from || start == end)
            continue;
        result.append(TextRange{{line, start}, {line, end}});
    }
    return result;
}

TextRange Searcher::find(const TextBuffer &buffer, const TextPosition &position, bool backwards,
                         const TextRange &scopeRange, bool *wrapped) const
{
    if (wrapped)
        *wrapped = false;
    if (!isValid())
        return notFound;

    const TextRange scope = scopeRange.isEmpty() ? TextRange{{0, 0}, buffer.end()} : scopeRange.normalized();
    TextPosition from = buffer.clamp(position);
    if (from < scope.start || from > scope.end)
        from = backwards ? scope.end : scope.start;

    auto lineMatches = [&](int line) {
        const QString &text = buffer.line(line);
        const int lower = line == scope.start.line ? scope.start.column : 0;
        const int upper = line == scope.end.line ? scope.end.column : int(text.size());
        return matchesInLine(text, line, lower, upper);
    };

    if (!backwards) {
        for (int pass = 0; pass < 2; ++pass) {
            const int first = pass == 0 ? from.line : scope.start.line;
            const int last = pass == 0 ? scope.end.line : from.line;
            for (int line = first; line <= last; ++line) {
                for (const TextRange &match : lineMatches(line)) {
                    if (pass == 0 && match.start < from)
                        continue;
                    if (pass == 1 && match.start >= from)
                        return notFound;
                    if (wrapped)
                        *wrapped = pass == 1;
                    return match;
                }
            }
        }
        return notFound;
    }

    for (int pass = 0; pass < 2; ++pass) {
        const int first = pass == 0 ? from.line : scope.end.line;
        const int last = pass == 0 ? scope.start.line : from.line;
        for (int line = first; line >= last; --line) {
            const QList<TextRange> matches = lineMatches(line);
            for (auto it = matches.crbegin(); it != matches.crend(); ++it) {
                if (pass == 0 && it->end > from)
                    continue;
                if (pass == 1 && it->end <= from)
                    return notFound;
                if (wrapped)
                    *wrapped = pass == 1;
                return *it;
            }
        }
    }
    return notFound;
}

int Searcher::count(const TextBuffer &buffer, const TextRange &scopeRange, int limit) const
{
    if (!isValid())
        return 0;
    const TextRange scope = scopeRange.isEmpty() ? TextRange{{0, 0}, buffer.end()} : scopeRange.normalized();
    int total = 0;
    for (int line = scope.start.line; line <= scope.end.line && total < limit; ++line) {
        const QString &text = buffer.line(line);
        const int lower = line == scope.start.line ? scope.start.column : 0;
        const int upper = line == scope.end.line ? scope.end.column : int(text.size());
        total += matchesInLine(text, line, lower, upper).size();
    }
    return qMin(total, limit);
}

QString Searcher::replacement(const TextBuffer &buffer, const TextRange &range, const QString &replace) const
{
    if (!m_options.regularExpression)
        return replace;

    const QString &text = buffer.line(range.start.line);
    const QRegularExpressionMatch match = m_expression.match(text, range.start.column, QRegularExpression::NormalMatch,
                                                             QRegularExpression::AnchorAtOffsetMatchOption);
    QString result;
    for (qsizetype i = 0; i < replace.size(); ++i) {
        const QChar c = replace.at(i);
        const QChar next = i + 1 < replace.size() ? replace.at(i + 1) : QChar();
        if (c == QLatin1Char('\\') && !next.isNull()) {
            ++i;
            if (next.isDigit())
                result += match.captured(next.digitValue());
            else if (next == QLatin1Char('n'))
                result += QLatin1Char('\n');
            else if (next == QLatin1Char('t'))
                result += QLatin1Char('\t');
            else
                result += next;
        } else if (c == QLatin1Char('$') && next.isDigit()) {
            ++i;
            result += match.captured(next.digitValue());
        } else if (c == QLatin1Char('$') && next == QLatin1Char('$')) {
            ++i;
            result += c;
        } else {
            result += c;
        }
    }
    return result;
}
