// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - small helpers shared by the window code: layout rounding like WPF's
// UseLayoutRounding, and the font set of the UI.

#pragma once

#include "gfx.h"

#include <math.h>

namespace ui {

// Math.Round on a value in device pixels (to nearest, halves to even), like WPF's layout rounding.
inline float Rn(float v) {
    return (float)nearbyint((double)v);
}

// The fonts of the UI. Sizes are in DIP, the caller scales them (UI scale and device scale).
inline gfx::Font TextFont(float sizePx, int weight = 400) { return gfx::Font{ gfx::FamText, weight, sizePx }; }
inline gfx::Font DisplayFont(float sizePx, int weight = 600) { return gfx::Font{ gfx::FamDisplay, weight, sizePx }; }
inline gfx::Font IconFont(float sizePx) { return gfx::Font{ gfx::FamIcons, 400, sizePx }; }

// Glyphs of Segoe Fluent Icons (Segoe MDL2 Assets).
const wchar_t kGlyphBackspace = (wchar_t)0xE94F;
const wchar_t kGlyphDivide = (wchar_t)0xE94A;
const wchar_t kGlyphMultiply = (wchar_t)0xE947;
const wchar_t kGlyphMinus = (wchar_t)0xE949;
const wchar_t kGlyphPlus = (wchar_t)0xE948;
const wchar_t kGlyphEquals = (wchar_t)0xE94E;
const wchar_t kGlyphPin = (wchar_t)0xE718;
const wchar_t kGlyphUnpin = (wchar_t)0xE77A;
const wchar_t kGlyphMenu = (wchar_t)0xE700;

}  // namespace ui
