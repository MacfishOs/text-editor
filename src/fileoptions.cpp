// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "fileoptions.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>

// The EditorConfig glob syntax as a regular expression: * stays within a
// path component, ** crosses them, {a,b} and [...] as in the shell.
static QString globToRegex(const QString &glob)
{
    QString result;
    int braces = 0;
    for (int i = 0; i < glob.size(); ++i) {
        const QChar c = glob.at(i);
        if (c == QLatin1Char('*')) {
            if (i + 1 < glob.size() && glob.at(i + 1) == QLatin1Char('*')) {
                result += QLatin1String(".*");
                ++i;
            } else {
                result += QLatin1String("[^/]*");
            }
        } else if (c == QLatin1Char('?')) {
            result += QLatin1String("[^/]");
        } else if (c == QLatin1Char('[')) {
            const qsizetype end = glob.indexOf(QLatin1Char(']'), i + 1);
            if (end < 0) {
                result += QLatin1String("\\[");
            } else {
                QString set = glob.mid(i + 1, end - i - 1);
                if (set.startsWith(QLatin1Char('!')))
                    set[0] = QLatin1Char('^');
                result += QLatin1Char('[') + set + QLatin1Char(']');
                i = int(end);
            }
        } else if (c == QLatin1Char('{')) {
            ++braces;
            result += QLatin1String("(?:");
        } else if (c == QLatin1Char('}') && braces > 0) {
            --braces;
            result += QLatin1Char(')');
        } else if (c == QLatin1Char(',') && braces > 0) {
            result += QLatin1Char('|');
        } else if (c == QLatin1Char('/')) {
            result += c;
        } else if (c == QLatin1Char('\\') && i + 1 < glob.size()) {
            result += QRegularExpression::escape(glob.at(++i));
        } else {
            result += QRegularExpression::escape(c);
        }
    }
    return result;
}

bool EditorConfig::globMatches(const QString &glob, const QString &relativePath)
{
    QString pattern = glob;
    if (pattern.startsWith(QLatin1Char('/')))
        pattern.remove(0, 1);
    else if (!pattern.contains(QLatin1Char('/')))
        pattern.prepend(QLatin1String("**/"));
    QString regex = globToRegex(pattern);
    // "**/" also matches no directory at all.
    regex.replace(QLatin1String(".*/"), QLatin1String("(?:.*/)?"));
    const QRegularExpression expression(QRegularExpression::anchoredPattern(regex));
    return expression.match(relativePath).hasMatch();
}

void EditorConfig::apply(const QString &configText, const QString &relativePath, bool *root)
{
    bool matches = false;
    bool preamble = true;
    for (const QString &raw : configText.split(QLatin1Char('\n'))) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')) || line.startsWith(QLatin1Char(';')))
            continue;
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            preamble = false;
            matches = globMatches(line.mid(1, line.size() - 2), relativePath);
            continue;
        }
        const qsizetype equals = line.indexOf(QLatin1Char('='));
        if (equals < 0)
            continue;
        const QString key = line.left(equals).trimmed().toLower();
        const QString value = line.mid(equals + 1).trimmed().toLower();
        if (preamble) {
            if (key == QLatin1String("root") && root)
                *root = value == QLatin1String("true");
            continue;
        }
        if (!matches)
            continue;

        bool number = false;
        const int size = value.toInt(&number);
        if (key == QLatin1String("indent_style")) {
            if (value == QLatin1String("space") || value == QLatin1String("tab"))
                insertSpaces = value == QLatin1String("space");
        } else if (key == QLatin1String("indent_size")) {
            if (number && size > 0 && size <= 16)
                indentSize = size;
            else if (value == QLatin1String("tab"))
                indentSize.reset();
        } else if (key == QLatin1String("tab_width")) {
            if (number && size > 0 && size <= 16)
                tabWidth = size;
        } else if (key == QLatin1String("end_of_line")) {
            if (value == QLatin1String("lf"))
                lineEnding = LineEnding::Unix;
            else if (value == QLatin1String("crlf"))
                lineEnding = LineEnding::Windows;
            else if (value == QLatin1String("cr"))
                lineEnding = LineEnding::ClassicMac;
        } else if (key == QLatin1String("charset")) {
            charsetBom = value == QLatin1String("utf-8-bom");
            if (value == QLatin1String("utf-8") || value == QLatin1String("utf-8-bom"))
                charset = QByteArrayLiteral("UTF-8");
            else if (value == QLatin1String("utf-16le"))
                charset = QByteArrayLiteral("UTF-16LE");
            else if (value == QLatin1String("utf-16be"))
                charset = QByteArrayLiteral("UTF-16BE");
            else if (value == QLatin1String("latin1"))
                charset = QByteArrayLiteral("ISO-8859-1");
        } else if (key == QLatin1String("trim_trailing_whitespace")) {
            trimTrailingWhitespace = value == QLatin1String("true");
        } else if (key == QLatin1String("insert_final_newline")) {
            insertFinalNewline = value == QLatin1String("true");
        }
    }
}

EditorConfig EditorConfig::forFile(const QString &path)
{
    // Nearer files override farther ones, so they are applied last.
    QStringList configs;
    QDir dir = QFileInfo(path).absoluteDir();
    while (true) {
        const QString config = dir.filePath(QStringLiteral(".editorconfig"));
        QFile file(config);
        if (file.open(QIODevice::ReadOnly) && file.size() < 1024 * 1024) {
            const QString text = QString::fromUtf8(file.readAll());
            configs.prepend(config);
            bool root = false;
            EditorConfig probe;
            probe.apply(text, QString(), &root);
            if (root)
                break;
        }
        if (!dir.cdUp())
            break;
    }

    EditorConfig result;
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (const QString &config : std::as_const(configs)) {
        QFile file(config);
        if (!file.open(QIODevice::ReadOnly))
            continue;
        const QString relative = QFileInfo(config).absoluteDir().relativeFilePath(absolute);
        result.apply(QString::fromUtf8(file.readAll()), relative, nullptr);
    }
    if (result.indentSize && !result.tabWidth)
        result.tabWidth = result.indentSize;
    return result;
}

std::optional<Indentation> FileOptions::detectIndentation(const QList<QString> &lines)
{
    int tabs = 0;
    int spaces = 0;
    QHash<int, int> steps;
    int previous = 0;
    const int limit = qMin<int>(lines.size(), 2000);
    for (int i = 0; i < limit; ++i) {
        const QString &line = lines.at(i);
        if (line.trimmed().isEmpty())
            continue;
        if (line.startsWith(QLatin1Char('\t'))) {
            ++tabs;
            previous = 0;
            continue;
        }
        int indent = 0;
        while (indent < line.size() && line.at(indent) == QLatin1Char(' '))
            ++indent;
        // A " * " comment continuation is not indentation.
        if (indent > 0 && indent < line.size() && line.at(indent) == QLatin1Char('*')) {
            continue;
        }
        if (indent > 0)
            ++spaces;
        const int step = indent - previous;
        if (step >= 2 && step <= 8)
            ++steps[step];
        previous = indent;
    }

    if (tabs + spaces < 3)
        return std::nullopt;
    if (tabs > spaces)
        return Indentation{false, 0};

    int best = 0;
    int bestCount = 0;
    for (auto it = steps.cbegin(); it != steps.cend(); ++it) {
        if (it.value() > bestCount || (it.value() == bestCount && it.key() < best)) {
            best = it.key();
            bestCount = it.value();
        }
    }
    if (best == 0)
        return std::nullopt;
    return Indentation{true, best};
}
