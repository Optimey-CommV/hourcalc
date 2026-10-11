// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// The modes of the test build hourcalc-qt-test. All of them run headless (QT_QPA_PLATFORM=offscreen);
// the test build never falls back to the normal GUI.

#pragma once

#include <QString>

#include <string>

namespace hcqt {
namespace testing {

// --selftest: the behaviour of SPEC 4-6b on the real widgets, in both decimal separators. 0 = all passed.
int runSelfTest();

// --xcb-selftest: real key presses through the X server of a virtual display (Xvfb, the xcb platform): number row
// shortcuts on a US and an AZERTY layout, Ctrl+Q / Ctrl+W, Keep on top. 0 = all passed, 77 = it could not run here
// (libXtst is not installed).
int runXcbSelfTest();

// --render <dir>: PNGs of the window, the menu and the About dialog, in light and dark, at the default and
// at the minimum size. 0 = done.
int runRender(const QString& dir);

// --measure-startup [runs] [--xcb]: start the probe that many times and report process start to first paint, on the
// offscreen platform or (--xcb, on a virtual display) on the real xcb platform. 0 = done.
int runMeasureStartup(const char* self, int runs, bool xcb);

// Called first thing in main(): the time the program code starts (the start-up probe reports it).
void noteMainEntry();

// --startup-probe (internal): build the window, print the time of its first paint on the system clock and
// the loaded libraries, and exit.
int runStartupProbe();

// The virtual display of a headless run: false (and why) when the environment points at a desktop - a Wayland
// display, or the display :0 that WSLg and a desktop session use.
bool headlessDisplay(std::string* why);

}  // namespace testing
}  // namespace hcqt
