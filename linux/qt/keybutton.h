// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// One key of the keypad: a plain QPushButton drawn by the desktop's style. The operator and backspace
// symbols are painted as strokes in the button text colour (no font has to carry them), and the
// accent key (Convert, =) takes its colours from the palette's highlight, so it follows the desktop's
// accent colour and its light / dark scheme. Keys never take the keyboard focus: the keypad types into
// the converter field that is active, the way the Windows version does.

#pragma once

#include <QColor>
#include <QPushButton>

class QStyleOptionButton;

namespace hcqt {

class KeyButton : public QPushButton {
    Q_OBJECT
public:
    enum Kind { Digit, Function, Accent };
    enum Glyph { NoGlyph, Backspace, Plus, Minus, Times, Divide, Equals };

    // 'command' is what the page's press() gets: "7", "CE", "BACK", "RESET", "CONVERT", "+", ...
    KeyButton(const QString& command, const QString& label, Kind kind, Glyph glyph, QWidget* parent = nullptr);

    const QString& command() const { return command_; }
    Kind kind() const { return kind_; }
    Glyph glyph() const { return glyph_; }
    // The text a key shows (empty for the symbol keys).
    QString label() const { return text(); }

    // The font of the key follows the window: 1.0 is the size of the default window.
    void setScale(double scale);
    double scale() const { return scale_; }

    QSize sizeHint() const override;
    QSize minimumSizeHint() const override;

protected:
    void paintEvent(QPaintEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void applyFont();
    QColor styledFill(const QStyleOptionButton& option) const;

    QString command_;
    Kind kind_;
    Glyph glyph_;
    double scale_ = 1.0;
};

}  // namespace hcqt
