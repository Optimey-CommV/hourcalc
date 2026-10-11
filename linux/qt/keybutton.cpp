// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "keybutton.h"

#include "util.h"

#include <QEvent>
#include <QIcon>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOptionButton>
#include <QStylePainter>

namespace hcqt {

KeyButton::KeyButton(const QString& command, const QString& label, Kind kind, Glyph glyph, QWidget* parent)
    : QPushButton(glyph == NoGlyph ? label : QString(), parent), command_(command), kind_(kind), glyph_(glyph) {
    setFocusPolicy(Qt::NoFocus);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setAutoDefault(false);
    setDefault(kind == Accent);
    applyFont();
}

void KeyButton::setScale(double scale) {
    scale_ = scale;
    applyFont();
}

void KeyButton::applyFont() {
    // Digits are large, the function keys and the symbols are the size of the interface, Convert in between.
    double factor = 1.0;
    switch (kind_) {
    case Digit: factor = 1.45; break;
    case Function: factor = 1.1; break;
    case Accent: factor = glyph_ == NoGlyph ? 1.25 : 1.1; break;
    }
    setFont(scaledFont(factor * scale_));
}

// Every key reports the same size, whatever its font: the grid then gives every row and column the same share.
QSize KeyButton::sizeHint() const {
    const double u = unitPx();
    return QSize(qRound(u * 3.6), qRound(u * 2.6));
}

QSize KeyButton::minimumSizeHint() const {
    const double u = unitPx();
    return QSize(qRound(u * 2.2), qRound(u * 1.7));
}

void KeyButton::changeEvent(QEvent* event) {
    if (event->type() == QEvent::ApplicationFontChange) applyFont();
    QPushButton::changeEvent(event);
}

// The colour in the middle of the key as the style paints its background for 'option' (the bevel only, drawn into a small
// image).
QColor KeyButton::styledFill(const QStyleOptionButton& option) const {
    QImage image(33, 33, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QStyleOptionButton probe(option);
    probe.rect = image.rect();
    probe.text.clear();
    probe.icon = QIcon();
    {
        QPainter painter(&image);
        style()->drawControl(QStyle::CE_PushButtonBevel, &probe, &painter, this);
    }
    return image.pixelColor(16, 16);
}

void KeyButton::paintEvent(QPaintEvent*) {
    QStylePainter painter(this);
    QStyleOptionButton option;
    initStyleOption(&option);
    if (glyph_ != NoGlyph) option.text.clear();

    if (kind_ == Accent) {
        // The palette's highlight, as the accent of the desktop; derived here, at paint time, so a change of
        // the palette (the system theme, or a forced Light / Dark) needs no handler.
        QPalette& pal = option.palette;
        for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) pal.setColor(group, QPalette::Button, pal.color(group, QPalette::Highlight));
        pal.setColor(QPalette::Disabled, QPalette::Button,
                     blend(pal.color(QPalette::Active, QPalette::Highlight), pal.color(QPalette::Active, QPalette::Window), 0.6));
        // The label: white or dark, whichever reads better on the fill the style really paints (a style lightens and
        // softens the colour of a button: white on Fusion's version of Windows blue is only 3 : 1), not the palette's
        // HighlightedText, which a desktop sets for its own widgets.
        const QColor label = readableOn(styledFill(option));
        for (QPalette::ColorGroup group : {QPalette::Active, QPalette::Inactive}) pal.setColor(group, QPalette::ButtonText, label);
        pal.setColor(QPalette::Disabled, QPalette::ButtonText, blend(label, pal.color(QPalette::Active, QPalette::Window), 0.5));
    }
    painter.drawControl(QStyle::CE_PushButton, option);
    if (glyph_ == NoGlyph) return;

    // The symbol: strokes in the button text colour, centred in the contents of the button.
    const QRectF area = QRectF(style()->subElementRect(QStyle::SE_PushButtonContents, &option, this));
    const QPointF c = area.center();
    const double side = QFontMetricsF(font()).height() * 0.62;       // the extent of the symbol
    const double pen = qMax(1.5, side * 0.13);
    QColor color = option.palette.color(QPalette::ButtonText);
    if (kind_ != Accent && glyph_ != Backspace) color = blend(color, option.palette.color(QPalette::Button), 0.12);

    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setBrush(Qt::NoBrush);
    painter.setPen(QPen(color, pen, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    const double h = side / 2;
    switch (glyph_) {
    case Plus:
        painter.drawLine(QPointF(c.x() - h, c.y()), QPointF(c.x() + h, c.y()));
        painter.drawLine(QPointF(c.x(), c.y() - h), QPointF(c.x(), c.y() + h));
        break;
    case Minus:
        painter.drawLine(QPointF(c.x() - h, c.y()), QPointF(c.x() + h, c.y()));
        break;
    case Times: {
        const double d = h * 0.78;
        painter.drawLine(QPointF(c.x() - d, c.y() - d), QPointF(c.x() + d, c.y() + d));
        painter.drawLine(QPointF(c.x() - d, c.y() + d), QPointF(c.x() + d, c.y() - d));
        break;
    }
    case Divide: {
        painter.drawLine(QPointF(c.x() - h, c.y()), QPointF(c.x() + h, c.y()));
        painter.setPen(Qt::NoPen);
        painter.setBrush(color);
        const double r = pen * 0.85, off = h * 0.62;
        painter.drawEllipse(QPointF(c.x(), c.y() - off), r, r);
        painter.drawEllipse(QPointF(c.x(), c.y() + off), r, r);
        break;
    }
    case Equals: {
        const double off = h * 0.38;
        painter.drawLine(QPointF(c.x() - h, c.y() - off), QPointF(c.x() + h, c.y() - off));
        painter.drawLine(QPointF(c.x() - h, c.y() + off), QPointF(c.x() + h, c.y() + off));
        break;
    }
    case Backspace: {
        // The erase key: a label pointing left with a cross in it.
        const double wide = h * 1.25, high = h * 0.78, tip = h * 0.55;
        QPainterPath path;
        path.moveTo(c.x() - wide, c.y());
        path.lineTo(c.x() - wide + tip, c.y() - high);
        path.lineTo(c.x() + wide, c.y() - high);
        path.lineTo(c.x() + wide, c.y() + high);
        path.lineTo(c.x() - wide + tip, c.y() + high);
        path.closeSubpath();
        painter.setPen(QPen(color, qMax(1.3, pen * 0.9), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter.drawPath(path);
        const double x0 = c.x() + wide * 0.12, d = h * 0.34;
        painter.drawLine(QPointF(x0 - d, c.y() - d), QPointF(x0 + d, c.y() + d));
        painter.drawLine(QPointF(x0 - d, c.y() + d), QPointF(x0 + d, c.y() - d));
        break;
    }
    case NoGlyph:
        break;
    }
}

}  // namespace hcqt
