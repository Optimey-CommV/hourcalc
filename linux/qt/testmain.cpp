// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// hourcalc-qt-test: --selftest, --render <dir>, --measure-startup [runs]. Headless only: the platform is
// forced to offscreen before Qt starts, so nothing is ever shown on a screen, and an unknown option ends
// the program with exit code 2 instead of starting the GUI.

#include "testhooks.h"

#include <QApplication>
#include <QByteArray>
#include <QDir>
#include <QtGlobal>

#include <clocale>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unistd.h>

namespace {

// The offscreen platform warns about every window feature it does not have (raise(), grabbing the keyboard ...):
// that is expected here and would bury the real output.
void quietHandler(QtMsgType type, const QMessageLogContext&, const QString& msg) {
    if (msg.startsWith(QLatin1String("This plugin does not support"))) return;
    const char* prefix = type == QtWarningMsg ? "warning: " : (type == QtCriticalMsg || type == QtFatalMsg) ? "error: " : "";
    std::fprintf(stderr, "%s%s\n", prefix, msg.toLocal8Bit().constData());
}

void usage() {
    std::fprintf(stderr,
                 "Usage: hourcalc-qt-test --selftest\n"
                 "       hourcalc-qt-test --xcb-selftest\n"
                 "       hourcalc-qt-test --render <dir>\n"
                 "       hourcalc-qt-test --measure-startup [runs] [--xcb]\n"
                 "Headless: QT_QPA_PLATFORM=offscreen, or with --xcb-selftest / --xcb the xcb platform on a virtual\n"
                 "display (linux/qt/run-headless.sh runs a command under Xvfb); there is no GUI mode.\n");
}

// A fresh configuration folder for this process: a test must never read or write the settings of the user.
// The folder is removed when the process ends. A child of --measure-startup inherits the folder of its parent.
QString g_sandbox;

void removeSandbox() {
    if (!g_sandbox.isEmpty()) QDir(g_sandbox).removeRecursively();
}

void sandboxConfig() {
    if (qEnvironmentVariableIsSet("HCQT_TEST_SANDBOX")) return;
    QByteArray tmpl = QDir::temp().filePath(QStringLiteral("hcqt-test-XXXXXX")).toLocal8Bit();
    if (char* dir = mkdtemp(tmpl.data())) {
        g_sandbox = QString::fromLocal8Bit(dir);
        qputenv("XDG_CONFIG_HOME", QByteArray(dir));
        qputenv("HCQT_TEST_SANDBOX", QByteArray("1"));
        std::atexit(removeSandbox);
    }
}

}  // namespace

namespace hcqt {
namespace testing {

// No window may ever reach the desktop of the owner (WSLg shows Linux windows on his Windows desktop): the xcb platform
// is only used on a display that is not :0, with no Wayland display in sight.
bool headlessDisplay(std::string* why) {
    const char* wl = std::getenv("WAYLAND_DISPLAY");
    if (wl && *wl) {
        *why = "WAYLAND_DISPLAY is set";
        return false;
    }
    const char* d = std::getenv("DISPLAY");
    if (!d || !*d) {
        *why = "DISPLAY is not set";
        return false;
    }
    if (!std::strcmp(d, ":0") || !std::strncmp(d, ":0.", 3) || !std::strncmp(d, "localhost", 9)) {
        *why = std::string("DISPLAY is ") + d + ", a desktop display";
        return false;
    }
    return true;
}

}  // namespace testing
}  // namespace hcqt

int main(int argc, char** argv) {
    hcqt::testing::noteMainEntry();
    std::setlocale(LC_ALL, "");

    enum class Mode { None, SelfTest, XcbSelfTest, Render, Measure, Probe } mode = Mode::None;
    QString renderDir;
    int runs = 7;
    bool xcb = false;
    for (int i = 1; i < argc; ++i) {
        const char* a = argv[i];
        if (!std::strcmp(a, "--selftest") && mode == Mode::None) {
            mode = Mode::SelfTest;
        } else if (!std::strcmp(a, "--xcb-selftest") && mode == Mode::None) {
            mode = Mode::XcbSelfTest;
        } else if (!std::strcmp(a, "--xcb")) {
            xcb = true;
        } else if (!std::strcmp(a, "--render") && mode == Mode::None && i + 1 < argc) {
            mode = Mode::Render;
            renderDir = QString::fromLocal8Bit(argv[++i]);
        } else if (!std::strcmp(a, "--measure-startup") && mode == Mode::None) {
            mode = Mode::Measure;
            if (i + 1 < argc && argv[i + 1][0] != '-') runs = std::atoi(argv[++i]);
            if (runs < 1) runs = 1;
        } else if (!std::strcmp(a, "--startup-probe") && mode == Mode::None) {
            mode = Mode::Probe;
        } else {
            std::fprintf(stderr, "hourcalc-qt-test: unknown or incomplete option '%s'\n", a);
            usage();
            return 2;
        }
    }
    if (mode == Mode::None || (xcb && mode != Mode::Measure && mode != Mode::Probe)) {
        usage();
        return 2;
    }

    // Headless, always: the offscreen platform, or the xcb platform on a virtual display that is not a desktop.
    qInstallMessageHandler(quietHandler);
    const bool useXcb = xcb || mode == Mode::XcbSelfTest;
    if (useXcb) {
        std::string why;
        if (!hcqt::testing::headlessDisplay(&why)) {
            std::fprintf(stderr, "hourcalc-qt-test: will not use the xcb platform: %s (run it with linux/qt/run-headless.sh)\n", why.c_str());
            return 2;
        }
        qputenv("QT_QPA_PLATFORM", QByteArray("xcb"));
    } else {
        qputenv("QT_QPA_PLATFORM", QByteArray("offscreen"));
    }
    qunsetenv("WAYLAND_DISPLAY");
    qunsetenv("QT_QPA_PLATFORMTHEME");
    sandboxConfig();

    if (mode == Mode::Measure) return hcqt::testing::runMeasureStartup(argv[0], runs, xcb);

    QApplication app(argc, argv);
    QApplication::setApplicationName(QStringLiteral("HourCalc"));
    switch (mode) {
    case Mode::SelfTest: return hcqt::testing::runSelfTest();
    case Mode::XcbSelfTest: return hcqt::testing::runXcbSelfTest();
    case Mode::Render: return hcqt::testing::runRender(renderDir);
    case Mode::Probe: return hcqt::testing::runStartupProbe();
    default: break;
    }
    return 2;
}
