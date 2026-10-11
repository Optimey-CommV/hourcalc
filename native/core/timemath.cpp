// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Port of oracle\TimeMath.cs: pure time arithmetic and formatting on hc::Dec minutes.

#include "hc_internal.h"
#include "platform.h"

namespace hc {
namespace timemath {

namespace {

const int MinutesGuardPlaces = 9;
const int HoursGuardPlaces = 12;

std::wstring g_separatorOverride;

// ToString("0.00..."): integer digits, the separator, 'places' fraction digits. With trim the
// fraction is "######": no trailing zeros, no separator when nothing is left. 'v' must not be negative.
std::wstring FormatPlain(const Dec& v, int places, bool trim)
{
    std::wstring text = v.RoundAwayFromZero(places).ToInvariant();
    size_t dot = text.find(L'.');
    std::wstring ip = dot == std::wstring::npos ? text : text.substr(0, dot);
    std::wstring fp = dot == std::wstring::npos ? std::wstring() : text.substr(dot + 1);
    if (trim) {
        while (!fp.empty() && fp.back() == L'0') fp.pop_back();
    } else {
        while (static_cast<int>(fp.size()) < places) fp += L'0';
    }
    if (fp.empty()) return ip;
    return ip + DecimalSeparator() + fp;
}

} // namespace

Dec LimitMinutes()
{
    return Dec(10000000) * Dec(60);
}

std::wstring DecimalSeparator()
{
    if (!g_separatorOverride.empty()) return g_separatorOverride;
    return platform::UserDecimalSeparator();
}

void SetDecimalSeparatorOverride(const std::wstring& sep)
{
    g_separatorOverride = sep;
}

bool IsDigit(wchar_t c)
{
    return c >= L'0' && c <= L'9';
}

bool AllDigits(const std::wstring& s)
{
    for (size_t i = 0; i < s.size(); ++i)
        if (!IsDigit(s[i])) return false;
    return true;
}

int ClampDecimals(int places)
{
    return places < MinDisplayDecimals || places > MaxDisplayDecimals ? DefaultDecimals : places;
}

Dec RoundMinutes(const Dec& minutes)
{
    Dec guarded = minutes.RoundAwayFromZero(MinutesGuardPlaces);
    return guarded.RoundAwayFromZero(0);
}

Dec RoundHours(const Dec& hours)
{
    return RoundHours(hours, DefaultDecimals);
}

Dec RoundHours(const Dec& hours, int places)
{
    Dec guarded = hours.RoundAwayFromZero(HoursGuardPlaces);
    return guarded.RoundAwayFromZero(ClampDecimals(places));
}

std::wstring FormatHm(const Dec& minutes)
{
    Dec r = RoundMinutes(minutes);
    long long total = r.Abs().ToInt64();
    std::wstring s = detail::UInt(static_cast<unsigned long long>(total / 60), 2) + L":" +
                     detail::UInt(static_cast<unsigned long long>(total % 60), 2);
    return r.IsNegative() ? L"-" + s : s;
}

std::wstring FormatDecimal(const Dec& minutes)
{
    return FormatDecimal(minutes, DefaultDecimals);
}

std::wstring FormatDecimal(const Dec& minutes, int places)
{
    int p = ClampDecimals(places);
    Dec h = RoundHours(minutes / Dec(60), p);
    std::wstring s = FormatPlain(h.Abs(), p, false);
    return h.IsNegative() ? L"-" + s : s;
}

std::wstring FormatWords(const Dec& minutes)
{
    Dec r = RoundMinutes(minutes);
    long long total = r.Abs().ToInt64();
    std::wstring s = detail::UInt(static_cast<unsigned long long>(total / 60), 1) + L" h " +
                     detail::UInt(static_cast<unsigned long long>(total % 60), 1) + L" min";
    return r.IsNegative() ? L"-" + s : s;
}

std::wstring FormatFactor(const Dec& minutes)
{
    Dec h = (minutes / Dec(60)).RoundAwayFromZero(MaxDecimals);
    if (h.IsZero()) return L"0";
    std::wstring s = FormatPlain(h.Abs(), MaxDecimals, true);
    return h.IsNegative() ? L"-" + s : s;
}

bool TryParseEntry(const std::wstring& entry, Dec& minutes)
{
    minutes = Dec();
    size_t sep = entry.find_first_of(L":,.");
    std::wstring a = sep == std::wstring::npos ? entry : entry.substr(0, sep);
    std::wstring b = sep == std::wstring::npos ? std::wstring() : entry.substr(sep + 1);
    if (!AllDigits(a) || !AllDigits(b)) return false;
    if (static_cast<int>(a.size()) > MaxHourDigits) return false;
    if (sep != std::wstring::npos && entry[sep] == L':') {
        if (static_cast<int>(b.size()) > MaxMinuteDigits) return false;
        Dec h = a.empty() ? Dec() : Dec::Parse(a);
        Dec m = b.empty() ? Dec() : Dec::Parse(b);
        minutes = h * Dec(60) + m;
        return true;
    }
    if (static_cast<int>(b.size()) > MaxDecimals) return false;
    if (a.empty() && b.empty()) {
        minutes = Dec();
        return true;
    }
    Dec hours = Dec::Parse((a.empty() ? std::wstring(L"0") : a) + L"." + (b.empty() ? std::wstring(L"0") : b));
    minutes = hours * Dec(60);
    return true;
}

} // namespace timemath
} // namespace hc
