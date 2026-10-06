// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "syntaxtheme.h"

#include <QCoreApplication>
#include <QVariantMap>

struct Palette
{
    const char *background;
    const char *text;
    const char *comment;
    const char *keyword;
    const char *type;
    const char *constant;
    const char *string;
    const char *function;
    const char *variable;
    const char *preprocessor;
};

// One Light / One Dark, Solarized and Tokyo Night (Day), as their authors define them.
static const Palette defaultLight{"#FFFFFF", "#323238", "#A0A1A7", "#A626A4", "#0184BC", "#986801",
                                  "#50A14F", "#4078F2", "#E45649", "#C18401"};
static const Palette defaultDark{"#232325", "#E6E6E6", "#7F848E", "#C678DD", "#56B6C2", "#D19A66",
                                 "#98C379", "#61AFEF", "#E06C75", "#E5C07B"};
static const Palette solarizedLight{"#FDF6E3", "#586E75", "#93A1A1", "#859900", "#B58900", "#CB4B16",
                                    "#2AA198", "#268BD2", "#D33682", "#CB4B16"};
static const Palette solarizedDark{"#002B36", "#93A1A1", "#586E75", "#859900", "#B58900", "#CB4B16",
                                   "#2AA198", "#268BD2", "#D33682", "#CB4B16"};
static const Palette tokyoDay{"#E1E2E7", "#3760BF", "#848CB5", "#9854F1", "#188092", "#B15C00",
                              "#587539", "#2E7DE9", "#F52A65", "#8C6C3E"};
static const Palette tokyoNight{"#1A1B26", "#C0CAF5", "#565F89", "#BB9AF7", "#2AC3DE", "#FF9E64",
                                "#9ECE6A", "#7AA2F7", "#F7768E", "#7DCFFF"};

static const Palette &palette(const QString &scheme, bool dark)
{
    if (scheme == QLatin1String("solarized"))
        return dark ? solarizedDark : solarizedLight;
    if (scheme == QLatin1String("tokyonight"))
        return dark ? tokyoNight : tokyoDay;
    return dark ? defaultDark : defaultLight;
}

static QColor alpha(const QColor &color, qreal a)
{
    QColor result = color;
    result.setAlphaF(a);
    return result;
}

SyntaxTheme SyntaxTheme::theme(const QString &scheme, bool dark, const QColor &accent)
{
    const Palette &p = palette(scheme, dark);
    SyntaxTheme t;
    t.background = QColor(QLatin1String(p.background));
    t.text = QColor(QLatin1String(p.text));
    t.gutterText = alpha(t.text, dark ? 0.35 : 0.4);
    t.currentLine = alpha(t.text, dark ? 0.05 : 0.04);
    t.selection = alpha(accent, dark ? 0.4 : 0.25);
    t.match = dark ? QColor(255, 196, 0, 70) : QColor(255, 200, 0, 90);
    t.currentMatch = dark ? QColor(255, 152, 0, 150) : QColor(255, 152, 0, 140);
    t.bracket = alpha(accent, dark ? 0.35 : 0.22);
    t.whitespace = alpha(t.text, 0.3);

    const QColor comment(QLatin1String(p.comment));
    const QColor keyword(QLatin1String(p.keyword));
    const QColor type(QLatin1String(p.type));
    const QColor constant(QLatin1String(p.constant));
    const QColor string(QLatin1String(p.string));
    const QColor function(QLatin1String(p.function));
    const QColor variable(QLatin1String(p.variable));
    const QColor preprocessor(QLatin1String(p.preprocessor));

    auto set = [&t](SyntaxStyle style, const QColor &color) { t.styles[int(style)] = color; };
    set(SyntaxStyle::Normal, t.text);
    set(SyntaxStyle::Keyword, keyword);
    set(SyntaxStyle::Type, type);
    set(SyntaxStyle::Constant, constant);
    set(SyntaxStyle::String, string);
    set(SyntaxStyle::Escape, type);
    set(SyntaxStyle::Number, constant);
    set(SyntaxStyle::Comment, comment);
    set(SyntaxStyle::Preprocessor, preprocessor);
    set(SyntaxStyle::Function, function);
    set(SyntaxStyle::Variable, variable);
    set(SyntaxStyle::Heading, function);
    set(SyntaxStyle::Emphasis, t.text);
    set(SyntaxStyle::Strong, t.text);
    set(SyntaxStyle::Code, string);
    set(SyntaxStyle::Link, function);
    set(SyntaxStyle::Tag, variable);
    set(SyntaxStyle::Attribute, constant);
    set(SyntaxStyle::Key, function);
    set(SyntaxStyle::Section, keyword);
    set(SyntaxStyle::Added, string);
    set(SyntaxStyle::Removed, variable);
    set(SyntaxStyle::DiffHeader, comment);
    set(SyntaxStyle::Error, variable);
    set(SyntaxStyle::Warning, constant);
    return t;
}

QVariantList SyntaxThemes::schemes() const
{
    return {
        QVariantMap{{QStringLiteral("id"), QStringLiteral("default")},
                    {QStringLiteral("name"), QCoreApplication::translate("SyntaxThemes", "Cutefish")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("solarized")},
                    {QStringLiteral("name"), QCoreApplication::translate("SyntaxThemes", "Solarized")}},
        QVariantMap{{QStringLiteral("id"), QStringLiteral("tokyonight")},
                    {QStringLiteral("name"), QCoreApplication::translate("SyntaxThemes", "Tokyo Night")}},
    };
}

QColor SyntaxThemes::background(const QString &scheme, bool dark) const
{
    return QColor(QLatin1String(palette(scheme, dark).background));
}
