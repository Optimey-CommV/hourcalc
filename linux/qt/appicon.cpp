// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "appicon.h"

#include "hcqt_config.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>

#include <cmath>

namespace hcqt {

namespace {

// An SVG arc of the circle (cx, cy, r) from the point 'from' to the point 'to', as a Qt arc.
// 'clockwise' is the SVG sweep flag 1 (clockwise on screen).
void arc(QPainterPath& path, double cx, double cy, double r, QPointF from, QPointF to, bool clockwise) {
    const double kPi = 3.14159265358979323846;
    // Qt angles: degrees, counter-clockwise, 0 at 3 o'clock, y up.
    double a0 = std::atan2(-(from.y() - cy), from.x() - cx) * 180.0 / kPi;
    double a1 = std::atan2(-(to.y() - cy), to.x() - cx) * 180.0 / kPi;
    double sweep = a1 - a0;
    if (clockwise) {
        while (sweep > 0) sweep -= 360.0;
    } else {
        while (sweep < 0) sweep += 360.0;
    }
    path.arcTo(QRectF(cx - r, cy - r, 2 * r, 2 * r), a0, sweep);
}

}  // namespace

QPixmap paintAppIcon(int px) {
    QPixmap pm(px, px);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing, true);
    p.scale(px / 256.0, px / 256.0);

    QLinearGradient g(20, 20, 236, 236);
    g.setColorAt(0, QColor(0x28, 0xC4, 0xE6));
    g.setColorAt(1, QColor(0x0B, 0x7F, 0xAF));
    const QBrush brush(g);

    // Three solid quarters (fill and a 20 unit stroke of the same gradient: rounded corners).
    auto quarter = [&](QPointF corner, QPointF a, QPointF b, bool clockwise) {
        QPainterPath path;
        path.moveTo(corner);
        path.lineTo(a);
        arc(path, 128, 128, 108, a, b, clockwise);
        path.closeSubpath();
        p.setBrush(brush);
        p.setPen(QPen(brush, 20, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    };
    quarter({149, 107}, {149, 22.06}, {233.94, 107}, true);
    quarter({149, 149}, {149, 233.94}, {233.94, 149}, false);
    quarter({107, 149}, {107, 233.94}, {22.06, 149}, true);

    // The fourth quarter is an outline: 45 min = 0.75 h.
    {
        QPainterPath path;
        path.moveTo(110, 110);
        path.lineTo(110, 18.47);
        arc(path, 128, 128, 111, {110, 18.47}, {18.47, 110}, false);
        path.closeSubpath();
        p.setBrush(Qt::NoBrush);
        p.setPen(QPen(brush, 14, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.drawPath(path);
    }

    // The clock hand to 12.
    p.setBrush(Qt::NoBrush);
    p.setPen(QPen(QColor(0x1C, 0xA8, 0xD0), 30, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(128, 128), QPointF(128, 52));
    p.setPen(QPen(Qt::white, 20, Qt::SolidLine, Qt::RoundCap));
    p.drawLine(QPointF(128, 128), QPointF(128, 52));
    p.end();
    return pm;
}

QIcon appIcon() {
    QIcon icon = QIcon::fromTheme(QStringLiteral(HC_APP_ID));
    if (!icon.isNull()) return icon;
    QIcon painted;
    for (int px : {16, 24, 32, 48, 64, 128, 256}) painted.addPixmap(paintAppIcon(px));
    return painted;
}

}  // namespace hcqt
