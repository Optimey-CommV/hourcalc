// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Forced Light / Dark (SPEC 6b). QStyleHints::setColorScheme() asks the platform theme for the scheme; a
// platform theme that does not implement the request (the generic Unix theme, offscreen, plasma-integration
// where the colours come from the user's colour scheme file) ignores it. The window then gets a palette of
// its own for the scheme: the Breeze light / dark colours, with the accent (the highlight) of the desktop.

#pragma once

#include <QPalette>

namespace hcqt {

// A palette of the requested lightness. 'system' is the palette of the desktop: its accent and its disabled /
// inactive treatment are kept.
QPalette schemePalette(bool dark, const QPalette& system);

// A copy of a palette in which every role is set explicitly. A widget whose palette is set explicitly stops following
// the palette of the application; this is what a forced Light or Dark needs when the desktop is in that scheme already.
QPalette pinnedPalette(const QPalette& palette);

}  // namespace hcqt
