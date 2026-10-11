// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - colours of the light and the dark theme, the system theme and accent, the DWM
// title bar colours. Values follow the Windows 11 Calculator (docs\SPEC.md section 3).

#pragma once

#include "gfx.h"
#include "../core/hc_core.h"

namespace theme {

using gfx::Argb;

// One complete set of colours for a theme.
struct Palette {
    bool dark;
    Argb bg, func, funcHover, digit, digitHover;
    Argb edgeTop, edgeBottom;
    Argb text, textSecondary, textDisabled;
    Argb accent, accentText, accentEdgeBottom;
    Argb subtle, subtlePressed;
    Argb flyout, flyoutEdge;     // menus and tool tips
};

// True when Windows apps use the dark theme (a missing value means light).
bool SystemIsDark();
// The accent colour for a theme: dark1 for light, light2 for dark, from the AccentPalette in the registry.
Argb SystemAccent(bool dark);
// Whether the window is dark for a theme choice: Auto follows the Windows app theme, Light and Dark force one. The accent is
// not part of that choice: it always follows Windows (SystemAccent).
bool IsDarkFor(hc::settings::ThemeMode mode);
#ifdef HC_TESTHOOKS
// Test build only: pretends to be Windows. With 'on', SystemIsDark() answers 'dark' and SystemAccent() the two given colours
// (for the light and the dark theme) instead of reading the registry; the self test and the snapshot tool use it to simulate a
// change of the Windows theme or accent without touching the settings of the machine. 'off' goes back to the registry.
void SetSystemForTest(bool on, bool dark, Argb accentLight, Argb accentDark);

// Test build only: what ApplyCaption last set on a window. DWM can be asked for the immersive dark mode switch, but not for the
// caption and text colours (the attributes are write only), so the colours are remembered here. 'accepted': DWM took all three.
struct CaptionState {
    bool known;
    bool dark;
    COLORREF caption, text;
    bool accepted;
};
CaptionState LastCaptionForTest(HWND hwnd);
#endif
Palette Create(bool dark, Argb accent);
// Makes the native title bar blend with the client area (Windows 11). Failures are ignored.
void ApplyCaption(HWND hwnd, const Palette& p);

}  // namespace theme
