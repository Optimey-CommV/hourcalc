// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Small helpers shared by the Qt front end: std::wstring <-> QString (the core speaks std::wstring,
// wchar_t is UTF-32 on Linux), colour blending, and font sizing from the application font.

#pragma once

#include <QApplication>
#include <QColor>
#include <QFont>
#include <QFontMetricsF>
#include <QKeyEvent>
#include <QPalette>
#include <QString>

#include <algorithm>
#include <cmath>
#include <string>

namespace hcqt {

inline QString q(const std::wstring& s) { return QString::fromStdWString(s); }
inline std::wstring w(const QString& s) { return s.toStdWString(); }

// a * (1 - t) + b * t
inline QColor blend(const QColor& a, const QColor& b, double t) {
    return QColor::fromRgbF(a.redF() * (1.0 - t) + b.redF() * t, a.greenF() * (1.0 - t) + b.greenF() * t,
                            a.blueF() * (1.0 - t) + b.blueF() * t, a.alphaF() * (1.0 - t) + b.alphaF() * t);
}

inline bool isDarkPalette(const QPalette& p) { return p.color(QPalette::Window).lightness() < 128; }

// WCAG 2.x relative luminance and contrast ratio (1 = the same colour, 21 = black on white).
inline double relativeLuminance(const QColor& c) {
    auto linear = [](double v) { return v <= 0.03928 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4); };
    return 0.2126 * linear(c.redF()) + 0.7152 * linear(c.greenF()) + 0.0722 * linear(c.blueF());
}

inline double contrastRatio(const QColor& a, const QColor& b) {
    const double la = relativeLuminance(a), lb = relativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// The colour of a link on 'background': 'link' itself when it reads well enough (WCAG contrast of 'minContrast'), else 'link'
// pulled towards white or black, whichever the background allows, in 1 % steps, so the hue stays and only the lightness moves.
// 4.5 : 1 is the AA level for text; the default asks for a little more, so that another tool's rounding of the same colours
// still lands on 4.5 or above. The result is an 8 bit colour (what the markup of a label can say), tested as such.
inline QColor readableLink(const QColor& link, const QColor& background, double minContrast = 4.6) {
    const QColor bg(background.rgb());
    const QColor start(link.isValid() ? link.rgb() : QColor(0x1d, 0x99, 0xf3).rgb());
    if (contrastRatio(start, bg) >= minContrast) return start;
    const QColor white(0xff, 0xff, 0xff), black(0, 0, 0);
    const QColor end = contrastRatio(bg, white) >= contrastRatio(bg, black) ? white : black;
    QColor out = end;
    for (int step = 1; step <= 100; ++step) {
        out = QColor(blend(start, end, step / 100.0).rgb());
        if (contrastRatio(out, bg) >= minContrast) break;
    }
    return out;
}

// The text colour for a label on 'fill': white or the dark text colour of Breeze, whichever has the higher contrast
// (a lightness threshold gets the middle blues wrong: white on Breeze blue #3daee9 is only 2.5 : 1, the dark text 6 : 1).
inline QColor readableOn(const QColor& fill) {
    const QColor white(0xff, 0xff, 0xff), dark(0x23, 0x26, 0x27);
    return contrastRatio(fill, white) >= contrastRatio(fill, dark) ? white : dark;
}

// The text colour of captions, the expression line and the field labels: the text colour, drawn back
// towards the window colour. Derived at paint time from the palette of the widget, so it follows every
// theme and every forced light / dark choice.
inline QColor secondaryText(const QPalette& p) {
    return blend(p.color(QPalette::Active, QPalette::WindowText), p.color(QPalette::Active, QPalette::Window), 0.32);
}

// Ctrl+1 and Ctrl+2 as keys of the number row: 0 or 1 (the tab) when 'event' is Ctrl (alone) with the first or the second
// key of the number row, whatever it types on this layout; -1 otherwise. The key numbers are the xkb key codes (evdev code
// + 8), the same on X11 and Wayland: 10 is the key right of Esc and `, 11 the next one. A layout whose digits are on the
// shifted level of those keys (Belgian and French AZERTY: & and e-acute) would otherwise need Ctrl+Shift. Keys that are
// the digit itself match through the shortcuts as well, so this only adds.
inline int digitRowTab(const QKeyEvent* event) {
    const Qt::KeyboardModifiers mods = event->modifiers() & ~Qt::KeypadModifier;
    if (mods != Qt::ControlModifier) return -1;
    if (event->nativeScanCode() == 10) return 0;
    if (event->nativeScanCode() == 11) return 1;
    return -1;
}

// The base size of the interface in points: the application font, which comes from the platform theme.
inline double basePoints() {
    const QFont f = QApplication::font();
    if (f.pointSizeF() > 0) return f.pointSizeF();
    if (f.pixelSize() > 0) return f.pixelSize() * 72.0 / 96.0;
    return 10.0;
}

// A copy of the application font at 'factor' times its size.
inline QFont scaledFont(double factor, int weight = -1) {
    QFont f = QApplication::font();
    f.setPointSizeF(basePoints() * factor);
    if (weight >= 0) f.setWeight(static_cast<QFont::Weight>(weight));
    return f;
}

// One layout unit: the line height of the application font. Every size of the front end is a multiple of
// it (or comes from the style), so the window follows the font size and the display scale of the desktop.
inline double unitPx() { return QFontMetricsF(QApplication::font()).height(); }

// The largest point size in [minPt, maxPt] at which 'text' set in 'font' fits 'availWidth' (device independent pixels).
inline double fitPointSize(const QFont& font, const QString& text, double availWidth, double maxPt, double minPt) {
    QFont f(font);
    auto width = [&](double pt) {
        f.setPointSizeF(pt);
        return QFontMetricsF(f).horizontalAdvance(text);
    };
    if (text.isEmpty() || availWidth <= 8.0) return maxPt;
    double wide = width(maxPt);
    if (wide <= availWidth) return maxPt;
    double pt = qMax(minPt, maxPt * availWidth / wide);
    // Hinting makes the width a little non-linear in the size: step down until it fits.
    for (int i = 0; i < 40 && pt > minPt && width(pt) > availWidth; ++i) pt = qMax(minPt, pt * 0.97);
    return pt;
}

// The core reports arithmetic that is out of range by throwing (hc::DecOverflow and the like); an exception must
// never travel through the event loop of Qt. False when something was thrown.
template <class F>
bool guarded(F&& f) noexcept {
    try {
        f();
        return true;
    } catch (...) {
        return false;
    }
}

}  // namespace hcqt
