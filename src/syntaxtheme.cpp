// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#include "syntaxtheme.h"

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

// One Light and One Dark, as their author defines them.
static const Palette lightPalette{"#FFFFFF", "#323238", "#A0A1A7", "#A626A4", "#0184BC", "#986801",
                                  "#50A14F", "#4078F2", "#E45649", "#C18401"};
static const Palette darkPalette{"#232325", "#E6E6E6", "#7F848E", "#C678DD", "#56B6C2", "#D19A66",
                                 "#98C379", "#61AFEF", "#E06C75", "#E5C07B"};

static QColor alpha(const QColor &color, qreal a)
{
    QColor result = color;
    result.setAlphaF(a);
    return result;
}

SyntaxTheme SyntaxTheme::theme(bool dark, const QColor &accent)
{
    const Palette &p = dark ? darkPalette : lightPalette;
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

QColor SyntaxThemes::background(bool dark) const
{
    return QColor(QLatin1String((dark ? darkPalette : lightPalette).background));
}
