// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - text conversion between the core (std::wstring, UTF-32 on Linux) and GTK (UTF-8).

#pragma once

#include <string>

namespace hcgtk {

static_assert(sizeof(wchar_t) == 4, "the GTK front end expects a 32-bit wchar_t (UTF-32)");

std::string ToUtf8(const std::wstring& w);
std::wstring FromUtf8(const char* s);
inline std::wstring FromUtf8(const std::string& s) { return FromUtf8(s.c_str()); }

}  // namespace hcgtk
