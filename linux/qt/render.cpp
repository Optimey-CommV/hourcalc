// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// --render <dir>: PNGs of the window in its states, headless (QWidget::grab() on the offscreen platform).
//
// Names: <variant>-<state>[-min].png, where the variant says what the desktop does and what the user chose:
//   light         a light desktop, theme Auto
//   dark          a dark desktop (its palette stands in for what a dark Plasma or GNOME gives), theme Auto
//   forced-light  a dark desktop, the user chose Light
//   forced-dark   a light desktop, the user chose Dark
// "-min" is the minimum window size. The offscreen platform has no desktop theme and no Breeze: the style
// is Fusion, so these show the layout with Fusion's metrics, not Breeze's look.

#include "testhooks.h"

#include "aboutdialog.h"
#include "calculatorpage.h"
#include "converterpage.h"
#include "displaywidgets.h"
#include "mainwindow.h"
#include "scheme.h"
#include "util.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QImage>
#include <QLayout>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QTemporaryDir>
#include <QTest>

#include <cstdio>
#include <functional>

namespace hcqt {
namespace testing {

namespace {

QString g_dir;
int g_written = 0;
bool g_failed = false;

void settle(QWidget* w) {
    for (int i = 0; i < 4; ++i) {
        QCoreApplication::sendPostedEvents();
        QCoreApplication::processEvents();
        if (w->layout()) w->layout()->activate();
        QTest::qWait(15);
    }
}

bool save(const QPixmap& pm, const QString& name) {
    const QString path = QDir(g_dir).filePath(name + QStringLiteral(".png"));
    if (pm.isNull() || !pm.save(path, "PNG")) {
        std::fprintf(stderr, "cannot write %s\n", path.toLocal8Bit().constData());
        g_failed = true;
        return false;
    }
    ++g_written;
    return true;
}

void keys(MainWindow& w, const char* sequence) {
    for (const char* p = sequence; *p; ++p) w.press(QString(QLatin1Char(*p)));
}

// What is on the window for a state: the tab, the typed text, the keypad.
void drive(MainWindow& w, const QString& state) {
    ConverterPage* conv = w.converter();
    if (state == QLatin1String("converter-idle")) {
        w.selectTab(0);
        conv->press(QStringLiteral("RESET"));
    } else if (state == QLatin1String("converter-left")) {
        w.selectTab(0);
        conv->press(QStringLiteral("RESET"));
        conv->setFieldText(0, QStringLiteral("8:30"));
        conv->press(QStringLiteral("CONVERT"));
        conv->edit(0)->setFocus(Qt::OtherFocusReason);
    } else if (state == QLatin1String("converter-right")) {
        w.selectTab(0);
        conv->press(QStringLiteral("RESET"));
        conv->setFieldText(1, QStringLiteral("8") + q(hc::timemath::DecimalSeparator()) + QStringLiteral("5"));
        conv->press(QStringLiteral("CONVERT"));
        conv->edit(1)->setFocus(Qt::OtherFocusReason);
    } else if (state == QLatin1String("converter-long")) {
        w.selectTab(0);
        conv->press(QStringLiteral("RESET"));
        conv->setFieldText(0, QStringLiteral("9999999:99"));
    } else if (state == QLatin1String("calc-result")) {
        w.selectTab(1);
        keys(w, "7:30+7:20=");
    } else if (state == QLatin1String("calc-entry")) {
        w.selectTab(1);
        keys(w, "7:30+7:2");
    } else if (state == QLatin1String("calc-divzero")) {
        w.selectTab(1);
        keys(w, "7:30/0=");
    } else if (state == QLatin1String("calc-chain")) {
        w.selectTab(1);
        for (int i = 0; i < 14; ++i) keys(w, "7:30+");
    } else if (state == QLatin1String("calc-negative")) {
        w.selectTab(1);
        keys(w, "7:00-8:30=");
    }
}

QPixmap grabWindow(MainWindow& w, const QSize& size) {
    w.resize(size);
    w.show();
    settle(&w);
    return w.grab();
}

// The window with its menu open below the menu button, in one picture. 'hover': the item the pointer is on (highlighted).
QPixmap grabMenu(MainWindow& w, const QSize& size, QAction* hover = nullptr) {
    w.resize(size);
    w.show();
    settle(&w);
    w.openMenu();
    settle(w.menu());
    if (hover) {
        w.menu()->setActiveAction(hover);
        settle(w.menu());
    }
    const QPixmap window = w.grab();
    const QPixmap menu = w.menu()->grab();
    const QPoint at = w.mapFromGlobal(w.menu()->pos());
    QRect all = QRect(QPoint(0, 0), window.size()).united(QRect(at, menu.size()));
    QImage canvas(all.size(), QImage::Format_ARGB32_Premultiplied);
    canvas.fill(w.palette().color(QPalette::Window));
    QPainter p(&canvas);
    p.drawPixmap(-all.topLeft(), window);
    p.drawPixmap(at - all.topLeft(), menu);
    p.end();
    w.menu()->hide();
    return QPixmap::fromImage(canvas);
}

QPixmap grabAbout(MainWindow& w) {
    AboutDialog* d = w.createAbout();
    d->show();
    settle(d);
    const QPixmap pm = d->grab();
    d->hide();
    delete d;
    return pm;
}

struct Variant {
    QString prefix;
    bool systemDark;
    MainWindow::ThemeMode mode;
    bool full;      // all states and both sizes, or the short list
};

// A picture of all the states of a variant at the default size, for a quick look.
void contactSheet(const QString& name, const QList<QPixmap>& shots) {
    if (shots.isEmpty()) return;
    int wsum = 0, h = 0;
    for (const QPixmap& s : shots) {
        wsum += s.width() + 8;
        h = qMax(h, s.height());
    }
    QImage sheet(wsum + 8, h + 16, QImage::Format_ARGB32_Premultiplied);
    sheet.fill(QColor(0x88, 0x88, 0x88));
    QPainter p(&sheet);
    int x = 8;
    for (const QPixmap& s : shots) {
        p.drawPixmap(x, 8, s);
        x += s.width() + 8;
    }
    p.end();
    save(QPixmap::fromImage(sheet), name);
}

}  // namespace

int runRender(const QString& dir) {
    g_dir = dir;
    if (!QDir().mkpath(dir)) {
        std::fprintf(stderr, "cannot create %s\n", dir.toLocal8Bit().constData());
        return 1;
    }
    hc::timemath::SetDecimalSeparatorOverride(L",");        // the owner's nl-BE: 8,50

    const QPalette original = QApplication::palette();
    const QStringList stateList = {QStringLiteral("converter-idle"), QStringLiteral("converter-left"), QStringLiteral("converter-right"),
                                   QStringLiteral("converter-long"), QStringLiteral("calc-result"), QStringLiteral("calc-entry"),
                                   QStringLiteral("calc-divzero"), QStringLiteral("calc-chain"), QStringLiteral("calc-negative")};
    const QStringList shortList = {QStringLiteral("converter-idle"), QStringLiteral("calc-result")};

    const Variant variants[] = {
        {QStringLiteral("light"), false, MainWindow::ThemeMode::Auto, true},
        {QStringLiteral("dark"), true, MainWindow::ThemeMode::Auto, true},
        {QStringLiteral("forced-light"), true, MainWindow::ThemeMode::Light, false},
        {QStringLiteral("forced-dark"), false, MainWindow::ThemeMode::Dark, false},
    };

    for (const Variant& v : variants) {
        QApplication::setPalette(v.systemDark ? schemePalette(true, original) : original);
        QList<QPixmap> sheet, sheetMin;
        const QStringList& states = v.full ? stateList : shortList;
        for (int sizeIndex = 0; sizeIndex < (v.full ? 2 : 1); ++sizeIndex) {
            for (const QString& state : states) {
                MainWindow w(QString(), 1);
                w.setThemeMode(v.mode);
                const QSize size = sizeIndex == 0 ? w.defaultSize() : w.minimumWindowSize();
                drive(w, state);
                const QPixmap pm = grabWindow(w, size);
                save(pm, v.prefix + QLatin1Char('-') + state + (sizeIndex == 0 ? QString() : QStringLiteral("-min")));
                (sizeIndex == 0 ? sheet : sheetMin) << pm;
                w.hide();
            }
        }
        // the menu (with Keep on top, as on X11) and the one without it (Wayland), and the About dialog
        {
            MainWindow w(QString(), 1);
            w.setThemeMode(v.mode);
            save(grabMenu(w, w.defaultSize()), v.prefix + QStringLiteral("-menu"));
            w.hide();
        }
        {
            // the same with Keep on top switched on: the checked box next to the checked radio items
            MainWindow w(QString(), 1);
            w.setThemeMode(v.mode);
            w.setKeepOnTop(true);
            save(grabMenu(w, w.defaultSize()), v.prefix + QStringLiteral("-menu-keep-on-top"));
            w.hide();
        }
        {
            // ... and with the pointer on it: the tick on the highlight of the row
            MainWindow w(QString(), 1);
            w.setThemeMode(v.mode);
            w.setKeepOnTop(true);
            save(grabMenu(w, w.defaultSize(), w.keepOnTopAction()), v.prefix + QStringLiteral("-menu-keep-on-top-hover"));
            w.hide();
        }
        if (v.full) {
            MainWindow w(QString(), 0);
            w.setThemeMode(v.mode);
            save(grabMenu(w, w.defaultSize()), v.prefix + QStringLiteral("-menu-wayland"));
            w.hide();
            MainWindow w2(QString(), 1);
            w2.setThemeMode(v.mode);
            w2.setDecimalPlaces(4);
            save(grabMenu(w2, w2.defaultSize()), v.prefix + QStringLiteral("-menu-4-places"));
            w2.hide();
        }
        {
            MainWindow w(QString(), 0);
            w.setThemeMode(v.mode);
            save(grabAbout(w), v.prefix + QStringLiteral("-about"));
        }
        contactSheet(QStringLiteral("sheet-") + v.prefix, sheet);
        contactSheet(QStringLiteral("sheet-") + v.prefix + QStringLiteral("-min"), sheetMin);
    }
    // A forced theme that was saved: the window and its menu start in it (the menu used to start in the palette of the desktop).
    {
        QTemporaryDir config;
        struct Saved {
            QString prefix;
            bool systemDark;
            MainWindow::ThemeMode mode;
        };
        for (const Saved& s : {Saved{QStringLiteral("saved-dark"), false, MainWindow::ThemeMode::Dark},
                               Saved{QStringLiteral("saved-light"), true, MainWindow::ThemeMode::Light}}) {
            QApplication::setPalette(s.systemDark ? schemePalette(true, original) : original);
            const QString path = config.filePath(s.prefix + QStringLiteral(".ini"));
            hc::settings::SaveTheme(w(path), s.mode, 2);
            MainWindow window(path, 1);
            save(grabMenu(window, window.defaultSize()), s.prefix + QStringLiteral("-menu-at-start"));
            window.hide();
        }
    }
    QApplication::setPalette(original);
    std::printf("%d pictures written to %s\n", g_written, dir.toLocal8Bit().constData());
    return g_failed ? 1 : 0;
}

}  // namespace testing
}  // namespace hcqt
