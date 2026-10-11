// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "util.h"

#include <glib.h>

namespace hcgtk {

std::string ToUtf8(const std::wstring& w) {
    if (w.empty()) return std::string();
    glong n = 0;
    gchar* s = g_ucs4_to_utf8(reinterpret_cast<const gunichar*>(w.data()), static_cast<glong>(w.size()), nullptr, &n, nullptr);
    if (!s) return std::string();
    std::string r(s, static_cast<size_t>(n));
    g_free(s);
    return r;
}

std::wstring FromUtf8(const char* s) {
    if (!s || !*s) return std::wstring();
    glong n = 0;
    gunichar* u = g_utf8_to_ucs4(s, -1, nullptr, &n, nullptr);
    if (!u) return std::wstring();
    std::wstring r(reinterpret_cast<const wchar_t*>(u), static_cast<size_t>(n));
    g_free(u);
    return r;
}

}  // namespace hcgtk
