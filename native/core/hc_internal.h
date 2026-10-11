// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Small helpers shared by the core sources. Not part of the contract with the UI (hc_core.h is).

#pragma once

#include "hc_core.h"

namespace hc {
namespace detail {

// char.IsWhiteSpace of the reference runtime (Unicode White_Space, UTF-16 code units).
inline bool IsWhiteSpace(wchar_t c)
{
    return (c >= 0x0009 && c <= 0x000D) || c == 0x0020 || c == 0x0085 || c == 0x00A0 || c == 0x1680 ||
           (c >= 0x2000 && c <= 0x200A) || c == 0x2028 || c == 0x2029 || c == 0x202F || c == 0x205F ||
           c == 0x3000;
}

// string.Trim()
inline std::wstring Trim(const std::wstring& s)
{
    size_t b = 0, e = s.size();
    while (b < e && IsWhiteSpace(s[b])) ++b;
    while (e > b && IsWhiteSpace(s[e - 1])) --e;
    return s.substr(b, e - b);
}

// Invariant text of a non-negative integer, zero padded to at least minWidth digits (ToString("00")).
inline std::wstring UInt(unsigned long long v, int minWidth)
{
    wchar_t buf[24];
    int pos = 24;
    do {
        buf[--pos] = static_cast<wchar_t>(L'0' + v % 10);
        v /= 10;
    } while (v != 0);
    while (24 - pos < minWidth && pos > 0) buf[--pos] = L'0';
    return std::wstring(buf + pos, static_cast<size_t>(24 - pos));
}

} // namespace detail
} // namespace hc
