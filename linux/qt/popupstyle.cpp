// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "popupstyle.h"

#include "util.h"

#include <QApplication>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QStyleOption>

namespace hcqt {

bool PopupStyle::sync(QMenu* menu) {
    if (!menu) return false;
    const QString key = QApplication::style()->name();
    const bool wanted = key.compare(QLatin1String("fusion"), Qt::CaseInsensitive) == 0;
    auto* have = dynamic_cast<PopupStyle*>(menu->style());
    if (wanted && !have) {
        auto* style = new PopupStyle(key);
        style->setParent(menu);            // the style goes with the menu
        menu->setStyle(style);
        return true;
    }
    if (!wanted && have) {
        menu->setStyle(nullptr);           // back to the style of the application
        delete have;
        return false;
    }
    return have != nullptr;
}

// The box is drawn from the palette of the menu and in proportion to the rectangle the style hands over (nothing here is a
// pixel size of Fusion): a fill of the Base colour, a frame of the text colour pulled 40 % towards the window colour (4 : 1 or
// better on the Breeze windows and menus, light and dark), and, when checked, a tick in the plain text colour. The fill keeps the
// tick readable on the highlight of the row under the pointer as well.
void PopupStyle::drawPrimitive(PrimitiveElement element, const QStyleOption* option, QPainter* painter, const QWidget* widget) const {
    if (element != PE_IndicatorCheckBox || !option || !painter || !qobject_cast<const QMenu*>(widget)) {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
    const QPalette& pal = option->palette;
    const QColor text = pal.color(QPalette::Text);
    const QColor frame = blend(text, pal.color(QPalette::Window), 0.40);
    const QRectF box(option->rect);
    const qreal side = qMin(box.width(), box.height());

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing, true);
    painter->setPen(QPen(frame, qMax<qreal>(1.0, side * 0.08)));
    painter->setBrush(pal.color(QPalette::Base));
    const qreal edge = qMax<qreal>(1.0, side * 0.08) / 2.0;
    painter->drawRoundedRect(box.adjusted(edge, edge, -edge, -edge), side * 0.14, side * 0.14);
    if (option->state & State_On) {
        QPainterPath tick;
        tick.moveTo(box.left() + box.width() * 0.22, box.top() + box.height() * 0.52);
        tick.lineTo(box.left() + box.width() * 0.42, box.top() + box.height() * 0.72);
        tick.lineTo(box.left() + box.width() * 0.78, box.top() + box.height() * 0.28);
        painter->setPen(QPen(text, qMax<qreal>(1.6, side * 0.14), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
        painter->drawPath(tick);
    }
    painter->restore();
}

}  // namespace hcqt
