// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// hourcalc-gtk - the GTK4 + libadwaita front end of HourCalc. The test build (hourcalc-gtk-test, built with
// HC_TESTHOOKS) never starts the normal GUI: it runs the headless self test, the render tool or the start-up
// measurement, and exits with 2 on anything else.

#include <clocale>

#include "app.h"

#ifdef HC_TESTHOOKS
#include "testhooks.h"
#endif

int main(int argc, char** argv) {
    std::setlocale(LC_ALL, "");
#ifdef HC_TESTHOOKS
    return hctest::Main(argc, argv);
#else
    hcgtk::AppOptions opt;
    return hcgtk::RunApp(argc, argv, opt);
#endif
}
