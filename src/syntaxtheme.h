// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#ifndef SYNTAXTHEME_H
#define SYNTAXTHEME_H

#include "syntax.h"

#include "ui/geometry.h"

// The text area's own colours and one per syntax style, in a light and a dark
// variant that follow the system appearance.
struct SyntaxTheme
{
    cutefish::ui::Color background;
    cutefish::ui::Color text;
    cutefish::ui::Color gutterText;
    cutefish::ui::Color currentLine;
    cutefish::ui::Color selection;
    cutefish::ui::Color match;
    cutefish::ui::Color currentMatch;
    cutefish::ui::Color bracket;
    cutefish::ui::Color whitespace;
    cutefish::ui::Color styles[int(SyntaxStyle::Count)];

    static SyntaxTheme theme(bool dark, cutefish::ui::Color accent);
    // The background the theme gives the text area, which the window takes.
    static cutefish::ui::Color windowBackground(bool dark);
};

#endif
