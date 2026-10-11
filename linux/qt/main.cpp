// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// hourcalc-qt: HourCalc with Qt6 Widgets (the KDE front end). The look comes from the platform theme at
// run time (Breeze on Plasma); the settings and the arithmetic come from the shared core.

#include "appicon.h"
#include "hcqt_config.h"
#include "mainwindow.h"
#include "util.h"

#include "hc_core.h"

#include <QApplication>
#include <QGuiApplication>

#include <clocale>
#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    // The user's locale: the core takes the decimal separator from its numeric part.
    std::setlocale(LC_ALL, "");

    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--version") || !std::strcmp(argv[i], "-V")) {
            std::printf("hourcalc-qt %s\n", HC_VERSION);
            return 0;
        }
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) {
            std::printf("Usage: hourcalc-qt [--version] [Qt options]\n");
            return 0;
        }
    }

    QApplication app(argc, argv);
    QGuiApplication::setApplicationName(QStringLiteral("HourCalc"));
    QGuiApplication::setApplicationVersion(QStringLiteral(HC_VERSION));
    QGuiApplication::setDesktopFileName(QStringLiteral(HC_APP_ID));
    QApplication::setWindowIcon(hcqt::appIcon());

    hcqt::MainWindow window(hcqt::q(hc::settings::DefaultPath()));
    window.show();
    return app.exec();
}
