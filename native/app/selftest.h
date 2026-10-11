// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the UI behaviour tests of the test build (--selftest).

#pragma once

#include <windows.h>

namespace selftest {

// Runs every test; returns 0 only when all checks pass.
int Run(HINSTANCE hInst);

}  // namespace selftest
