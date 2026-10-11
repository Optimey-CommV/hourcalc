// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the hooks of the test build (HC_TESTHOOKS): --render <dir>, --selftest and
// --measure-startup. The release exe is compiled without them.

#pragma once

#include <windows.h>

namespace testhooks {

// Looks at the command line. Returns true when it ran a hook (then *rc is the exit code).
bool Handle(HINSTANCE hInst, PWSTR cmdLine, int show, int* rc);

}  // namespace testhooks
