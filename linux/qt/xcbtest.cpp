// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// --xcb-selftest: the part of the behaviour that depends on the real platform plugin, not on events made up by the test:
// real key presses (the XTest extension of the virtual display) that travel X server -> xcb plugin -> keyboard map ->
// shortcut map -> widgets. It checks Ctrl+1 / Ctrl+2 on a US and on an AZERTY layout (the digits are on the shifted level
// of the number row there), Ctrl+Q and Ctrl+W, and Keep on top (the _NET_WM_STATE message to a window manager - here
// a minimal one of the test's own, the virtual display has none). Run it with linux/qt/run-headless.sh (Xvfb); where
// libXtst is not installed it reports that it did not run (exit code 77, "skipped" for meson).

#include "testhooks.h"

#ifdef HC_TEST_X11

#include "converterpage.h"
#include "mainwindow.h"
#include "util.h"
#include "x11helper.h"

#include <QAction>
#include <QApplication>
#include <QElapsedTimer>
#include <QGuiApplication>
#include <QTabBar>
#include <QTest>
#include <QWindow>

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>

namespace hcqt {
namespace testing {

namespace {

int g_checks = 0;
int g_failures = 0;
X11Env* g_x = nullptr;
int g_ctrl = 0, g_shift = 0;

void yes(const QString& what, bool ok) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        std::fprintf(stderr, "FAIL [xcb] %s\n", what.toLocal8Bit().constData());
    }
}

// Waits while handling what the X server sends: the requests of the window manager and Qt's events.
void wait(int ms) {
    QElapsedTimer t;
    t.start();
    do {
        g_x->pump();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qSleep(5);
    } while (t.elapsed() < ms);
}

bool until(const std::function<bool()>& done, int ms = 2500) {
    QElapsedTimer t;
    t.start();
    while (!done() && t.elapsed() < ms) {
        g_x->pump();
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QTest::qSleep(5);
    }
    return done();
}

// A key, with Ctrl and / or Shift held.
void tap(int keycode, bool ctrl = false, bool shift = false) {
    if (ctrl) g_x->key(g_ctrl, true);
    if (shift) g_x->key(g_shift, true);
    g_x->key(keycode, true);
    g_x->key(keycode, false);
    if (shift) g_x->key(g_shift, false);
    if (ctrl) g_x->key(g_ctrl, false);
    g_x->flush();
    wait(80);
}

bool setLayout(const char* name) {
    const std::string command = std::string("setxkbmap ") + name + " >/dev/null 2>&1";
    const bool done = std::system(command.c_str()) == 0;
    wait(500);              // the keyboard map change reaches the X server clients
    g_ctrl = g_x->keycodeForKeysym("Control_L");
    g_shift = g_x->keycodeForKeysym("Shift_L");
    return done;
}

std::unique_ptr<MainWindow> openWindow(int keepOnTop = -1) {
    auto w = std::make_unique<MainWindow>(QString(), keepOnTop);
    w->show();
    yes(QStringLiteral("the window becomes the active window"), until([&] { return w->isActiveWindow(); }));
    wait(100);
    return w;
}

QString left(MainWindow& w) { return w.converter()->fieldText(0); }

}  // namespace

int runXcbSelfTest() {
    std::setvbuf(stdout, nullptr, _IOLBF, 0);          // what has run is on the screen even when a later step crashes
    X11Env x;
    g_x = &x;
    std::string why;
    if (!x.open(&why)) {
        std::fprintf(stderr, "xcb selftest: %s\n", why.c_str());
        return 1;
    }
    if (!x.hasXTest()) {
        std::printf("xcb selftest: libXtst is not installed here, so no real key press can be sent: SKIPPED\n");
        return 77;
    }
    const bool ownWm = x.startWindowManager();
    std::printf("xcb selftest: platform %s, window manager: %s\n", QGuiApplication::platformName().toLocal8Bit().constData(),
                ownWm ? "the test's own (minimal)" : "another one is running");
    QGuiApplication::setQuitOnLastWindowClosed(false);

    // ---- a US keyboard
    yes(QStringLiteral("the layout can be set to us"), setLayout("us"));
    {
        auto wp = openWindow();
        MainWindow& w = *wp;
        const int k1 = x.keycodeForKeysym("1"), k2 = x.keycodeForKeysym("2"), kTab = x.keycodeForKeysym("Tab");
        yes(QStringLiteral("us: the digit keys are the keys 10 and 11 (%1, %2)").arg(k1).arg(k2), k1 == 10 && k2 == 11);
        tap(k2, true);
        yes(QStringLiteral("us: Ctrl+2 selects the calculator"), until([&] { return w.currentTab() == 1; }));
        tap(k1, true);
        yes(QStringLiteral("us: Ctrl+1 selects the converter"), until([&] { return w.currentTab() == 0; }));
        tap(kTab, true);
        yes(QStringLiteral("us: Ctrl+Tab selects the calculator"), until([&] { return w.currentTab() == 1; }));
        tap(kTab, true);
        yes(QStringLiteral("us: Ctrl+Tab selects the converter again"), until([&] { return w.currentTab() == 0; }));
        for (const char* digit : {"8", "3", "0"}) tap(x.keycodeForKeysym(digit));
        yes(QStringLiteral("us: 8 3 0 typed into the left field (%1)").arg(left(w)), left(w) == QStringLiteral("830"));
        tap(k1, true);
        yes(QStringLiteral("us: Ctrl+1 types nothing into the field"), left(w) == QStringLiteral("830"));
    }

    // ---- a Belgian AZERTY keyboard: & and e-acute on the keys 10 and 11, the digits one level up
    if (setLayout("be") && x.keysymAt(10, 0) == "ampersand" && x.keysymAt(10, 1) == "1" && x.keysymAt(11, 1) == "2") {
        std::printf("xcb selftest: layout be (key 10: %s / %s, key 11: %s / %s)\n", x.keysymAt(10, 0).c_str(), x.keysymAt(10, 1).c_str(), x.keysymAt(11, 0).c_str(),
                    x.keysymAt(11, 1).c_str());
        for (int start = 0; start < 2; ++start) {
            auto wp = openWindow();
            MainWindow& w = *wp;
            const QString from = QStringLiteral("(from the %1)").arg(start == 0 ? "converter" : "calculator");
            w.selectTab(start);
            wait(100);
            tap(11, true);
            yes(QStringLiteral("be: Ctrl + the key of 2 (unshifted) selects the calculator %1").arg(from), until([&] { return w.currentTab() == 1; }));
            tap(10, true);
            yes(QStringLiteral("be: Ctrl + the key of 1 (unshifted) selects the converter %1").arg(from), until([&] { return w.currentTab() == 0; }));
            yes(QStringLiteral("be: ... and the field took no character (%1)").arg(left(w)), left(w).isEmpty());
            tap(11, true, true);
            yes(QStringLiteral("be: Ctrl+Shift + the key of 2 selects the calculator %1").arg(from), until([&] { return w.currentTab() == 1; }));
            tap(10, true, true);
            yes(QStringLiteral("be: Ctrl+Shift + the key of 1 selects the converter %1").arg(from), until([&] { return w.currentTab() == 0; }));
            tap(10, false, true);
            tap(11, false, true);
            yes(QStringLiteral("be: Shift + the keys types the digits 1 and 2 (%1)").arg(left(w)), left(w) == QStringLiteral("12"));
            tap(10);
            yes(QStringLiteral("be: the unshifted key types no digit (%1)").arg(left(w)), left(w) == QStringLiteral("12"));
        }
    } else {
        std::printf("xcb selftest: the Belgian layout could not be set here (setxkbmap): the AZERTY checks did not run\n");
    }

    // ---- Ctrl+W and Ctrl+Q close the window (on the Belgian layout, Q is the key of A, so the key code is looked up)
    for (const char* name : {"w", "q"}) {
        auto wp = openWindow();
        MainWindow& w = *wp;
        tap(x.keycodeForKeysym(name), true);
        yes(QStringLiteral("Ctrl+%1 closes the window").arg(QString::fromLatin1(name).toUpper()), until([&] { return !w.isVisible(); }));
    }
    yes(QStringLiteral("the layout can be set back to us"), setLayout("us"));
    for (const char* name : {"w", "q"}) {
        auto wp = openWindow();
        MainWindow& w = *wp;
        tap(x.keycodeForKeysym(name), true);
        yes(QStringLiteral("us: Ctrl+%1 closes the window").arg(QString::fromLatin1(name).toUpper()), until([&] { return !w.isVisible(); }));
    }

    // ---- Keep on top: the window asks the window manager for _NET_WM_STATE_ABOVE, and takes it back
    if (ownWm) {
        auto wp = openWindow(1);
        MainWindow& w = *wp;
        QAction* keep = w.keepOnTopAction();
        yes(QStringLiteral("Keep on top is offered on the xcb platform"), keep != nullptr);
        const unsigned long xid = static_cast<unsigned long>(w.winId());
        auto above = [&] {
            for (const std::string& s : x.netWmState(xid))
                if (s == "_NET_WM_STATE_ABOVE") return true;
            return false;
        };
        yes(QStringLiteral("the window is not above at first"), !above());
        if (keep) {
            keep->trigger();
            yes(QStringLiteral("Keep on top on: the window manager got _NET_WM_STATE_ABOVE for the mapped window"), until(above));
            keep->trigger();
            yes(QStringLiteral("Keep on top off: and the state is taken back"), until([&] { return !above(); }));
        }
    } else {
        std::printf("xcb selftest: another window manager runs, the Keep on top check did not run\n");
    }

    std::printf("xcb selftest: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

}  // namespace testing
}  // namespace hcqt

#else  // built without the X11 library: the test does not run

#include <cstdio>

namespace hcqt {
namespace testing {

int runXcbSelfTest() {
    std::printf("xcb selftest: built without the X11 library: SKIPPED\n");
    return 77;
}

}  // namespace testing
}  // namespace hcqt

#endif
