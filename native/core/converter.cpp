// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Port of oracle\Converter.cs: the converter field logic (tab 1). Stateless.

#include "hc_internal.h"

namespace hc {
namespace converter {

namespace {

// A formatted value can carry one hour digit more than the 7 that can be typed: 9999999:99 is
// 10000000:39 and 9999999,999999 rounds to 10000000,00. That is the only 8 digit hour count there
// is, so text that was formatted here is accepted back.
const wchar_t* const ShownMaxHoursPrefix = L"1000000";

// shown = true: the text is already in a field, so it may be a formatted value that carries the
// 8 digit hour count 10000000.
std::wstring FilterCore(int field, const std::wstring& text, bool shown)
{
    if (text.empty()) return std::wstring();
    std::wstring dec = timemath::DecimalSeparator();
    std::wstring sb;
    sb.reserve(text.size());
    bool sep = false;
    int before = 0, after = 0;
    int maxAfter = field == Left ? timemath::MaxMinuteDigits : timemath::MaxDecimals;
    for (size_t i = 0; i < text.size(); ++i) {
        wchar_t c = text[i];
        if (timemath::IsDigit(c)) {
            if (!sep) {
                if (before < timemath::MaxHourDigits) {
                    sb += c;
                    ++before;
                } else if (shown && before == timemath::MaxHourDigits && c == L'0' && sb == ShownMaxHoursPrefix) {
                    sb += c;
                    ++before;
                }
            } else if (after < maxAfter) {
                sb += c;
                ++after;
            }
        } else if (!sep) {
            if (field == Left && (c == L':' || c == L'.' || c == L'u' || c == L'U' || c == L'h' || c == L'H')) {
                sb += L':';
                sep = true;
            } else if (field == Right && (c == L',' || c == L'.' || dec.find(c) != std::wstring::npos)) {
                sb += dec;
                sep = true;
            }
        }
    }
    return sb;
}

} // namespace

std::wstring Filter(int field, const std::wstring& text)
{
    return FilterCore(field, text, false);
}

std::wstring FilterField(int field, const std::wstring& text)
{
    return FilterCore(field, text, true);
}

bool TryGetMinutes(int field, const std::wstring& text, Dec& minutes)
{
    minutes = Dec();
    if (text.empty()) return false;
    if (field == Left) {
        size_t sep = text.find(L':');
        if (sep == std::wstring::npos) {
            // Digits only: 1-2 digits are whole hours, 3 or more end in minutes.
            if (text.size() <= 2) {
                minutes = Dec::Parse(text) * Dec(60);
            } else {
                std::wstring hh = text.substr(0, text.size() - 2);
                std::wstring mm = text.substr(text.size() - 2);
                // One step at a time: C++ does not say which operand of '+' is evaluated first, and the one that throws
                // first decides which exception the caller sees. The reference (C#) goes left to right: parse the hours,
                // multiply, parse the minutes, add.
                Dec hours = Dec::Parse(hh);
                Dec hourMinutes = hours * Dec(60);
                Dec mins = Dec::Parse(mm);
                minutes = hourMinutes + mins;
            }
            return true;
        }
        std::wstring a = text.substr(0, sep);
        std::wstring b = text.substr(sep + 1);
        if (a.empty() && b.empty()) return false;
        if (!timemath::AllDigits(a) || !timemath::AllDigits(b)) return false;
        Dec h = a.empty() ? Dec() : Dec::Parse(a);
        Dec m = b.empty() ? Dec() : Dec::Parse(b);
        minutes = h * Dec(60) + m;
        return true;
    }
    std::wstring dec = timemath::DecimalSeparator();
    size_t sep = text.find(dec);
    std::wstring a = sep == std::wstring::npos ? text : text.substr(0, sep);
    std::wstring b = sep == std::wstring::npos ? std::wstring() : text.substr(sep + dec.size());
    if (a.empty() && b.empty()) return false;
    if (!timemath::AllDigits(a) || !timemath::AllDigits(b)) return false;
    Dec hours = Dec::Parse((a.empty() ? std::wstring(L"0") : a) + L"." + (b.empty() ? std::wstring(L"0") : b));
    minutes = hours * Dec(60);
    return true;
}

std::wstring Format(int field, const Dec& minutes)
{
    return Format(field, minutes, timemath::DefaultDecimals);
}

std::wstring Format(int field, const Dec& minutes, int places)
{
    return field == Left ? timemath::FormatHm(minutes) : timemath::FormatDecimal(minutes, places);
}

std::wstring Normalize(int field, const std::wstring& text)
{
    return Normalize(field, text, timemath::DefaultDecimals);
}

std::wstring Normalize(int field, const std::wstring& text, int places)
{
    Dec m;
    return TryGetMinutes(field, FilterCore(field, text, true), m) ? Format(field, m, places) : std::wstring();
}

void Compute(int sourceField, const std::wstring& sourceText, std::wstring& otherText, std::wstring& caption)
{
    Compute(sourceField, sourceText, timemath::DefaultDecimals, otherText, caption);
}

void Compute(int sourceField, const std::wstring& sourceText, int places, std::wstring& otherText, std::wstring& caption)
{
    Dec m;
    if (!TryGetMinutes(sourceField, Filter(sourceField, sourceText), m)) {
        otherText.clear();
        caption.clear();
        return;
    }
    otherText = Format(1 - sourceField, m, places);
    caption = timemath::FormatWords(m);
}

void Convert(int field, const std::wstring& text, int places, std::wstring& normalized, std::wstring& otherText, std::wstring& caption)
{
    Dec m;
    if (!TryGetMinutes(field, FilterCore(field, text, true), m)) {
        normalized.clear();
        otherText.clear();
        caption.clear();
        return;
    }
    normalized = Format(field, m, places);
    otherText = Format(1 - field, m, places);
    caption = timemath::FormatWords(m);
}

} // namespace converter
} // namespace hc
