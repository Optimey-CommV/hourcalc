// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// GDI+ needs a few Windows headers that WIN32_LEAN_AND_MEAN leaves out, and std::min / std::max
// (NOMINMAX is on). Include this instead of <gdiplus.h>.

#pragma once

#include <windows.h>
#include <ole2.h>
#include <algorithm>
using std::min;
using std::max;
#include <gdiplus.h>
