// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "scheme.h"

#include "util.h"

namespace hcqt {

QPalette schemePalette(bool dark, const QPalette& system) {
    // Breeze Light and Breeze Dark.
    const QColor window = dark ? QColor(0x2a, 0x2e, 0x32) : QColor(0xef, 0xf0, 0xf1);
    const QColor base = dark ? QColor(0x1b, 0x1e, 0x20) : QColor(0xfc, 0xfc, 0xfc);
    const QColor alt = dark ? QColor(0x23, 0x26, 0x29) : QColor(0xef, 0xf0, 0xf1);
    const QColor text = dark ? QColor(0xfc, 0xfc, 0xfc) : QColor(0x23, 0x26, 0x27);
    const QColor windowText = dark ? QColor(0xef, 0xf0, 0xf1) : QColor(0x23, 0x26, 0x27);
    const QColor button = dark ? QColor(0x31, 0x36, 0x3b) : QColor(0xe3, 0xe5, 0xe7);
    const QColor link = dark ? QColor(0x1d, 0x99, 0xf3) : QColor(0x29, 0x80, 0xb9);

    // The accent of the desktop, when the desktop palette has a usable one; Breeze blue otherwise.
    QColor highlight = system.color(QPalette::Active, QPalette::Highlight);
    if (!highlight.isValid() || highlight.alpha() == 0 || highlight.saturation() < 40 || highlight.lightness() < 50 || highlight.lightness() > 200)
        highlight = QColor(0x3d, 0xae, 0xe9);
    const QColor highlightedText = readableOn(highlight);

    QPalette p(button, window);       // fills the bevel colours (Light, Midlight, Dark, Mid, Shadow) from the button
    for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled}) {
        const bool off = group == QPalette::Disabled;
        auto soften = [&](const QColor& c) { return off ? blend(c, window, 0.55) : c; };
        p.setColor(group, QPalette::Window, window);
        p.setColor(group, QPalette::WindowText, soften(windowText));
        p.setColor(group, QPalette::Base, base);
        p.setColor(group, QPalette::AlternateBase, alt);
        p.setColor(group, QPalette::Text, soften(text));
        p.setColor(group, QPalette::Button, button);
        p.setColor(group, QPalette::ButtonText, soften(windowText));
        p.setColor(group, QPalette::BrightText, Qt::white);
        p.setColor(group, QPalette::Highlight, off ? blend(highlight, window, 0.6) : highlight);
        p.setColor(group, QPalette::HighlightedText, highlightedText);
        p.setColor(group, QPalette::Link, link);
        p.setColor(group, QPalette::LinkVisited, dark ? QColor(0x9b, 0x59, 0xb6) : QColor(0x7f, 0x8c, 0x8d));
        p.setColor(group, QPalette::ToolTipBase, dark ? QColor(0x31, 0x36, 0x3b) : QColor(0xf7, 0xf7, 0xf7));
        p.setColor(group, QPalette::ToolTipText, windowText);
        p.setColor(group, QPalette::PlaceholderText, blend(text, base, 0.45));
        p.setColor(group, QPalette::Accent, highlight);
    }
    return p;
}

QPalette pinnedPalette(const QPalette& palette) {
    QPalette p(palette);
    for (int g = 0; g < QPalette::NColorGroups; ++g) {
        for (int r = 0; r < QPalette::NColorRoles; ++r) {
            const auto group = static_cast<QPalette::ColorGroup>(g);
            const auto role = static_cast<QPalette::ColorRole>(r);
            p.setBrush(group, role, palette.brush(group, role));
        }
    }
    return p;
}

}  // namespace hcqt
