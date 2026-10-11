// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - the test hooks of hourcalc-gtk-test (built with HC_TESTHOOKS only):
//   --selftest [--system-dark]        drives the real widgets headless and checks SPEC 4-6b
//   --render <dir> [options]          writes PNGs of the window states
//   --measure-startup [--runs N]      process start to first frame, per renderer
// Anything else prints a usage text and exits with 2: the test build never starts the normal GUI.

#pragma once

namespace hctest {

int Main(int argc, char** argv);

}  // namespace hctest
