// SPDX-FileCopyrightText: 2026 Reion Wong <reionwong@gmail.com>
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <cstdint>

#include "ui/color_provider.h"

// The text editor's own colours, after the toolkit's; colors.cpp gives them
// for each theme. The code's colours are the syntax theme's.
enum class EditorColor : uint16_t {
    FloatingCardShadow = uint16_t(cutefish::ui::ColorId::ApplicationFirst),
    FloatingCardBorder,
};

void addEditorColors(cutefish::ui::ColorMixer &mixer, const cutefish::ui::Theme &theme);

// Added before main by every program that names one of the colours.
[[maybe_unused]] inline const bool editorColorsAdded = (cutefish::ui::ColorProvider::addMixer(addEditorColors), true);
