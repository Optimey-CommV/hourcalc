// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// The check box of the menu (Keep on top). Fusion draws its frame in a darker shade of the window colour, and in a dark scheme
// that is the colour of the menu itself: the empty box cannot be seen (measured 1.05 : 1, in the light scheme 1.9 : 1) and the
// checked one shows a thin tick that seems to float. Breeze, the style of the target desktop, draws a visible box of its own
// and is left alone: this style only goes onto the menu when the application style is Fusion (what Qt falls back to without a
// desktop theme, and what the headless tests use), and it only replaces the drawing of the check box inside a menu. Everything
// else is the Fusion of the application.

#pragma once

#include <QProxyStyle>

class QMenu;

namespace hcqt {

class PopupStyle : public QProxyStyle {
public:
    // Makes the style of 'menu' what it should be for the application style: this style (which the menu owns) under Fusion, the
    // application style otherwise. True when the menu has this style afterwards.
    static bool sync(QMenu* menu);

    void drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget = nullptr) const override;

private:
    explicit PopupStyle(const QString& key) : QProxyStyle(key) {}
};

}  // namespace hcqt
