// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// The proportions of the window. Nothing in the front end is a pixel size: every size is a multiple of
// the line height of the application font (the "unit") or comes from the style, so the window fits the
// font and the display scale of the desktop, and the metrics of any style (Fusion, Breeze, ...).
// The reference sizes are those of the Windows window (340 x 560 DIP with a 14 px Segoe UI) in units.

#pragma once

#include <QSize>

#include <algorithm>

namespace hcqt {

// The default window and its minimum, in units.
constexpr double kDefaultWidth = 20.0;
constexpr double kDefaultHeight = 34.0;
constexpr double kMinimumWidth = 17.0;
constexpr double kMinimumHeight = 28.0;

// The area of a page (below the header) that the fonts scale against: at this size the fonts have
// their nominal size (scale 1.0).
constexpr double kPageRefWidth = 20.0;
constexpr double kPageRefHeight = 30.0;

constexpr double kMinScale = 0.85;
constexpr double kMaxScale = 2.0;

// Font sizes as multiples of the interface font.
constexpr double kValueFactor = 2.9;         // the big values at scale 1.0 (40 px next to a 14 px interface)
constexpr double kValueMinFactor = 1.15;     // a value never shrinks below this (16 px)
constexpr double kCalcLeftMinFactor = 0.86;  // the left value of the calculator may shrink further (12 px)
constexpr double kErrorFactor = 0.65;        // the error text ("Cannot divide by zero")
constexpr double kLabelFactor = 0.9;         // "Hours : minutes", "Decimal hours"
constexpr double kCaptionFactor = 1.0;       // the value in words, the expression line

// The scale of the fonts for a page of this size.
inline double windowScale(const QSize& size, double unit, double refWidth, double refHeight) {
    if (unit <= 0) return 1.0;
    const double s = std::min(size.width() / (refWidth * unit), size.height() / (refHeight * unit));
    return std::max(kMinScale, std::min(kMaxScale, s));
}

}  // namespace hcqt
