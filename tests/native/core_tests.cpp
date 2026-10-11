// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc core unit tests: a console runner, no test framework. A port of tests\Tests.cs, which
// holds the example tables of SPEC sections 4, 5 and 6 for the C# reference, to the native core.
// Every table runs with ',' and with '.' as decimal separator (the separator override of the core);
// expected strings are written with ',' and converted to the separator when compared.
// Exit code 0 only when every check passes.

#include "../../native/core/hc_core.h"

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#include <knownfolders.h>

// The shell's own answer for the local application data folder, which the registry fallback of settings::DefaultPath must equal.
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "uuid.lib")
#else
#include <cerrno>
#include <chrono>
#include <clocale>
#include <dirent.h>
#include <fcntl.h>
#include <locale.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif

using namespace hc;

namespace {

int g_checks = 0;
int g_failures = 0;
std::wstring g_sep = L",";

std::string Narrow(const std::wstring& s)
{
    std::string o;
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        if (c >= 0x20 && c < 0x7F) {
            o += static_cast<char>(c);
        } else {
            char buf[16];
            std::snprintf(buf, sizeof(buf), "\\u%04X", static_cast<unsigned>(c));
            o += buf;
        }
    }
    return o;
}

void Fail(const std::wstring& what, const std::wstring& message)
{
    ++g_failures;
    std::printf("FAIL [%s] %s: %s\n", Narrow(g_sep).c_str(), Narrow(what).c_str(), Narrow(message).c_str());
}

void Eq(const std::wstring& what, const std::wstring& expected, const std::wstring& actual)
{
    ++g_checks;
    if (expected != actual) Fail(what, L"expected <" + expected + L"> but got <" + actual + L">");
}

void True(const std::wstring& what, bool value)
{
    ++g_checks;
    if (!value) Fail(what, L"expected true");
}

void False(const std::wstring& what, bool value)
{
    ++g_checks;
    if (value) Fail(what, L"expected false");
}

std::wstring Num(long long v)
{
    return Dec(v).ToInvariant();
}

// Expected strings use ',' as decimal separator; the separator under test may be '.'.
std::wstring S(const std::wstring& s)
{
    std::wstring o;
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == L',') o += g_sep;
        else o += s[i];
    }
    return o;
}

Dec M(const char* text)
{
    std::wstring w;
    while (*text) w += static_cast<wchar_t>(*text++);
    return Dec::Parse(w);
}

// m.ToString("0.######", Invariant)
std::wstring DecStr(const Dec& m)
{
    std::wstring t = m.RoundAwayFromZero(6).ToInvariant();
    if (t.find(L'.') != std::wstring::npos) {
        while (!t.empty() && t.back() == L'0') t.pop_back();
        if (!t.empty() && t.back() == L'.') t.pop_back();
    }
    if (t == L"-0") t = L"0";
    return t;
}

// Presses a key string: characters one by one, spaces ignored, <CE> <C> <BK> for the named keys.
void Press(CalcEngine& e, const std::wstring& keys)
{
    for (size_t i = 0; i < keys.size(); ++i) {
        wchar_t c = keys[i];
        if (c == L' ') continue;
        if (c == L'<') {
            size_t end = keys.find(L'>', i);
            std::wstring name = keys.substr(i + 1, end - i - 1);
            e.Press(name == L"BK" ? std::wstring(L"BACK") : name);
            i = end;
            continue;
        }
        e.Press(std::wstring(1, c));
    }
}

CalcEngine Keys(const std::wstring& keys)
{
    CalcEngine e;
    Press(e, keys);
    return e;
}

CalcEngine KeysAt(int places, const std::wstring& keys)
{
    CalcEngine e(places);
    Press(e, keys);
    return e;
}

std::wstring IntStr(int v)
{
    return Num(v);
}

// ------------------------------------------------------------ formatting

void FormatTests()
{
    using namespace timemath;
    Eq(L"FormatHm 510", L"08:30", FormatHm(M("510")));
    Eq(L"FormatHm 0", L"00:00", FormatHm(M("0")));
    Eq(L"FormatHm 7425 (123:45)", L"123:45", FormatHm(M("7425")));
    Eq(L"FormatHm 0.5 rounds up", L"00:01", FormatHm(M("0.5")));
    Eq(L"FormatHm 0.49 rounds down", L"00:00", FormatHm(M("0.49")));
    Eq(L"FormatHm 89.5 carries", L"01:30", FormatHm(M("89.5")));
    Eq(L"FormatHm 59.5 carries to the hour", L"01:00", FormatHm(M("59.5")));
    Eq(L"FormatHm -90", L"-01:30", FormatHm(M("-90")));
    Eq(L"FormatHm -0.5 away from zero", L"-00:01", FormatHm(M("-0.5")));
    Eq(L"FormatHm -0.4 shows no sign", L"00:00", FormatHm(M("-0.4")));
    Eq(L"FormatHm 9999999:59", L"9999999:59", FormatHm(Dec(9999999) * Dec(60) + Dec(59)));

    Eq(L"FormatDecimal 510", S(L"8,50"), FormatDecimal(M("510")));
    Eq(L"FormatDecimal 500", S(L"8,33"), FormatDecimal(M("500")));
    Eq(L"FormatDecimal 0", S(L"0,00"), FormatDecimal(M("0")));
    Eq(L"FormatDecimal 112.5 midpoint away", S(L"1,88"), FormatDecimal(M("112.5")));
    Eq(L"FormatDecimal 0.3 min = 0.005 h rounds up", S(L"0,01"), FormatDecimal(M("0.3")));
    Eq(L"FormatDecimal 7.5 min = 0.125 h", S(L"0,13"), FormatDecimal(M("7.5")));
    Eq(L"FormatDecimal -90", S(L"-1,50"), FormatDecimal(M("-90")));
    Eq(L"FormatDecimal -0.1 shows no sign", S(L"0,00"), FormatDecimal(M("-0.1")));
    Eq(L"FormatDecimal no grouping", S(L"12345,00"), FormatDecimal(Dec(12345) * Dec(60)));
    Eq(L"FormatDecimal 7425", S(L"123,75"), FormatDecimal(M("7425")));

    Eq(L"FormatWords 510", L"8 h 30 min", FormatWords(M("510")));
    Eq(L"FormatWords 45", L"0 h 45 min", FormatWords(M("45")));
    Eq(L"FormatWords 720", L"12 h 0 min", FormatWords(M("720")));
    Eq(L"FormatWords -90", L"-1 h 30 min", FormatWords(M("-90")));
    Eq(L"FormatWords 0", L"0 h 0 min", FormatWords(M("0")));

    Eq(L"FormatFactor 120", L"2", FormatFactor(M("120")));
    Eq(L"FormatFactor 90", S(L"1,5"), FormatFactor(M("90")));
    Eq(L"FormatFactor 0", L"0", FormatFactor(M("0")));
    Eq(L"FormatFactor 80 (1:20)", S(L"1,333333"), FormatFactor(M("80")));

    Dec m;
    True(L"TryParseEntry 7:30", TryParseEntry(L"7:30", m)); Eq(L"7:30 minutes", L"450", DecStr(m));
    True(L"TryParseEntry 7,5", TryParseEntry(L"7,5", m)); Eq(L"7,5 minutes", L"450", DecStr(m));
    True(L"TryParseEntry 7.5", TryParseEntry(L"7.5", m)); Eq(L"7.5 minutes", L"450", DecStr(m));
    True(L"TryParseEntry 8", TryParseEntry(L"8", m)); Eq(L"8 minutes", L"480", DecStr(m));
    True(L"TryParseEntry 0:90", TryParseEntry(L"0:90", m)); Eq(L"0:90 minutes", L"90", DecStr(m));
    True(L"TryParseEntry 7:3", TryParseEntry(L"7:3", m)); Eq(L"7:3 minutes", L"423", DecStr(m));
    True(L"TryParseEntry 7:", TryParseEntry(L"7:", m)); Eq(L"7: minutes", L"420", DecStr(m));
    True(L"TryParseEntry empty", TryParseEntry(L"", m)); Eq(L"empty minutes", L"0", DecStr(m));
    True(L"TryParseEntry 0,1", TryParseEntry(L"0,1", m)); Eq(L"0,1 minutes", L"6", DecStr(m));
    False(L"TryParseEntry letters", TryParseEntry(L"7x", m));
    False(L"TryParseEntry too many minute digits", TryParseEntry(L"7:300", m));
    False(L"TryParseEntry too many hour digits", TryParseEntry(L"12345678", m));
    False(L"TryParseEntry two separators", TryParseEntry(L"1,2,3", m));
}

// ------------------------------------------------------------ converter

struct ConverterRow {
    int field;
    const wchar_t* input;
    const wchar_t* other;
    const wchar_t* caption;
};

const ConverterRow ConverterTable[] = {
    { converter::Left,  L"08:30",  L"8,50",   L"8 h 30 min" },
    { converter::Right, L"8,50",   L"08:30",  L"8 h 30 min" },
    { converter::Right, L"8,5",    L"08:30",  L"8 h 30 min" },
    { converter::Left,  L"08:20",  L"8,33",   L"8 h 20 min" },
    { converter::Right, L"8,33",   L"08:20",  L"8 h 20 min" },
    { converter::Left,  L"830",    L"8,50",   L"8 h 30 min" },
    { converter::Left,  L"8.30",   L"8,50",   L"8 h 30 min" },
    { converter::Left,  L"8u30",   L"8,50",   L"8 h 30 min" },
    { converter::Left,  L"8h30",   L"8,50",   L"8 h 30 min" },
    { converter::Left,  L"0:45",   L"0,75",   L"0 h 45 min" },
    { converter::Right, L"0,1",    L"00:06",  L"0 h 6 min" },
    { converter::Left,  L"123:45", L"123,75", L"123 h 45 min" },
    { converter::Right, L"8,51",   L"08:31",  L"8 h 31 min" },
    { converter::Left,  L"0:90",   L"1,50",   L"1 h 30 min" },
    { converter::Left,  L"8",      L"8,00",   L"8 h 0 min" },
    { converter::Left,  L"12",     L"12,00",  L"12 h 0 min" },
    { converter::Left,  L"1230",   L"12,50",  L"12 h 30 min" },
    { converter::Left,  L"8:3",    L"8,05",   L"8 h 3 min" },
    { converter::Left,  L"8:",     L"8,00",   L"8 h 0 min" },
    { converter::Left,  L":30",    L"0,50",   L"0 h 30 min" },
    { converter::Left,  L"0",      L"0,00",   L"0 h 0 min" },
    { converter::Left,  L"7:59",   L"7,98",   L"7 h 59 min" },
    { converter::Right, L"0",      L"00:00",  L"0 h 0 min" },
    { converter::Right, L",5",     L"00:30",  L"0 h 30 min" },
    { converter::Right, L"8,",     L"08:00",  L"8 h 0 min" },
    { converter::Right, L"8,333",  L"08:20",  L"8 h 20 min" },
    { converter::Right, L"8,999",  L"09:00",  L"9 h 0 min" },
    { converter::Right, L"123,75", L"123:45", L"123 h 45 min" },
    { converter::Right, L"8.5",    L"08:30",  L"8 h 30 min" },
    { converter::Right, L"0,01",   L"00:01",  L"0 h 1 min" },
    { converter::Right, L"0,008",  L"00:00",  L"0 h 0 min" },
};

void ConverterTableTests()
{
    for (size_t i = 0; i < sizeof(ConverterTable) / sizeof(ConverterTable[0]); ++i) {
        const ConverterRow& row = ConverterTable[i];
        std::wstring input = S(row.input);
        std::wstring other, caption;
        converter::Compute(row.field, input, other, caption);
        std::wstring tag = (row.field == converter::Left ? L"left " : L"right ") + input;
        Eq(L"converter " + tag, S(row.other), other);
        Eq(L"converter caption " + tag, row.caption, caption);
    }

    // Empty or digit-less sources clear the other field and the caption.
    const wchar_t* empties[] = { L"", L":", L",", L"abc" };
    for (size_t i = 0; i < 4; ++i) {
        std::wstring empty = empties[i];
        std::wstring o = L"x", c = L"x";
        converter::Compute(converter::Left, empty, o, c);
        Eq(L"left source '" + empty + L"' clears other", L"", o);
        Eq(L"left source '" + empty + L"' clears caption", L"", c);
        o = c = L"x";
        converter::Compute(converter::Right, empty, o, c);
        Eq(L"right source '" + empty + L"' clears other", L"", o);
        Eq(L"right source '" + empty + L"' clears caption", L"", c);
    }

    // Normalization (what Convert writes back into the active field).
    Eq(L"normalize 8:3", L"08:03", converter::Normalize(converter::Left, L"8:3"));
    Eq(L"normalize 830", L"08:30", converter::Normalize(converter::Left, L"830"));
    Eq(L"normalize 0:90", L"01:30", converter::Normalize(converter::Left, L"0:90"));
    Eq(L"normalize 8", L"08:00", converter::Normalize(converter::Left, L"8"));
    Eq(L"normalize 8u30", L"08:30", converter::Normalize(converter::Left, L"8u30"));
    Eq(L"normalize 8,5", S(L"8,50"), converter::Normalize(converter::Right, S(L"8,5")));
    Eq(L"normalize 8.5", S(L"8,50"), converter::Normalize(converter::Right, L"8.5"));
    Eq(L"normalize 8,333", S(L"8,33"), converter::Normalize(converter::Right, S(L"8,333")));
    Eq(L"normalize ,5", S(L"0,50"), converter::Normalize(converter::Right, S(L",5")));
    Eq(L"normalize empty left", L"", converter::Normalize(converter::Left, L""));
    Eq(L"normalize empty right", L"", converter::Normalize(converter::Right, L""));
}

void ConverterFilterTests()
{
    using converter::Filter;
    const int Lf = converter::Left, Rt = converter::Right;
    // Left field: digits plus one separator, shown as ':'.
    Eq(L"filter L 8:30", L"8:30", Filter(Lf, L"8:30"));
    Eq(L"filter L 8.30", L"8:30", Filter(Lf, L"8.30"));
    Eq(L"filter L 8u30", L"8:30", Filter(Lf, L"8u30"));
    Eq(L"filter L 8U30", L"8:30", Filter(Lf, L"8U30"));
    Eq(L"filter L 8h30", L"8:30", Filter(Lf, L"8h30"));
    Eq(L"filter L comma ignored", L"830", Filter(Lf, L"8,30"));
    Eq(L"filter L at most 2 minute digits", L"8:30", Filter(Lf, L"8:3045"));
    Eq(L"filter L second separator ignored", L"1:23", Filter(Lf, L"1:2:3"));
    Eq(L"filter L letters dropped", L"12", Filter(Lf, L"a1b2c"));
    Eq(L"filter L minus dropped", L"15", Filter(Lf, L"-15"));
    Eq(L"filter L 7 hour digits", L"1234567", Filter(Lf, L"123456789"));
    Eq(L"filter L 7 hour digits with separator", L"1234567:30", Filter(Lf, L"123456789:30"));
    Eq(L"filter L empty", L"", Filter(Lf, L""));
    Eq(L"filter L spaces dropped", L"8:30", Filter(Lf, L" 8 : 30 "));

    // Right field: digits plus one decimal separator, shown as the separator under test.
    Eq(L"filter R 8,5", S(L"8,5"), Filter(Rt, L"8,5"));
    Eq(L"filter R 8.5", S(L"8,5"), Filter(Rt, L"8.5"));
    Eq(L"filter R colon ignored", L"830", Filter(Rt, L"8:30"));
    Eq(L"filter R u ignored", L"8", Filter(Rt, L"8u"));
    Eq(L"filter R second separator ignored", S(L"1,23"), Filter(Rt, L"1,2,3"));
    Eq(L"filter R at most 6 decimals", S(L"0,123456"), Filter(Rt, L"0,1234567"));
    Eq(L"filter R 7 integer digits", L"1234567", Filter(Rt, L"123456789"));
    Eq(L"filter R leading separator kept", S(L",5"), Filter(Rt, L",5"));
    Eq(L"filter R minus dropped", S(L"1,5"), Filter(Rt, L"-1,5"));
    Eq(L"filter R idempotent", S(L"8,50"), Filter(Rt, S(L"8,50")));
    Eq(L"filter L idempotent", L"08:30", Filter(Lf, L"08:30"));
}

void ConverterRoundTripTests()
{
    using namespace timemath;
    // Left -> right -> left is exact to the minute for 100 hours.
    int bad = 0;
    for (int minutes = 0; minutes <= 6000; ++minutes) {
        std::wstring left = FormatHm(Dec(minutes));
        std::wstring right, cap, back, cap2;
        converter::Compute(converter::Left, left, right, cap);
        converter::Compute(converter::Right, right, back, cap2);
        if (back != left) ++bad;
    }
    Eq(L"left -> right -> left exact for 0..6000 minutes (bad count)", L"0", IntStr(bad));

    // 2 decimals carry at most 0.3 minute (0.005 hour) of quantization error.
    int tooBig = 0;
    for (int minutes = 0; minutes <= 6000; ++minutes) {
        std::wstring right = FormatDecimal(Dec(minutes));
        Dec back;
        converter::TryGetMinutes(converter::Right, right, back);
        if ((back - Dec(minutes)).Abs() > M("0.3")) ++tooBig;
    }
    Eq(L"decimal text is within 0.3 min of the minutes (violations)", L"0", IntStr(tooBig));

    // Typing every hundredth from 0,00 to 100,00 gives a minute count within 0.5 of the exact value.
    int off = 0;
    for (int cents = 0; cents <= 10000; ++cents) {
        std::wstring right = Num(cents / 100) + DecimalSeparator() + (cents % 100 < 10 ? L"0" : L"") + Num(cents % 100);
        Dec exact, shown;
        converter::TryGetMinutes(converter::Right, right, exact);
        std::wstring other, cap;
        converter::Compute(converter::Right, right, other, cap);
        converter::TryGetMinutes(converter::Left, other, shown);
        if ((shown - exact).Abs() > M("0.5")) ++off;
    }
    Eq(L"right -> left is the nearest minute (violations)", L"0", IntStr(off));
}

// ------------------------------------------------------------ calculator

void Result(const std::wstring& keys, const wchar_t* left, const wchar_t* right)
{
    CalcEngine e = Keys(keys);
    Eq(L"calc '" + keys + L"' left", S(left), e.MainText());
    Eq(L"calc '" + keys + L"' right", S(right), e.DecimalText());
}

void CalcRequiredTests()
{
    // SPEC section 5: the required examples.
    Result(L"7:30 + 7:20 =", L"14:50", L"14,83");
    Result(L"8 + 0:30 =", L"08:30", L"8,50");
    Result(L"7,5 + 7:20 =", L"14:50", L"14,83");
    Result(L"7:30 * 5 =", L"37:30", L"37,50");
    Result(L"7:30 / 4 =", L"01:53", L"1,88");
    Result(L"7:00 - 8:30 =", L"-01:30", L"-1,50");
    Result(L"7:30 + 1:00 = =", L"09:30", L"9,50");
    Result(L"0:90 =", L"01:30", L"1,50");
    Result(L"1:00 + 2:00 * 2 =", L"06:00", L"6,00");
    Result(L"7:30 / 0 =", L"Cannot divide by zero", L"");

    // More arithmetic.
    Result(L"7:30 + =", L"15:00", L"15,00");
    Result(L"7:30 / 4 = * 4 =", L"07:30", L"7,50");
    Result(L"7:30 * 1,5 =", L"11:15", L"11,25");
    Result(L"7:30 * 1:30 =", L"11:15", L"11,25");
    Result(L"7:30 / 1,5 =", L"05:00", L"5,00");
    Result(L"7:30 / 1:30 =", L"05:00", L"5,00");
    Result(L"7:30 * 2 = =", L"30:00", L"30,00");
    Result(L"7:30 - 1:00 = =", L"05:30", L"5,50");
    Result(L"7:00 - 8:30 = + 2:00 =", L"00:30", L"0,50");
    Result(L"5 + 3 = + 2 =", L"10:00", L"10,00");
    Result(L"7:30 + 1 = 5 =", L"05:00", L"5,00");
    Result(L"1:20 + 1:20 + 1:20 =", L"04:00", L"4,00");
    Result(L"0,1 + 0,1 =", L"00:12", L"0,20");
    Result(L"8:30 - 0,5 =", L"08:00", L"8,00");
    Result(L"12 * 0 =", L"00:00", L"0,00");
    Result(L"7u30 + 7h20 =", L"14:50", L"14,83");
    Result(L"7:30 + 7:20 + 8:00 =", L"22:50", L"22,83");
    Result(L"1:00 + 1:00 * 3 + 1:00 =", L"07:00", L"7,00");
    Result(L"99:59 + 0:01 =", L"100:00", L"100,00");
    Result(L"0:30 / 3 * 3 =", L"00:30", L"0,50");
    Result(L"1:00 / 3 * 3 =", L"01:00", L"1,00");
    Result(L"2:00 / 7 * 7 =", L"02:00", L"2,00");
    Result(L"9999999 + 0:59 =", L"9999999:59", L"9999999,98");
}

void CalcExpressionTests()
{
    Eq(L"expr after =", L"07:30 + 07:20 =", Keys(L"7:30 + 7:20 =").Expression());
    Eq(L"expr chain", L"07:30 + 07:20 + 08:00 +", Keys(L"7:30 + 7:20 + 8:00 +").Expression());
    Eq(L"expr after first operator", L"07:30 +", Keys(L"7:30 +").Expression());
    Eq(L"expr factor", L"07:30 * 2 =", Keys(L"7:30 * 2 =").Expression());
    Eq(L"expr factor decimal", S(L"07:30 / 1,5 ="), Keys(L"7:30 / 1,5 =").Expression());
    Eq(L"expr factor from h:mm", S(L"07:30 / 1,5 ="), Keys(L"7:30 / 1:30 =").Expression());
    Eq(L"expr entry as decimal hours shows HH:mm", L"07:30 + 07:20 =", Keys(L"7,5 + 7:20 =").Expression());
    Eq(L"expr repeat", L"08:30 + 01:00 =", Keys(L"7:30 + 1:00 = =").Expression());
    Eq(L"expr repeat twice", L"09:30 + 01:00 =", Keys(L"7:30 + 1:00 = = =").Expression());
    Eq(L"expr repeat factor", L"15:00 * 2 =", Keys(L"7:30 * 2 = =").Expression());
    Eq(L"expr operator replaced", L"07:30 -", Keys(L"7:30 + -").Expression());
    Eq(L"expr operator replaced 3x", L"07:30 /", Keys(L"7:30 + - /").Expression());
    Eq(L"expr continue after =", L"14:50 +", Keys(L"7:30 + 7:20 = +").Expression());
    Eq(L"expr + with accumulator", L"07:30 + 07:30 =", Keys(L"7:30 + =").Expression());
    Eq(L"expr 0:90 =", L"01:30 =", Keys(L"0:90 =").Expression());
    Eq(L"expr before anything", L"", CalcEngine().Expression());
    Eq(L"expr of divide by zero", L"07:30 / 0 =", Keys(L"7:30 / 0 =").Expression());
    Eq(L"expr operator first", L"00:00 +", Keys(L"+").Expression());
    Eq(L"expr new entry after = clears", L"", Keys(L"7:30 + 1 = 5").Expression());
    Eq(L"expr after C", L"", Keys(L"7:30 + 1 <C>").Expression());
    Eq(L"expr negative accumulator", L"-01:30 +", Keys(L"7:00 - 8:30 = +").Expression());

    // Running result shows after an operator.
    CalcEngine e = Keys(L"7:30 + 7:20 +");
    Eq(L"running result left", L"14:50", e.MainText());
    Eq(L"running result right", S(L"14,83"), e.DecimalText());
    Eq(L"running result caption", L"14 h 50 min", e.Caption());
    e = Keys(L"7:30 + 7:20 + 8:00 +");
    Eq(L"running result 3 operands", L"22:50", e.MainText());
}

void CalcEntryTests()
{
    // Idle.
    CalcEngine e;
    Eq(L"idle left", L"00:00", e.MainText());
    Eq(L"idle right", S(L"0,00"), e.DecimalText());
    Eq(L"idle caption", L"", e.Caption());
    False(L"idle has no entry", e.HasEntry());
    False(L"idle no error", e.IsError());

    // Mid-entry: raw text as typed, live decimal value, caption in words.
    e = Keys(L"7:30 + 7:2");
    Eq(L"mid-entry left", L"7:2", e.MainText());
    Eq(L"mid-entry right", S(L"7,03"), e.DecimalText());
    Eq(L"mid-entry caption", L"7 h 2 min", e.Caption());
    Eq(L"mid-entry expression", L"07:30 +", e.Expression());
    True(L"mid-entry has entry", e.HasEntry());

    e = Keys(L"7,5");
    Eq(L"decimal entry left", S(L"7,5"), e.MainText());
    Eq(L"decimal entry right", S(L"7,50"), e.DecimalText());
    Eq(L"decimal entry caption", L"7 h 30 min", e.Caption());

    e = Keys(L"12");
    Eq(L"whole hours left", L"12", e.MainText());
    Eq(L"whole hours right", S(L"12,00"), e.DecimalText());
    Eq(L"whole hours caption", L"12 h 0 min", e.Caption());

    // Limits.
    Eq(L"max 7 hour digits", L"1234567", Keys(L"12345678").MainText());
    Eq(L"max 2 minute digits", L"7:30", Keys(L"7:300").MainText());
    Eq(L"max 6 decimals", S(L"1,123456"), Keys(L"1,1234567").MainText());
    Eq(L"one separator per entry", L"7:30", Keys(L"7:3:0").MainText());
    Eq(L"no ':' after ','", S(L"7,5"), Keys(L"7,5:").MainText());
    Eq(L"no ',' after ':'", L"7:3", Keys(L"7:3,").MainText());
    Eq(L"leading ','", S(L"0,5"), Keys(L",5").MainText());
    Eq(L"leading ':'", L"0:5", Keys(L":5").MainText());
    Eq(L"leading ':' value", S(L"0,08"), Keys(L":5").DecimalText());
    Eq(L"typed leading zero kept", L"07:30", Keys(L"07:30").MainText());

    // Aliases.
    Eq(L"alias u", L"7:30", Keys(L"7u30").MainText());
    Eq(L"alias h", L"7:30", Keys(L"7h30").MainText());
    Eq(L"alias .", S(L"7,5"), Keys(L"7.5").MainText());

    // Backspace edits the entry only.
    Eq(L"backspace once", L"7:", Keys(L"7:3<BK>").MainText());
    Eq(L"backspace twice", L"7", Keys(L"7:3<BK><BK>").MainText());
    Eq(L"backspace to empty shows 0", L"0", Keys(L"7<BK>").MainText());
    Eq(L"backspace to empty value", S(L"0,00"), Keys(L"7<BK>").DecimalText());
    Eq(L"backspace on empty entry", L"0", Keys(L"7<BK><BK>").MainText());
    Eq(L"backspace has no effect on a result", L"08:30", Keys(L"7:30 + 1 = <BK>").MainText());
    Eq(L"backspace before any entry", L"00:00", Keys(L"<BK>").MainText());
    Eq(L"typing after backspace", L"7:5", Keys(L"7:3<BK>5").MainText());
    Eq(L"backspace then operator uses the shortened entry", L"07:00", Keys(L"7:3<BK><BK>+").MainText());
}

void CalcStateTests()
{
    // CE clears the current entry.
    CalcEngine e = Keys(L"7:30 + 5 <CE>");
    Eq(L"CE shows zero", L"0", e.MainText());
    Eq(L"CE keeps expression", L"07:30 +", e.Expression());
    Press(e, L"=");
    Eq(L"CE then = adds zero", L"07:30", e.MainText());
    e = Keys(L"7:30 + 5 <CE> 2 =");
    Eq(L"CE then new entry", L"09:30", e.MainText());

    // C clears everything.
    e = Keys(L"7:30 + 5 <C>");
    Eq(L"C left", L"00:00", e.MainText());
    Eq(L"C right", S(L"0,00"), e.DecimalText());
    Eq(L"C expression", L"", e.Expression());
    Eq(L"C caption", L"", e.Caption());
    Press(e, L"+ 1 =");
    Eq(L"after C starts fresh", L"01:00", e.MainText());

    // CE after = starts over; CE before anything does nothing visible.
    e = Keys(L"7:30 + 1 = <CE>");
    Eq(L"CE after = resets", L"00:00", e.MainText());
    Eq(L"CE before anything", L"00:00", Keys(L"<CE>").MainText());
    Eq(L"CE before anything caption", L"", Keys(L"<CE>").Caption());

    // After =, digits start a new calculation, operators continue.
    e = Keys(L"7:30 + 1 = 2");
    Eq(L"digit after = starts new", L"2", e.MainText());
    e = Keys(L"7:30 + 1 = + 2 =");
    Eq(L"operator after = continues", L"10:30", e.MainText());
    e = Keys(L"7:30 + 1 = :");
    Eq(L"separator after = starts new", L"0:", e.MainText());

    // Separator inside a pending state.
    e = Keys(L"7:30 + ,5 =");
    Eq(L"0,5 as second operand", L"08:00", e.MainText());

    // "=" without anything to do.
    e = Keys(L"=");
    Eq(L"= at idle left", L"00:00", e.MainText());
    Eq(L"= at idle expression", L"", e.Expression());
    e = Keys(L"0:90 = =");
    Eq(L"= twice after a bare value", L"01:30", e.MainText());

    // Negative results and captions.
    e = Keys(L"7:00 - 8:30 =");
    Eq(L"negative caption", L"-1 h 30 min", e.Caption());
    Eq(L"negative decimal", S(L"-1,50"), e.DecimalText());
    e = Keys(L"0:30 - 0:30 =");
    Eq(L"zero result", L"00:00", e.MainText());
    Eq(L"zero decimal", S(L"0,00"), e.DecimalText());
    Eq(L"zero caption", L"0 h 0 min", e.Caption());
    e = Keys(L"0:01 - 0:02 =");
    Eq(L"-1 minute", L"-00:01", e.MainText());

    // Full precision accumulator: only the display rounds.
    e = Keys(L"7:30 / 4 =");
    Eq(L"precision caption (rounded)", L"1 h 53 min", e.Caption());
    e = Keys(L"7:30 / 4 = * 4 =");
    Eq(L"precision restored", L"07:30", e.MainText());
    e = Keys(L"7:30 / 4 = + 0:07 =");
    Eq(L"precision accumulates", L"02:00", e.MainText());
    Eq(L"precision accumulates right", S(L"1,99"), e.DecimalText());

    // Copy texts.
    e = Keys(L"7:30 + 7:20 =");
    Eq(L"copy left", L"14:50", e.CopyMainText());
    Eq(L"copy right", S(L"14,83"), e.CopyDecimalText());
    e = Keys(L"7:30 / 0 =");
    Eq(L"copy left on error", L"", e.CopyMainText());
    Eq(L"copy right on error", L"", e.CopyDecimalText());

    // Key aliases and robustness.
    e = CalcEngine();
    e.Press(L"garbage");
    e.Press(L"");
    Eq(L"unknown keys ignored", L"00:00", e.MainText());
    e = Keys(L"7:30 + 7:20 =");
    e.Press(L"c");
    Eq(L"lowercase c", L"00:00", e.MainText());
}

void CalcErrorTests()
{
    CalcEngine e = Keys(L"7:30 / 0 =");
    True(L"div0 is error", e.IsError());
    Eq(L"div0 text", L"Cannot divide by zero", e.MainText());
    Eq(L"div0 text constant", CalcEngine::DivideByZeroText, e.MainText());
    Eq(L"div0 right empty", L"", e.DecimalText());
    Eq(L"div0 caption empty", L"", e.Caption());
    const wchar_t* disabled[] = { L"+", L"-", L"*", L"/", L"=", L":", L",", L"BACK" };
    for (size_t i = 0; i < 8; ++i) False(std::wstring(L"div0 disables ") + disabled[i], e.IsEnabled(disabled[i]));
    const wchar_t* kept[] = { L"0", L"5", L"9", L"C", L"CE" };
    for (size_t i = 0; i < 5; ++i) True(std::wstring(L"div0 keeps ") + kept[i], e.IsEnabled(kept[i]));
    // Disabled keys do nothing.
    Press(e, L"+ = : ,");
    Eq(L"keys ignored in error", L"Cannot divide by zero", e.MainText());
    // A digit resets and starts an entry.
    e.Press(L"5");
    False(L"digit clears the error", e.IsError());
    Eq(L"digit after error starts entry", L"5", e.MainText());
    Eq(L"expression cleared after error digit", L"", e.Expression());
    // C and CE reset.
    e = Keys(L"7:30 / 0 = <C>");
    False(L"C clears error", e.IsError());
    Eq(L"C after error", L"00:00", e.MainText());
    e = Keys(L"7:30 / 0 = <CE>");
    False(L"CE clears error", e.IsError());
    Eq(L"CE after error", L"00:00", e.MainText());
    Eq(L"CE after error expression", L"", e.Expression());
    // Division by zero through a chain and a zero factor spelled as h:mm.
    e = Keys(L"7:30 / 0:00 +");
    Eq(L"div0 mid-chain", L"Cannot divide by zero", e.MainText());
    e = Keys(L"7:30 / 0,0 =");
    Eq(L"div0 with 0,0", L"Cannot divide by zero", e.MainText());
    e = Keys(L"0 / 0 =");
    Eq(L"0/0", L"Cannot divide by zero", e.MainText());
    // Dividing by a tiny non-zero factor is fine (1 000 000 hours is below the limit).
    e = Keys(L"1:00 / 0,000001 =");
    Eq(L"tiny factor", L"1000000:00", e.MainText());
    Eq(L"tiny factor right", S(L"1000000,00"), e.DecimalText());
    e = Keys(L"10:00 / 0,000001 =");
    Eq(L"tiny factor overflows", L"Overflow", e.MainText());

    // Overflow.
    e = Keys(L"9999999 * 9999999 =");
    Eq(L"overflow multiply", L"Overflow", e.MainText());
    Eq(L"overflow text constant", CalcEngine::OverflowText, e.MainText());
    Eq(L"overflow right empty", L"", e.DecimalText());
    False(L"overflow disables +", e.IsEnabled(L"+"));
    e = Keys(L"9999999 + 1 =");
    Eq(L"overflow add", L"Overflow", e.MainText());
    e = Keys(L"9999999 + 0:59 =");
    False(L"below the limit is fine", e.IsError());
    e = Keys(L"0 - 9999999 - 1 =");
    Eq(L"overflow negative", L"Overflow", e.MainText());
    e = Keys(L"9999999 + 1 = 5");
    Eq(L"digit clears overflow", L"5", e.MainText());
    Eq(L"overflow shown mid-chain", L"Overflow", Keys(L"9999999 + 1 *").MainText());
}

void CalcPasteTests()
{
    CalcEngine e;
    True(L"paste 08:30", e.Paste(L"08:30"));
    Eq(L"pasted left", L"08:30", e.MainText());
    Eq(L"pasted right", S(L"8,50"), e.DecimalText());
    True(L"paste with spaces", e.Paste(L"  1:15 "));
    Eq(L"pasted with spaces", L"1:15", e.MainText());
    True(L"paste 1.5", e.Paste(L"1.5"));
    Eq(L"pasted 1.5", S(L"1,5"), e.MainText());
    Eq(L"pasted 1.5 value", S(L"1,50"), e.DecimalText());
    True(L"paste 1,5", e.Paste(L"1,5"));
    Eq(L"pasted 1,5", S(L"1,5"), e.MainText());
    True(L"paste 8u30", e.Paste(L"8u30"));
    Eq(L"pasted 8u30", L"8:30", e.MainText());
    True(L"paste 8", e.Paste(L"8"));
    Eq(L"pasted 8", L"8", e.MainText());

    std::wstring before = e.MainText();
    False(L"paste garbage", e.Paste(L"abc"));
    False(L"paste empty", e.Paste(L""));
    False(L"paste negative", e.Paste(L"-1:30"));
    False(L"paste too many digits", e.Paste(L"12345678"));
    False(L"paste 1,2,3", e.Paste(L"1,2,3"));
    False(L"paste 7:300", e.Paste(L"7:300"));
    Eq(L"failed paste changes nothing", before, e.MainText());

    // Pasting after = starts a new calculation; typing continues the pasted entry.
    e = Keys(L"7:30 + 1 =");
    True(L"paste after =", e.Paste(L"2:00"));
    Eq(L"paste after = is a new entry", L"2:00", e.MainText());
    Eq(L"paste after = clears expression", L"", e.Expression());
    Press(e, L"+ 1 =");
    Eq(L"pasted value is usable", L"03:00", e.MainText());

    e = Keys(L"7:30 +");
    True(L"paste as second operand", e.Paste(L"0:30"));
    Press(e, L"=");
    Eq(L"pasted second operand", L"08:00", e.MainText());

    e = Keys(L"7:30 / 0 =");
    True(L"paste resets error", e.Paste(L"1:00"));
    False(L"paste cleared error", e.IsError());
    Eq(L"paste after error", L"1:00", e.MainText());
}

void CalcPrecisionTests()
{
    // The decimal type keeps these exact: no 0.1 + 0.2 style errors.
    Result(L"0,1 + 0,2 =", L"00:18", L"0,30");
    Result(L"0,7 + 0,1 =", L"00:48", L"0,80");
    Result(L"8,33 + 0,01 =", L"08:20", L"8,34");
    // 1/3 of an hour times 3 comes back to the minute.
    Result(L"1:00 / 3 = * 3 =", L"01:00", L"1,00");
    // Midpoint away from zero on both displays.
    Result(L"0:01 / 2 =", L"00:01", L"0,01");
    Result(L"0:03 / 2 =", L"00:02", L"0,03");
    Result(L"0:00 - 0:01 / 2 =", L"-00:01", L"-0,01");
    // 100 additions of 0:01 are exactly 1:40.
    std::wstring sb = L"0:01";
    for (int i = 0; i < 99; ++i) sb += L"+0:01";
    sb += L"=";
    Result(sb, L"01:40", L"1,67");
    // Long chains keep working.
    std::wstring sb2 = L"1";
    for (int i = 0; i < 200; ++i) sb2 += L"+1";
    sb2 += L"=";
    Result(sb2, L"201:00", L"201,00");
}

void CalcLimitTests()
{
    // A typed or pasted entry of 10 000 000 hours or more is an overflow, like the same value reached
    // through an operation (minutes up to 99 can be typed: 9999999:60 is 10000000:00).
    const wchar_t* overflowing[] = { L"9999999:60 =", L"9999999:99 =", L"9999999:60 +", L"9999999:99 *", L"9999999:60 -", L"9999999:60 /" };
    for (size_t i = 0; i < 6; ++i) {
        std::wstring k = overflowing[i];
        CalcEngine e = Keys(k);
        Eq(L"overflow for '" + k + L"'", L"Overflow", e.MainText());
        True(L"... is an error", e.IsError());
        Eq(L"... right is empty", L"", e.DecimalText());
    }
    CalcEngine p;
    True(L"paste 9999999:99", p.Paste(L"9999999:99"));
    Press(p, L"=");
    Eq(L"paste 9999999:99 then = overflows", L"Overflow", p.MainText());
    p = CalcEngine();
    p.Paste(L"9999999:99");
    Press(p, L"+");
    Eq(L"paste 9999999:99 then + overflows", L"Overflow", p.MainText());
    CalcEngine after = Keys(L"9999999:60 =");
    after.Press(L"5");
    Eq(L"a digit leaves the overflow", L"5", after.MainText());
    Eq(L"just below the limit is fine", L"9999999:59", Keys(L"9999999:59 =").MainText());
    Eq(L"... and 9999999:59 + 0 too", L"9999999:59", Keys(L"9999999:59 + 0 =").MainText());
    Eq(L"... and 9999999,999999 = is below the limit", L"10000000:00", Keys(L"9999999,999999 =").MainText());
    False(L"... no error", Keys(L"9999999,999999 =").IsError());

    // A pasted entry that starts with a separator shows a leading zero, like a typed one.
    const wchar_t* texts[] = { L",5", L".5", L":30", L":", L"," };
    for (size_t i = 0; i < 5; ++i) {
        std::wstring t = texts[i];
        CalcEngine typed = Keys(t == L".5" ? std::wstring(L",5") : t);
        CalcEngine pasted;
        True(L"paste <" + t + L">", pasted.Paste(t));
        Eq(L"pasted <" + t + L"> shows like the typed entry", typed.MainText(), pasted.MainText());
        Eq(L"pasted <" + t + L"> has the same value", typed.DecimalText(), pasted.DecimalText());
    }
    CalcEngine pc;
    pc.Paste(L",5");
    Eq(L"pasted ,5 value", S(L"0,50"), pc.DecimalText());
    CalcEngine pd;
    pd.Paste(L":30");
    Press(pd, L"=");
    Eq(L"pasted :30 =", L"00:30", pd.MainText());
}

// ------------------------------------------------------------ Convert (the button)

void ConvertButtonTests()
{
    std::wstring normal, other, caption;
    const int Lf = converter::Left, Rt = converter::Right;

    // Convert tidies the active text but never moves the other field.
    converter::Convert(Rt, S(L"8,058"), 2, normal, other, caption);
    Eq(L"Convert 8,058: text", S(L"8,06"), normal);
    Eq(L"Convert 8,058: left stays 08:03", L"08:03", other);
    Eq(L"Convert 8,058: caption", L"8 h 3 min", caption);
    converter::Convert(Rt, S(L"9,994"), 2, normal, other, caption);
    Eq(L"Convert 9,994: left stays 10:00", L"10:00", other);
    Eq(L"Convert 9,994: text", S(L"9,99"), normal);
    converter::Convert(Rt, S(L"0,0083"), 2, normal, other, caption);
    Eq(L"Convert 0,0083: left stays 00:00", L"00:00", other);
    converter::Convert(Rt, S(L"8,058"), 4, normal, other, caption);
    Eq(L"Convert 8,058 at 4 places: text", S(L"8,0580"), normal);
    converter::Convert(Lf, L"8:3", 2, normal, other, caption);
    Eq(L"Convert 8:3: text", L"08:03", normal);
    Eq(L"Convert 8:3: right", S(L"8,05"), other);
    converter::Convert(Lf, L":", 2, normal, other, caption);
    Eq(L"Convert on a lone separator: text", L"", normal);
    Eq(L"Convert on a lone separator: other", L"", other);
    Eq(L"Convert on a lone separator: caption", L"", caption);

    // The 8 digit hour count that formatting can produce (10000000) survives Convert and Normalize.
    Eq(L"Normalize keeps 10000000,65", S(L"10000000,65"), converter::Normalize(Rt, S(L"10000000,65")));
    Eq(L"Normalize keeps 10000000:39", L"10000000:39", converter::Normalize(Lf, L"10000000:39"));
    Eq(L"Normalize 9999999:60", L"10000000:00", converter::Normalize(Lf, L"9999999:60"));
    Eq(L"Normalize 9999999,999", S(L"10000000,00"), converter::Normalize(Rt, S(L"9999999,999")));
    Eq(L"Normalize 9999999,999999 twice", S(L"10000000,00"), converter::Normalize(Rt, converter::Normalize(Rt, S(L"9999999,999999"))));
    converter::Convert(Rt, S(L"10000000,65"), 2, normal, other, caption);
    Eq(L"Convert 10000000,65: text", S(L"10000000,65"), normal);
    Eq(L"Convert 10000000,65: left", L"10000000:39", other);
    converter::Convert(Lf, L"10000000:39", 2, normal, other, caption);
    Eq(L"Convert 10000000:39: right", S(L"10000000,65"), other);
    Eq(L"typed text still stops at 7 hour digits", L"1000000:39", converter::Filter(Lf, L"10000000:39"));
    Eq(L"field text may carry 10000000", L"10000000:39", converter::FilterField(Lf, L"10000000:39"));
    Eq(L"... but no other 8 digit count", L"2000000:00", converter::FilterField(Lf, L"20000000:00"));
    Eq(L"... nor 10000001", L"1000000:01", converter::FilterField(Lf, L"10000001:01"));

    // Pseudo random texts: Convert agrees with the live conversion and normalizing is idempotent.
    unsigned long long state = 11;
    auto next = [&](unsigned n) {
        state = state * 6364136223846793005ull + 1442695040888963407ull;
        return static_cast<unsigned>((state >> 33) % n);
    };
    const wchar_t alpha[] = L"0123456789:,.uh";
    int bad = 0;
    for (int n = 0; n < 20000; ++n) {
        int f = static_cast<int>(next(2));
        std::wstring raw;
        unsigned len = next(12);
        for (unsigned i = 0; i < len; ++i) raw += alpha[next(15)];
        std::wstring text = converter::Filter(f, raw);
        std::wstring o, c, nrm, o2, c2;
        converter::Compute(f, text, o, c);
        converter::Convert(f, text, 2, nrm, o2, c2);
        if (o != o2 || c != c2 || nrm != converter::Normalize(f, text) || converter::Normalize(f, nrm) != nrm) ++bad;
    }
    Eq(L"Convert matches the live conversion on random text (bad count)", L"0", IntStr(bad));
}

// ------------------------------------------------------------ decimal places (SPEC 6)

void DecimalPlacesTests()
{
    using namespace timemath;
    // The SPEC examples: 08:20 is 8,33 / 8,333 / 8,3333 / 8,33333 / 8,333333.
    const wchar_t* expect[] = { L"8,33", L"8,333", L"8,3333", L"8,33333", L"8,333333" };
    for (int p = 2; p <= 6; ++p) {
        std::wstring ps = IntStr(p);
        Eq(L"FormatDecimal 08:20 at " + ps, S(expect[p - 2]), FormatDecimal(M("500"), p));
        std::wstring o, c;
        converter::Compute(converter::Left, L"08:20", p, o, c);
        Eq(L"converter 08:20 at " + ps, S(expect[p - 2]), o);
        Eq(L"converter caption does not depend on the places", L"8 h 20 min", c);
        Eq(L"Format at " + ps, S(expect[p - 2]), converter::Format(converter::Right, M("500"), p));
        Eq(L"calculator 08:20 at " + ps, S(expect[p - 2]), KeysAt(p, L"8:20 =").DecimalText());
    }
    Eq(L"the default is 2 places", S(L"8,33"), FormatDecimal(M("500")));
    Eq(L"the default calculator has 2 places", L"2", IntStr(CalcEngine().DecimalPlaces()));
    Eq(L"h:mm stays whole minutes at 6 places", L"08:20", converter::Format(converter::Left, M("500"), 6));
    Eq(L"Normalize 8,5 at 4 places", S(L"8,5000"), converter::Normalize(converter::Right, S(L"8,5"), 4));
    Eq(L"Normalize 08:30 at 4 places (left)", L"08:30", converter::Normalize(converter::Left, L"830", 4));

    // 7:30 / 4 = 1,875 hours.
    Eq(L"7:30 / 4 at 2", S(L"1,88"), KeysAt(2, L"7:30 / 4 =").DecimalText());
    Eq(L"7:30 / 4 at 3", S(L"1,875"), KeysAt(3, L"7:30 / 4 =").DecimalText());
    Eq(L"7:30 / 4 at 4", S(L"1,8750"), KeysAt(4, L"7:30 / 4 =").DecimalText());
    Eq(L"7:30 / 4 at 6", S(L"1,875000"), KeysAt(6, L"7:30 / 4 =").DecimalText());
    Eq(L"7:00 - 8:30 at 3 places", S(L"-1,500"), KeysAt(3, L"7:00 - 8:30 =").DecimalText());

    // 0:01 is 0,016666... hours.
    Eq(L"0:01 at 2", S(L"0,02"), FormatDecimal(M("1"), 2));
    Eq(L"0:01 at 3", S(L"0,017"), FormatDecimal(M("1"), 3));
    Eq(L"0:01 at 4", S(L"0,0167"), FormatDecimal(M("1"), 4));
    Eq(L"0:01 at 5", S(L"0,01667"), FormatDecimal(M("1"), 5));
    Eq(L"0:01 at 6", S(L"0,016667"), FormatDecimal(M("1"), 6));
    Eq(L"calculator 0:01 at 4", S(L"0,0167"), KeysAt(4, L"0:1 =").DecimalText());
    Eq(L"-0:01 at 4 keeps its sign", S(L"-0,0167"), FormatDecimal(M("-1"), 4));
    Eq(L"a value that rounds to zero has no sign (at 4 places)", S(L"0,0000"), FormatDecimal(M("-0.001"), 4));

    // Changing the places re-formats from the exact value, not from the rounded text.
    CalcEngine e = KeysAt(2, L"7:30 / 4 =");
    Eq(L"start", S(L"1,88"), e.DecimalText());
    e.SetDecimalPlaces(4);
    Eq(L"re-formatted to 4", S(L"1,8750"), e.DecimalText());
    Eq(L"... the left display is unchanged", L"01:53", e.MainText());
    Eq(L"... the caption is unchanged", L"1 h 53 min", e.Caption());
    e.SetDecimalPlaces(2);
    Eq(L"back to 2 from the exact value", S(L"1,88"), e.DecimalText());
    e = KeysAt(2, L"1:00 / 3 =");
    e.SetDecimalPlaces(6);
    Eq(L"1:00 / 3 at 6 places", S(L"0,333333"), e.DecimalText());
    Press(e, L"* 3 =");
    Eq(L"the accumulator kept its full precision", L"01:00", e.MainText());
    Eq(L"... 1,000000 hours", S(L"1,000000"), e.DecimalText());
    e = KeysAt(2, L"7:3");
    e.SetDecimalPlaces(4);
    Eq(L"a live entry follows the places", S(L"7,0500"), e.DecimalText());

    // Anything outside 2..6 means the default.
    e = CalcEngine();
    const int badPlaces[] = { -1, 0, 1, 7, 100 };
    for (size_t i = 0; i < 5; ++i) {
        e.SetDecimalPlaces(4);
        e.SetDecimalPlaces(badPlaces[i]);
        Eq(L"places " + IntStr(badPlaces[i]) + L" is the default", L"2", IntStr(e.DecimalPlaces()));
    }
    Eq(L"constructor clamps too", L"2", IntStr(CalcEngine(9).DecimalPlaces()));
    Eq(L"constructor keeps 5", L"5", IntStr(CalcEngine(5).DecimalPlaces()));
    Eq(L"FormatDecimal with an invalid places value uses the default", S(L"8,33"), FormatDecimal(M("500"), 9));
    Eq(L"ClampDecimals 6", L"6", IntStr(ClampDecimals(6)));
    Eq(L"ClampDecimals 3", L"3", IntStr(ClampDecimals(3)));

    // Typed input keeps up to 6 decimals whatever the setting; more places make the round trip finer.
    std::wstring back, cap;
    converter::Compute(converter::Right, S(L"8,333333"), 6, back, cap);
    Eq(L"8,333333 gives 08:20", L"08:20", back);
    int bad2 = 0;
    for (int m = 0; m <= 6000; ++m) {
        for (int p = 2; p <= 6; ++p) {
            std::wstring right = FormatDecimal(Dec(m), p);
            std::wstring left, c2;
            converter::Compute(converter::Right, right, p, left, c2);
            if (left != FormatHm(Dec(m))) ++bad2;
        }
    }
    Eq(L"left -> right -> left is exact at every number of places (bad count)", L"0", IntStr(bad2));

    // Midpoint away from zero at any number of places, also for values that are an exact half but
    // not exactly representable after a division.
    Eq(L"0:05 / 6 * 3 shows 00:03", L"00:03", Keys(L"0:05 / 6 * 3 =").MainText());
    Eq(L"0,125 h at 2 places", S(L"0,13"), FormatDecimal(M("7.5"), 2));
    Eq(L"0,125 h at 3 places", S(L"0,125"), FormatDecimal(M("7.5"), 3));
    Eq(L"an exact half minute from divisions rounds up", L"00:03", Keys(L"0:05 / 6 * 3 =").MainText());
    Eq(L"... and its decimal at 2 places", S(L"0,04"), Keys(L"0:05 / 6 * 3 =").DecimalText());
    Eq(L"1:00 / 3 / 2 * 6 / 3 = still 01:00", L"01:00", Keys(L"1:00 / 3 / 2 * 6 / 3 * 3 =").MainText());
}

// ------------------------------------------------------------ Dec (System.Decimal behaviour)

void DecTests()
{
    // Parse and format keep the scale like System.Decimal.
    Eq(L"7.50 keeps its scale", L"7.50", M("7.50").ToInvariant());
    Eq(L"scale of a product adds up", L"15.00", (M("7.50") * Dec(2)).ToInvariant());
    Eq(L"0.0 keeps its scale", L"0.0", M("0.0").ToInvariant());
    Eq(L"negative zero prints without sign", L"0.00", M("-0.00").ToInvariant());
    Eq(L"leading zeros dropped", L"7", M("007").ToInvariant());
    Eq(L"exact quotient keeps the preferred scale", L"2.50", (M("10.00") / Dec(4)).ToInvariant());
    Eq(L"exact quotient without a preferred scale", L"2.5", (Dec(10) / Dec(4)).ToInvariant());
    Eq(L"exact quotient of a long scale", L"31637279604006.7120000000000", (M("1898236776240402.7200000000000") / Dec(60)).ToInvariant());
    Eq(L"zero quotient keeps the scale", L"0.00", (M("0.00") / Dec(60)).ToInvariant());
    Eq(L"inexact quotient is stripped", L"-0.000000000178232335562782025", (M("-0.0000000106939401337669215003") / Dec(60)).ToInvariant());
    Eq(L"underflowing quotient is a plain 0", L"0", (M("0.000000000000000002937568355") / M("-35861209128720684751070042.083")).ToInvariant());
    Eq(L"1/3", L"0.3333333333333333333333333333", (Dec(1) / Dec(3)).ToInvariant());
    Eq(L"2/3 rounds the last digit", L"0.6666666666666666666666666667", (Dec(2) / Dec(3)).ToInvariant());
    Eq(L"largest value", L"79228162514264337593543950335", M("79228162514264337593543950335").ToInvariant());
    Eq(L"round away: 2.5", L"3", M("2.5").RoundAwayFromZero(0).ToInvariant());
    Eq(L"round away: -2.5", L"-3", M("-2.5").RoundAwayFromZero(0).ToInvariant());
    Eq(L"round away: 1.005 at 2", L"1.01", M("1.005").RoundAwayFromZero(2).ToInvariant());
    Eq(L"round away keeps a smaller scale", L"1.5", M("1.5").RoundAwayFromZero(3).ToInvariant());
    Eq(L"round to even: 2.5", L"2", M("2.5").RoundToEven(0).ToInvariant());
    Eq(L"round to even: 3.5", L"4", M("3.5").RoundToEven(0).ToInvariant());
    Eq(L"round to even: 2.51", L"3", M("2.51").RoundToEven(0).ToInvariant());
    Eq(L"truncate", L"-2", M("-2.9").Truncate().ToInvariant());
    Eq(L"ToInt64 truncates", L"-2", Num(M("-2.9").ToInt64()));
    True(L"compare 7.50 == 7.5", M("7.50") == M("7.5"));
    True(L"compare -0 == 0", M("-0.0") == M("0"));
    True(L"IsNegative of -0 is false", !M("-0.0").IsNegative());
    True(L"IsNegative of -0.1", M("-0.1").IsNegative());
    // The quirks of System.Decimal (.NET Framework 4.8), each of them proved against the C# reference.
    Eq(L"zero product of small operands keeps the scale", L"0.00", (M("0.00") * Dec(5)).ToInvariant());
    Eq(L"... 0.000 * 0.0000", L"0.0000000", (M("0.000") * M("0.0000")).ToInvariant());
    Eq(L"... a scale above 28 is cut to 28", L"0.0000000000000000000000000000", (M("0.0000000000000000000000000000") * M("1.5")).ToInvariant());
    Eq(L"... 0.0000 * 4294967295 (32 bits) keeps it", L"0.0000", (M("0.0000") * M("4294967295")).ToInvariant());
    Eq(L"zero product of a larger operand is a plain 0", L"0", (M("0.0000") * M("4294967296")).ToInvariant());
    Eq(L"... also with a long mantissa", L"0", (M("0.0000") * M("-79228162514.264337593543950289")).ToInvariant());
    Eq(L"a product that loses 19 digits keeps scale 28", L"0.0000000000000000000000000002",
       (M("0.0000000004294967295") * M("0.0000000000000000004294967295")).ToInvariant());
    Eq(L"... 20 digits or more: a plain 0", L"0", (M("0.0000000000000000000040593") * M("0.000000000000002889779429")).ToInvariant());
    Eq(L"... a tiny non-zero product of a larger operand keeps scale 28", L"0.0000000000000000000000000000",
       (M("0.0000000000000000000000000001") * M("0.0000000018446744073709551617")).ToInvariant());
    Eq(L"a product is rounded once (the estimate of the digits to drop is one short)", L"967323.1264875716987255233671",
       (M("-34423140001471055981601") * M("-0.000000000000000028100955533")).ToInvariant());
    Eq(L"... 99.311534...", L"99.31153402614967261217886901", (M("-0.1601282393198156604517556740") * M("-620.2")).ToInvariant());
    Eq(L"1 / 8", L"0.125", (Dec(1) / Dec(8)).ToInvariant());
    Eq(L"1.00 / 8", L"0.125", (M("1.00") / Dec(8)).ToInvariant());
    Eq(L"1.0000 / 8 keeps the preferred scale", L"0.1250", (M("1.0000") / Dec(8)).ToInvariant());
    Eq(L"an exact quotient that fills all 96 bits", L"792281625142643375935439503.35", (M("47536897508558602556126370201") / Dec(60)).ToInvariant());
    Eq(L"... the largest value by 7 is exact", L"11318308930609191084791992905", (M("79228162514264337593543950335") / Dec(7)).ToInvariant());
    Eq(L"a quotient whose rounding would carry out of 96 bits is rounded one digit earlier", L"-7922816251426433759354395.034",
       (M("7922816251426433759354395.0328") / M("-0.9999999999999999999999999999")).ToInvariant());
    // A divisor of more than 64 bits is rounded the way the runtime does it (RoundsUpWide in dec.cpp): twice the remainder is
    // compared with the divisor, limb by limb from the top, and the carry out of a low or middle limb that holds exactly
    // 0x80000000 is lost, so a quotient a hair above one half can round down, and an exact half rounds down for an odd quotient
    // as well. The expected values are those of the real decimal of .NET Framework 4.8 (made by a program that divides
    // near-half cases, and by hand for the exact halves): the nine cases of a review, exact halves with the divisors
    // 2^65 and 2^64 + 2, then cases built from limb values around 0x80000000 (some round as an exact comparison would,
    // most of them do not).
    {
        struct WideCase { const char* a; const char* b; const char* expected; };
        static const WideCase wide[] = {
        { "0.0000000009223372036854775809", "-18446744073709551617", "0" },
        { "-0.0000000006456360425798343066", "-1844674407370955161.7", "0.0000000000000000000000000003" },
        { "0.011754105897830252544", "-235082117956605050865678639", "0" },
        { "-0.0000227161658752582358699205", "-56740766517442827201", "0.0000000000000000000000004003" },
        { "-0.0000000055232540298862942612", "110465080597725885220", "0" },
        { "0.0000000011320545169467603935", "22641090338935207868", "0" },
        { "0.0000000037273305624208650572", "-74546611248417301140", "0" },
        { "-0.0000000046116860184273879041", "18446744073709551616", "-0.0000000000000000000000000002" },
        { "0.0148826532629141475756605441", "-18446744073709551616", "-0.0000000000000000000008067902" },
        { "0.0000000018446744073709551616", "36893488147419103232", "0" },
        { "0.0000000055340232221128654848", "36893488147419103232", "0.0000000000000000000000000002" },
        { "0.0000000092233720368547758080", "36893488147419103232", "0.0000000000000000000000000002" },
        { "0.0000000129127208515966861312", "36893488147419103232", "0.0000000000000000000000000004" },
        { "0.0000000166020696663385964544", "36893488147419103232", "0.0000000000000000000000000004" },
        { "0.0000000202914184810805067776", "36893488147419103232", "0.0000000000000000000000000006" },
        { "0.0000000239807672958224171008", "36893488147419103232", "0.0000000000000000000000000006" },
        { "0.0000000276701161105643274240", "36893488147419103232", "0.0000000000000000000000000008" },
        { "0.0000000313594649253062377472", "36893488147419103232", "0.0000000000000000000000000008" },
        { "0.0000000350488137400481480704", "36893488147419103232", "0.000000000000000000000000001" },
        { "0.0000000009223372036854775809", "18446744073709551618", "0" },
        { "0.0000000027670116110564327427", "18446744073709551618", "0.0000000000000000000000000001" },
        { "0.0000000046116860184273879045", "18446744073709551618", "0.0000000000000000000000000002" },
        { "0.0000000064563604257983430663", "18446744073709551618", "0.0000000000000000000000000003" },
        { "0.0000000083010348331692982281", "18446744073709551618", "0.0000000000000000000000000004" },
        { "0.0000000101457092405402533899", "18446744073709551618", "0.0000000000000000000000000005" },
        { "0.0000000119903836479112085517", "18446744073709551618", "0.0000000000000000000000000006" },
        { "0.0000000138350580552821637135", "18446744073709551618", "0.0000000000000000000000000007" },
        { "0.3785793097255463407361642532", "2523862064836975604798246948", "0.0000000000000000000000000001" },
        { "0.0001059113870603387916102113", "706075913735591944048449", "0.0000000000000000000000000001" },
        { "3.3915606100200491811513533996", "22610404066800327873576978668", "0.0000000000000000000000000001" },
        { "0.0332462686084736410030097538", "221641790723157606682466351", "0.0000000000000000000000000001" },
        { "4.3291947301117902230379120339", "28861298200745268152356719557", "0.0000000000000000000000000001" },
        { "0.0000006322623878171518304065", "4215082585447678869313", "0.0000000000000000000000000001" },
        { "0.0000000116168280461438615561", "232336560922877231072", "0" },
        { "3.9614081247908796762064683008", "79228162495817593524129366013", "0" },
        { "0.0266047428713457044922671232", "177364952475638029946364296", "0.0000000000000000000000000001" },
        { "0.0000086216641006422668086000", "57477760670948445388354", "0.0000000000000000000000000001" },
        { "3.5687571904876727735080173301", "71375143809753455463409155859", "0" },
        { "0.0000003647290168962629762948", "2431526779308419841872", "0.0000000000000000000000000001" },
        { "1.9807040656236200508950315010", "39614081312472401017900630018", "0" },
        { "0.0000074715024054055096821224", "49810016036036731213815", "0.0000000000000000000000000001" },
        { "0.1344434137451774219370102784", "2688868274903548438514213036", "0" },
        { "0.0000351775389882236232794113", "703550779764472465525337", "0" },
        { "0.0000020896792070312765164096", "41793584140625530315951", "0" },
        { "5.9421121968708601539735785109", "39614081312472401026490502807", "0.0000000000000000000000000001" },
        { "0.0000000243921115660481986559", "162614077106987991038", "0.0000000000000000000000000001" },
        { "0.0000002257245974603331797247", "4514491949206663593969", "0" },
        { "0.4575907369057896226958957450", "3050604912705264151138023172", "0.0000000000000000000000000001" },
        { "0.0000024210664481141263697920", "48421328962282527394439", "0" },
        { "6.8249393210830624468020631340", "45499595473887082974096558490", "0.0000000000000000000000000001" },
        { "3.9614081247908796764212166654", "79228162495817593528424298617", "0" },
        { "5.9421121913368369318607192056", "39614081275578912879071461370", "0.0000000000000000000000000001" },
        { "6.8249393210830624467268831642", "45499595473887082974096558490", "0.0000000000000000000000000001" },
        { "0.2233281645547867302026934729", "4466563291095734604029299153", "0" },
        { "0.0008354225049180120021685196", "16708450098360240040768474", "0" },
        { "0.0001622827803406487978397783", "1081885202270991985594455", "0.0000000000000000000000000001" },
        { "0.2233281645547867302100355891", "4466563291095734604029299153", "0" },
        { "1.9807040619342712357851165121", "39614081238685424715702330239", "0.0000000000000000000000000001" },
        { "0.0079824761758946397150773247", "159649523517892794331196155", "0" },
        { "3.9614081238685424729504874495", "79228162477370849459009748988", "0.0000000000000000000000000001" },
        { "0.0105203592980279686400078182", "210407185960559372786311069", "0.0000000000000000000000000001" },
        { "0.0112011052452884860043941435", "224022104905769720103166722", "0" },
        { "0.1548735770401206976987186563", "3097471540802413954792797214", "0" },
        { "0.0000001882363807796465499905", "3764727615592931000412", "0" },
        { "0.0008435436207208878676181589", "16870872414417757353153633", "0" },
        { "1.4186672320291484974769159342", "28373344640582969950644757060", "0" },
        { "0.0000000010884352013248757762", "21768704026497515523", "0.0000000000000000000000000001" },
        { "2.9710560942849126621397004113", "19807040628566084414264669409", "0.0000000000000000000000000001" },
        { "0.0116708837239029107979906978", "233417674478058216006770749", "0" },
        { "0.0000577461199235358637686783", "1154922398470717275261495", "0.0000000000000000000000000001" },
        { "0.9903520314918088098562703360", "19807040629836176197125406723", "0" },
        { "1.8020468208361137551735521280", "36040936416722275102504679648", "0.0000000000000000000000000001" },
        { "0.0000000143817479840753254389", "287634959681506508780", "0" },
        { "0.7180891076017550666011608646", "4787260717345033777475039092", "0.0000000000000000000000000001" },
        { "0.0000532353770664276512473087", "1064707541328553024856653", "0.0000000000000000000000000001" },
        { "2.6809423719715538358408438580", "53618847439431076716461865219", "0.0000000000000000000000000001" },
        { "0.0000073251672488173973267051", "48834448325449315514885", "0.0000000000000000000000000001" },
        };
        for (const WideCase& c : wide) {
            std::wstring a, b, e;
            for (const char* p = c.a; *p; ++p) a += static_cast<wchar_t>(*p);
            for (const char* p = c.b; *p; ++p) b += static_cast<wchar_t>(*p);
            for (const char* p = c.expected; *p; ++p) e += static_cast<wchar_t>(*p);
            Eq(L"wide divisor, as the runtime rounds it: " + a + L" / " + b, e, (M(c.a) / M(c.b)).ToInvariant());
        }
    }
    bool overflow = false;
    try { Dec x = M("79228162514264337593543950335") + Dec(1); (void)x; } catch (const DecOverflow&) { overflow = true; }
    True(L"overflow throws DecOverflow", overflow);
    bool divzero = false;
    try { Dec x = Dec(1) / Dec(0); (void)x; } catch (const DecDivideByZero&) { divzero = true; }
    True(L"division by zero throws DecDivideByZero", divzero);
    bool bad = false;
    try { Dec x = Dec::Parse(L"1,5"); (void)x; } catch (const DecFormat&) { bad = true; }
    True(L"Parse of a bad text throws DecFormat", bad);
    bool big = false;
    try { Dec x = M("9223372036854775808").ToInt64(); (void)x; } catch (const DecOverflow&) { big = true; }
    True(L"ToInt64 out of range throws DecOverflow", big);

    // Out-of-contract text (the app filters to 7 hour digits first). When both the hours and the minutes of a digits-only
    // text are bad, the hours decide, as in the C# reference (left to right): the hours do not fit (DecOverflow), the
    // minutes are no number (DecFormat). C++ leaves the order of the two operands open, so this is checked on its own.
    {
        const wchar_t* texts[] = { L"2927466775137960846959889829776,0", L"86447062967593559152568581565895543776,0", L"7428651965771034349832396768438,3" };
        for (const wchar_t* t : texts) {
            bool hoursFirst = false, other = false;
            Dec m;
            try { converter::TryGetMinutes(converter::Left, t, m); } catch (const DecOverflow&) { hoursFirst = true; } catch (...) { other = true; }
            True(std::wstring(L"hours that do not fit throw DecOverflow before the minutes are looked at: ") + t, hoursFirst && !other);
        }
        bool formatFirst = false;
        Dec m;
        try { converter::TryGetMinutes(converter::Left, L"12,3", m); } catch (const DecFormat&) { formatFirst = true; } catch (...) {}
        True(L"hours that fit and minutes that are no number throw DecFormat", formatFirst);
    }
}

// ------------------------------------------------------------ HourCalc.ini

// ---- files and folders, once per platform (the tests use ASCII paths only)

std::wstring Wide(const std::string& s)
{
    return std::wstring(s.begin(), s.end());
}

#ifdef _WIN32

const wchar_t* const kSep = L"\\";

bool WriteFileText(const std::wstring& path, const std::string& bytes)
{
    HANDLE h = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0;
    bool ok = WriteFile(h, bytes.data(), static_cast<DWORD>(bytes.size()), &n, nullptr) != 0;
    CloseHandle(h);
    return ok;
}

std::string ReadFileText(const std::wstring& path)
{
    std::string r;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return r;
    char buf[512];
    DWORD n = 0;
    while (ReadFile(h, buf, sizeof(buf), &n, nullptr) && n > 0) r.append(buf, n);
    CloseHandle(h);
    return r;
}

// The number of *.tmp files in a folder: the temporary file of a save must never be left behind.
int CountTmpFiles(const std::wstring& dir)
{
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"\\*.tmp").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    int n = 0;
    do { ++n; } while (FindNextFileW(h, &fd));
    FindClose(h);
    return n;
}

bool PathExists(const std::wstring& path) { return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES; }
void RemoveFile(const std::wstring& path) { DeleteFileW(path.c_str()); }
void RemoveFolder(const std::wstring& path) { RemoveDirectoryW(path.c_str()); }
bool MakeFolder(const std::wstring& path) { return CreateDirectoryW(path.c_str(), nullptr) != 0; }

// A folder name that no other run uses (the folder itself is made by the test).
std::wstring NewTestFolderPath(bool& ok)
{
    wchar_t tmp[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH, tmp);
    ok = n > 0 && n < MAX_PATH;
    return std::wstring(tmp) + L"HourCalcCoreTests-" + std::to_wstring(static_cast<long long>(GetCurrentProcessId())) + L"-" +
           std::to_wstring(static_cast<long long>(GetTickCount64()));
}

#else

const wchar_t* const kSep = L"/";

std::string Narrow8(const std::wstring& w) { return std::string(w.begin(), w.end()); }

bool WriteFileText(const std::wstring& path, const std::string& bytes)
{
    int fd = open(Narrow8(path).c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
    if (fd < 0) return false;
    bool ok = write(fd, bytes.data(), bytes.size()) == static_cast<ssize_t>(bytes.size());
    close(fd);
    return ok;
}

std::string ReadFileText(const std::wstring& path)
{
    std::string r;
    int fd = open(Narrow8(path).c_str(), O_RDONLY | O_CLOEXEC);
    if (fd < 0) return r;
    char buf[512];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) r.append(buf, static_cast<size_t>(n));
    close(fd);
    return r;
}

int CountTmpFiles(const std::wstring& dir)
{
    int n = 0;
    if (DIR* d = opendir(Narrow8(dir).c_str())) {
        while (struct dirent* e = readdir(d)) {
            std::string name = e->d_name;
            if (name.size() >= 4 && name.compare(name.size() - 4, 4, ".tmp") == 0) ++n;
        }
        closedir(d);
    }
    return n;
}

bool PathExists(const std::wstring& path)
{
    struct stat st;
    return lstat(Narrow8(path).c_str(), &st) == 0;
}

void RemoveFile(const std::wstring& path) { unlink(Narrow8(path).c_str()); }
void RemoveFolder(const std::wstring& path) { rmdir(Narrow8(path).c_str()); }
bool MakeFolder(const std::wstring& path) { return mkdir(Narrow8(path).c_str(), 0777) == 0; }

std::wstring NewTestFolderPath(bool& ok)
{
    ok = true;
    return L"/tmp/HourCalcCoreTests-" + std::to_wstring(static_cast<long long>(getpid())) + L"-" +
           std::to_wstring(static_cast<long long>(std::chrono::steady_clock::now().time_since_epoch().count() % 1000000007LL));
}

#endif

// dir + separator + names joined by the separator.
std::wstring P(const std::wstring& dir, std::initializer_list<const wchar_t*> names)
{
    std::wstring r = dir;
    for (const wchar_t* n : names) { r += kSep; r += n; }
    return r;
}

std::wstring ThemeStr(settings::ThemeMode t)
{
    return Num(static_cast<long long>(t));
}

// "<places> <theme>" of a text: the two parsers on the same ini text.
std::wstring BothOf(const std::wstring& text)
{
    return IntStr(settings::ParseDecimalPlaces(text)) + L" " + ThemeStr(settings::ParseTheme(text));
}

// "<places> <theme>" as one read of the file gives them.
std::wstring LoadedBoth(const std::wstring& path)
{
    settings::Values v = settings::Load(path);
    return IntStr(v.decimalPlaces) + L" " + ThemeStr(v.theme);
}

// A reader that loads the file again and again while it is being saved. The file only ever holds (4, Dark) or (5, Light), so a
// read that gives anything else saw a file that was empty or half written (the defaults are 2 and Auto).
struct ReaderState {
    std::wstring path;
    std::atomic<int> stop;
    std::atomic<long> reads;
    std::atomic<long> torn;
};

// A pause between two reads: a start-up of a second HourCalc, not a busy loop (a reader that never lets go keeps any rename out).
void ReaderPause()
{
#ifdef _WIN32
    LARGE_INTEGER f, a, b;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&a);
    do { QueryPerformanceCounter(&b); } while ((b.QuadPart - a.QuadPart) * 1e6 / static_cast<double>(f.QuadPart) < 1000.0);
#else
    std::this_thread::sleep_for(std::chrono::microseconds(200));
#endif
}

void ReaderLoop(ReaderState* s)
{
    while (!s->stop) {
        settings::Values v = settings::Load(s->path);
        ++s->reads;
        bool a = v.decimalPlaces == 4 && v.theme == settings::ThemeMode::Dark;
        bool b = v.decimalPlaces == 5 && v.theme == settings::ThemeMode::Light;
        if (!a && !b) ++s->torn;
        ReaderPause();
    }
}

#ifdef _WIN32

// A file that is held open so that nobody else may open it: a program with the settings file open (an editor, a virus scanner).
void LockedFileTests(const std::wstring& f, const std::wstring& dir)
{
    using namespace settings;
    const std::wstring crlf5dark = L"DecimalPlaces=5\r\nTheme=dark\r\n";
    WriteFileText(f, "DecimalPlaces=5\r\nTheme=dark\r\n");
    HANDLE lock = CreateFileW(f.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    True(L"the file is locked (nobody else may open it)", lock != INVALID_HANDLE_VALUE);
    Eq(L"locked: a load gives the defaults", L"2 0", LoadedBoth(f));
    False(L"locked: SaveTheme writes nothing", SaveTheme(f, ThemeMode::Light, 2));
    False(L"locked: SavePlaces writes nothing", SavePlaces(f, 3, ThemeMode::Auto));
    False(L"locked: a plain Save cannot replace it either", Save(f, 3, ThemeMode::Auto));
    CloseHandle(lock);
    Eq(L"the file is as it was", crlf5dark, Wide(ReadFileText(f)));
    Eq(L"the failed saves left no temporary file", L"0", Num(CountTmpFiles(dir)));
    True(L"unlocked again: SaveTheme", SaveTheme(f, ThemeMode::Light, 2));
    Eq(L"... keeps the 5 places that the failed read of the start-up never saw", L"DecimalPlaces=5\r\nTheme=light\r\n", Wide(ReadFileText(f)));
}

// The attributes of the file: hidden and system are replaced (the file is saved), read-only is the user's "do not change this".
void FileMarkTests(const std::wstring& f, const std::wstring& dir)
{
    using namespace settings;
    const std::wstring crlf5dark = L"DecimalPlaces=5\r\nTheme=dark\r\n";
    const DWORD attrs[] = { FILE_ATTRIBUTE_HIDDEN, FILE_ATTRIBUTE_SYSTEM, FILE_ATTRIBUTE_HIDDEN | FILE_ATTRIBUTE_SYSTEM };
    const wchar_t* const attrNames[] = { L"hidden", L"system", L"hidden and system" };
    for (int i = 0; i < 3; i++) {
        WriteFileText(f, "DecimalPlaces=5\r\nTheme=dark\r\n");
        SetFileAttributesW(f.c_str(), attrs[i]);
        True(std::wstring(L"a ") + attrNames[i] + L" settings file is saved", Save(f, 3, ThemeMode::Light));
        Eq(std::wstring(L"... with the new contents (") + attrNames[i] + L")", L"DecimalPlaces=3\r\nTheme=light\r\n", Wide(ReadFileText(f)));
        SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_NORMAL);
    }
    WriteFileText(f, "DecimalPlaces=5\r\nTheme=dark\r\n");
    SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_READONLY);
    False(L"a read-only settings file is not saved (it is the user's mark)", Save(f, 3, ThemeMode::Light));
    False(L"... nor by SaveTheme", SaveTheme(f, ThemeMode::Light, 3));
    False(L"... nor by SavePlaces", SavePlaces(f, 3, ThemeMode::Light));
    Eq(L"... and it is as it was", crlf5dark, Wide(ReadFileText(f)));
    SetFileAttributesW(f.c_str(), FILE_ATTRIBUTE_NORMAL);
    Eq(L"no temporary file is left behind", L"0", Num(CountTmpFiles(dir)));
}

#else

bool Chmod(const std::wstring& path, unsigned mode) { return chmod(Narrow8(path).c_str(), static_cast<mode_t>(mode)) == 0; }

unsigned ModeOf(const std::wstring& path)
{
    struct stat st;
    return stat(Narrow8(path).c_str(), &st) == 0 ? static_cast<unsigned>(st.st_mode & 07777) : 0xFFFFu;
}

// A file that is there but cannot be read (no permission at all): nothing is written, and the file stays as it is. (Not as root:
// root reads everything.)
void LockedFileTests(const std::wstring& f, const std::wstring& dir)
{
    using namespace settings;
    const std::wstring crlf5dark = L"DecimalPlaces=5\r\nTheme=dark\r\n";
    WriteFileText(f, "DecimalPlaces=5\r\nTheme=dark\r\n");
    if (geteuid() == 0) {
        std::printf("note: running as root, the unreadable-file checks are skipped\n");
        return;
    }
    True(L"the file is made unreadable", Chmod(f, 0));
    Eq(L"unreadable: a load gives the defaults", L"2 0", LoadedBoth(f));
    False(L"unreadable: SaveTheme writes nothing", SaveTheme(f, ThemeMode::Light, 2));
    False(L"unreadable: SavePlaces writes nothing", SavePlaces(f, 3, ThemeMode::Auto));
    False(L"unreadable: a plain Save cannot replace it either (no write permission)", Save(f, 3, ThemeMode::Auto));
    True(L"the file is made readable again", Chmod(f, 0644));
    Eq(L"the file is as it was", crlf5dark, Wide(ReadFileText(f)));
    Eq(L"the failed saves left no temporary file", L"0", Num(CountTmpFiles(dir)));
    True(L"readable again: SaveTheme", SaveTheme(f, ThemeMode::Light, 2));
    Eq(L"... keeps the 5 places that the failed read of the start-up never saw", L"DecimalPlaces=5\r\nTheme=light\r\n", Wide(ReadFileText(f)));
}

// The permission bits of the file: a file without any write bit is the user's "do not change this" (the Windows read-only
// attribute); every other mode is saved, and the mode of the file stays.
void FileMarkTests(const std::wstring& f, const std::wstring& dir)
{
    using namespace settings;
    const std::wstring crlf5dark = L"DecimalPlaces=5\r\nTheme=dark\r\n";
    const unsigned modes[] = { 0600, 0640, 0664, 0666, 0200 };
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); i++) {
        std::wstring name = L"mode " + Num(static_cast<long long>(modes[i]));
        WriteFileText(f, "DecimalPlaces=5\r\nTheme=dark\r\n");
        Chmod(f, modes[i]);
        True(L"a file with " + name + L" is saved", Save(f, 3, ThemeMode::Light));
        Chmod(f, 0600);
        Eq(L"... with the new contents (" + name + L")", L"DecimalPlaces=3\r\nTheme=light\r\n", Wide(ReadFileText(f)));
    }
    for (size_t i = 0; i < 4; i++) {
        WriteFileText(f, "DecimalPlaces=5\r\nTheme=dark\r\n");
        Chmod(f, modes[i]);
        SaveTheme(f, ThemeMode::Auto, 6);
        Eq(L"the mode of the file stays after a save (" + Num(static_cast<long long>(modes[i])) + L")", Num(static_cast<long long>(modes[i])), Num(static_cast<long long>(ModeOf(f))));
    }
    const unsigned readOnly[] = { 0444, 0400, 0440 };
    for (size_t i = 0; i < 3; i++) {
        WriteFileText(f, "DecimalPlaces=5\r\nTheme=dark\r\n");
        Chmod(f, readOnly[i]);
        std::wstring name = Num(static_cast<long long>(readOnly[i]));
        False(L"a read-only settings file is not saved (it is the user's mark): mode " + name, Save(f, 3, ThemeMode::Light));
        False(L"... nor by SaveTheme", SaveTheme(f, ThemeMode::Light, 3));
        False(L"... nor by SavePlaces", SavePlaces(f, 3, ThemeMode::Light));
        Chmod(f, 0600);
        Eq(L"... and it is as it was", crlf5dark, Wide(ReadFileText(f)));
    }
    // A symbolic link to the file (a dotfiles manager makes them): the file behind it is replaced, the link stays.
    std::wstring target = P(dir, { L"link-target.ini" });
    std::wstring link = P(dir, { L"link.ini" });
    WriteFileText(target, "DecimalPlaces=5\r\nTheme=dark\r\n");
    True(L"a symbolic link is made", symlink("link-target.ini", Narrow8(link).c_str()) == 0);
    True(L"a save through the link", Save(link, 4, ThemeMode::Light));
    Eq(L"... changed the file behind it", L"DecimalPlaces=4\r\nTheme=light\r\n", Wide(ReadFileText(target)));
    struct stat st;
    True(L"... and the link is still a link", lstat(Narrow8(link).c_str(), &st) == 0 && S_ISLNK(st.st_mode));
    RemoveFile(link);
    RemoveFile(target);
    // A folder that cannot be written to.
    if (geteuid() != 0) {
        std::wstring ro = P(dir, { L"readonly-folder" });
        True(L"a folder is made", MakeFolder(ro));
        Chmod(ro, 0555);
        False(L"nothing is saved in a folder without write permission", Save(P(ro, { L"HourCalc.ini" }), 3, ThemeMode::Auto));
        Eq(L"... and no temporary file is left in it", L"0", Num(CountTmpFiles(ro)));
        Chmod(ro, 0755);
        RemoveFolder(ro);
    }
    Eq(L"no temporary file is left behind", L"0", Num(CountTmpFiles(dir)));
}

#endif

void SettingsSaveTests(const std::wstring& dir)
{
    using namespace settings;
    const std::wstring crlf5dark = L"DecimalPlaces=5\r\nTheme=dark\r\n";

    // ---- the folder is made by the first save (SPEC 6c), one level
    std::wstring sub = P(dir, { L"HourCalc" });
    std::wstring subIni = P(dir, { L"HourCalc", L"HourCalc.ini" });
    False(L"the folder does not exist yet", PathExists(sub));
    True(L"the first save makes the folder", Save(subIni, 4, ThemeMode::Light));
    Eq(L"... and the file is in it", L"DecimalPlaces=4\r\nTheme=light\r\n", Wide(ReadFileText(subIni)));
    True(L"a save into the existing folder", Save(subIni, 5, ThemeMode::Dark));
    Eq(L"... replaces the contents", crlf5dark, Wide(ReadFileText(subIni)));
    False(L"a chain of missing folders is not made", Save(P(dir, { L"a", L"b", L"HourCalc.ini" }), 3, ThemeMode::Auto));
    False(L"... not even its first folder", PathExists(P(dir, { L"a" })));
    std::wstring subTheme = P(dir, { L"fromtheme", L"HourCalc.ini" });
    True(L"SaveTheme makes the folder too", SaveTheme(subTheme, ThemeMode::Dark, 3));
    Eq(L"... and writes both", L"DecimalPlaces=3\r\nTheme=dark\r\n", Wide(ReadFileText(subTheme)));
    Eq(L"no temporary file is left in the folders", L"0", Num(CountTmpFiles(sub) + CountTmpFiles(P(dir, { L"fromtheme" })) + CountTmpFiles(dir)));
    RemoveFile(subTheme);
    RemoveFolder(P(dir, { L"fromtheme" }));

    // ---- one setting at a time: the other one is read from the file again
    std::wstring f = P(dir, { L"keep.ini" });
    RemoveFile(f);
    True(L"SaveTheme without a file: the places of the caller", SaveTheme(f, ThemeMode::Dark, 5));
    Eq(L"... both are in the file", crlf5dark, Wide(ReadFileText(f)));
    RemoveFile(f);
    True(L"SavePlaces without a file: the theme of the caller", SavePlaces(f, 4, ThemeMode::Light));
    Eq(L"... both are in the file", L"DecimalPlaces=4\r\nTheme=light\r\n", Wide(ReadFileText(f)));
    // Two HourCalc windows that both started with the defaults (2 places, Auto). A picks Dark, then B changes the places: the Dark
    // that A saved must survive, and the other way round.
    RemoveFile(f);
    True(L"A picks Dark", SaveTheme(f, ThemeMode::Dark, 2));
    True(L"B (which still believes in Auto) picks 4 places", SavePlaces(f, 4, ThemeMode::Auto));
    Eq(L"B keeps the Dark of A", L"DecimalPlaces=4\r\nTheme=dark\r\n", Wide(ReadFileText(f)));
    True(L"A (which still believes in 2 places) picks Light", SaveTheme(f, ThemeMode::Light, 2));
    Eq(L"A keeps the 4 places of B", L"DecimalPlaces=4\r\nTheme=light\r\n", Wide(ReadFileText(f)));
    Eq(L"a load gives both", L"4 1", LoadedBoth(f));
    // What the file says counts, also a file that holds only one of the settings or none.
    WriteFileText(f, "DecimalPlaces=6");
    True(L"a file with only the places", SaveTheme(f, ThemeMode::Dark, 3));
    Eq(L"... the places of the file stay, not those of the caller", L"DecimalPlaces=6\r\nTheme=dark\r\n", Wide(ReadFileText(f)));
    WriteFileText(f, "Theme=dark");
    True(L"a file with only the theme", SavePlaces(f, 5, ThemeMode::Light));
    Eq(L"... the theme of the file stays", crlf5dark, Wide(ReadFileText(f)));
    WriteFileText(f, "nothing useful in here\r\n");
    True(L"a file without a setting in it", SavePlaces(f, 5, ThemeMode::Dark));
    Eq(L"... has the default theme for the other one", L"DecimalPlaces=5\r\nTheme=auto\r\n", Wide(ReadFileText(f)));
    WriteFileText(f, "");
    True(L"an empty file", SaveTheme(f, ThemeMode::Light, 4));
    Eq(L"... has the default places for the other one", L"DecimalPlaces=2\r\nTheme=light\r\n", Wide(ReadFileText(f)));
    False(L"SavePlaces with an empty path", SavePlaces(L"", 3, ThemeMode::Auto));
    False(L"SaveTheme with an empty path", SaveTheme(L"", ThemeMode::Auto, 3));
    False(L"SaveTheme on a folder", SaveTheme(dir, ThemeMode::Dark, 3));
    False(L"SavePlaces on a folder", SavePlaces(dir, 3, ThemeMode::Dark));

    // ---- a file that is there but cannot be read is not overwritten with the defaults of a failed read
    LockedFileTests(f, dir);

    // ---- the marks of the file (attributes on Windows, permission bits on Linux)
    FileMarkTests(f, dir);

    // ---- a save is one step for a reader: it sees the old file or the new one, never an empty or half written one
    std::wstring torn = P(dir, { L"torn.ini" });
    True(L"the file the reader loads", Save(torn, 4, ThemeMode::Dark));
    ReaderState rs;
    rs.path = torn;
    rs.stop = 0;
    rs.reads = 0;
    rs.torn = 0;
    std::thread th(ReaderLoop, &rs);
    int saved = 0;
    int rounds = 0;
    // At least 100 saves, and as many more as it takes (a fast disk) for the reader to have loaded the file a few dozen times.
    while (rounds < 100 || (rs.reads <= 40 && rounds < 20000)) {
        bool a = (rounds & 1) != 0;
        if (Save(torn, a ? 4 : 5, a ? ThemeMode::Dark : ThemeMode::Light)) ++saved;
        ++rounds;
    }
    rs.stop = 1;
    th.join();
    True(L"the reader ran while the file was saved (" + Num(rs.reads) + L" loads)", rs.reads > 20);
    Eq(L"no load saw an empty or half written file", L"0", Num(rs.torn));
    True(L"the saves worked (" + Num(saved) + L" of " + Num(rounds) + L")", saved >= rounds / 2);
    True(L"the file holds one of the two states", LoadedBoth(torn) == L"4 2" || LoadedBoth(torn) == L"5 1");
    Eq(L"no temporary file is left behind by the saves", L"0", Num(CountTmpFiles(dir)));
    RemoveFile(torn);
    RemoveFile(f);
    RemoveFile(subIni);
    RemoveFolder(sub);
}

#ifdef _WIN32

// The default path (SPEC 6c): %LOCALAPPDATA%\HourCalc\HourCalc.ini. The environment variable of this test process is changed and put back.
void SettingsDefaultPathTests(const std::wstring& dir)
{
    using namespace settings;
    std::wstring old(32768, L'\0');
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", &old[0], static_cast<DWORD>(old.size()));
    bool had = n > 0 && n < old.size();
    old.resize(had ? n : 0);

    // What the shell says: the variable of a normal session holds this folder.
    std::wstring known;
    {
        PWSTR p = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_LocalAppData, KF_FLAG_DEFAULT, nullptr, &p)) && p) known = p;
        CoTaskMemFree(p);
    }
    True(L"the shell knows the local application data folder", known.size() > 3);
    std::wstring knownIni = known + L"\\" + FolderName + L"\\" + FileName;

    SetEnvironmentVariableW(L"LOCALAPPDATA", dir.c_str());
    Eq(L"the variable is used: <LOCALAPPDATA>\\HourCalc\\HourCalc.ini", dir + L"\\HourCalc\\HourCalc.ini", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", (dir + L"\\").c_str());
    Eq(L"... a trailing backslash does not double", dir + L"\\HourCalc\\HourCalc.ini", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", (dir + L"\\\\").c_str());
    Eq(L"... nor two", dir + L"\\HourCalc\\HourCalc.ini", DefaultPath());
    std::wstring longDir = dir + L"\\" + std::wstring(300, L'x');
    SetEnvironmentVariableW(L"LOCALAPPDATA", longDir.c_str());
    Eq(L"... a value that is longer than MAX_PATH is no folder of a user profile: no path (the settings last for the session)", L"", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", L"\\\\server\\share\\appdata");
    Eq(L"... a UNC path is absolute", L"\\\\server\\share\\appdata\\HourCalc\\HourCalc.ini", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", nullptr);
    Eq(L"without the variable there is no path (the settings last for the session)", L"", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", L"");
    Eq(L"an empty variable is no folder", L"", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", L"appdata");
    Eq(L"a relative path would put the file in the current folder: no path", L"", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", L".");
    Eq(L"... also a dot", L"", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", L"C:");
    Eq(L"... and a drive without a folder", L"", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", L"C:folder");
    Eq(L"... and one that is relative to the current folder of a drive", L"", DefaultPath());
    SetEnvironmentVariableW(L"LOCALAPPDATA", L"\\\\\\");
    Eq(L"... and slashes only", L"", DefaultPath());

    SetEnvironmentVariableW(L"LOCALAPPDATA", had ? old.c_str() : nullptr);
    std::wstring def = DefaultPath();
    True(L"the default path of the process ends in \\HourCalc\\HourCalc.ini", def.size() > 22 && def.compare(def.size() - 22, 22, L"\\HourCalc\\HourCalc.ini") == 0);
    // On this machine the variable and the shell agree (they differ only when a program was started with another environment).
    True(L"... and it is the one the shell gives for this user", def.size() == knownIni.size() &&
         CompareStringOrdinal(def.c_str(), static_cast<int>(def.size()), knownIni.c_str(), static_cast<int>(knownIni.size()), TRUE) == CSTR_EQUAL);
}

#else

// Sets (or, with nullptr, removes) an environment variable of this test process.
void SetEnv(const char* name, const char* value)
{
    if (value) setenv(name, value, 1);
    else unsetenv(name);
}

struct SavedEnv {
    std::string name;
    bool had;
    std::string value;
    explicit SavedEnv(const char* n) : name(n)
    {
        const char* v = std::getenv(n);
        had = v != nullptr;
        if (had) value = v;
    }
    void Restore() const { SetEnv(name.c_str(), had ? value.c_str() : nullptr); }
};

// The default path: $XDG_CONFIG_HOME/hourcalc/hourcalc.ini, else $HOME/.config/hourcalc/hourcalc.ini (HOME from the environment or
// the password database); the first save makes the .config folder too. The environment of this test process is changed and put back.
void SettingsDefaultPathTests(const std::wstring& dir)
{
    using namespace settings;
    SavedEnv xdg("XDG_CONFIG_HOME"), home("HOME");
    std::string d = Narrow8(dir);
    std::wstring tail = L"/hourcalc/hourcalc.ini";
    Eq(L"the file name", L"hourcalc.ini", FileName);
    Eq(L"the folder name", L"hourcalc", FolderName);

    SetEnv("XDG_CONFIG_HOME", d.c_str());
    Eq(L"XDG_CONFIG_HOME is used: <XDG_CONFIG_HOME>/hourcalc/hourcalc.ini", dir + tail, DefaultPath());
    SetEnv("XDG_CONFIG_HOME", (d + "/").c_str());
    Eq(L"... a trailing slash does not double", dir + tail, DefaultPath());
    SetEnv("XDG_CONFIG_HOME", (d + "//").c_str());
    Eq(L"... nor two", dir + tail, DefaultPath());
    SetEnv("HOME", "/home/someone-else");
    Eq(L"... HOME does not matter then", dir + tail, DefaultPath());
    SetEnv("XDG_CONFIG_HOME", "/");
    Eq(L"the root folder is a folder", L"/hourcalc/hourcalc.ini", DefaultPath());
    SetEnv("XDG_CONFIG_HOME", "relative/dir");
    Eq(L"a relative XDG_CONFIG_HOME is ignored (the specification says so): HOME/.config", L"/home/someone-else/.config" + tail, DefaultPath());
    SetEnv("XDG_CONFIG_HOME", "");
    Eq(L"an empty XDG_CONFIG_HOME is ignored", L"/home/someone-else/.config" + tail, DefaultPath());
    SetEnv("XDG_CONFIG_HOME", nullptr);
    Eq(L"without XDG_CONFIG_HOME: $HOME/.config/hourcalc/hourcalc.ini", L"/home/someone-else/.config" + tail, DefaultPath());
    SetEnv("HOME", "/home/someone-else///");
    Eq(L"... a trailing slash of HOME does not double", L"/home/someone-else/.config" + tail, DefaultPath());
    // No usable HOME: the password database knows the home folder of this user.
    std::wstring fromPasswd;
    if (struct passwd* pw = getpwuid(getuid())) {
        if (pw->pw_dir && pw->pw_dir[0] == '/') {
            std::string h = pw->pw_dir;
            while (h.size() > 1 && h[h.size() - 1] == '/') h.erase(h.size() - 1);
            fromPasswd = Wide(h) + L"/.config" + tail;
        }
    }
    SetEnv("HOME", nullptr);
    Eq(L"without HOME: the home folder from the password database", fromPasswd, DefaultPath());
    SetEnv("HOME", "relative");
    Eq(L"... also for a HOME that is not absolute", fromPasswd, DefaultPath());
    SetEnv("HOME", "");
    Eq(L"... and an empty one", fromPasswd, DefaultPath());

    // The first save makes ~/.config and the folder of the program, and nothing else: the base folder is the one parent that is made.
    SetEnv("XDG_CONFIG_HOME", nullptr);
    SetEnv("HOME", d.c_str());
    std::wstring dotConfig = P(dir, { L".config" });
    False(L"no .config yet", PathExists(dotConfig));
    std::wstring ini = DefaultPath();
    Eq(L"HOME = the test folder: the path", dir + L"/.config" + tail, ini);
    True(L"the first save makes .config and hourcalc", SaveTheme(ini, ThemeMode::Dark, 4));
    Eq(L"... and writes both settings", L"DecimalPlaces=4\r\nTheme=dark\r\n", Wide(ReadFileText(ini)));
    unsigned dotMode = ModeOf(dotConfig);
    Eq(L"... .config is private to the user (0700, the specification for a folder that is made on the way)", L"448", Num(dotMode));
    True(L"a load through the default path", Load(ini).decimalPlaces == 4 && Load(ini).theme == ThemeMode::Dark);
    RemoveFile(ini);
    RemoveFolder(P(dir, { L".config", L"hourcalc" }));
    RemoveFolder(dotConfig);
    // XDG_CONFIG_HOME that does not exist yet (its parent does): it is made as the base folder.
    SetEnv("XDG_CONFIG_HOME", Narrow8(P(dir, { L"xdgbase" })).c_str());
    std::wstring ini2 = DefaultPath();
    True(L"the first save makes the base folder of XDG_CONFIG_HOME", Save(ini2, 3, ThemeMode::Light));
    Eq(L"... and the file is in it", L"DecimalPlaces=3\r\nTheme=light\r\n", Wide(ReadFileText(ini2)));
    RemoveFile(ini2);
    RemoveFolder(P(dir, { L"xdgbase", L"hourcalc" }));
    RemoveFolder(P(dir, { L"xdgbase" }));
    // A folder name that is not UTF-8 (Linux allows any bytes in a name): the path keeps its bytes through the wide string.
    std::string odd = d + "/caf\xE9";
    True(L"a folder with a Latin-1 name is made", mkdir(odd.c_str(), 0777) == 0);
    SetEnv("XDG_CONFIG_HOME", odd.c_str());
    std::wstring oddIni = DefaultPath();
    True(L"... its path holds a stand-in for the odd byte", oddIni.find(static_cast<wchar_t>(0xDCE9)) != std::wstring::npos);
    True(L"... the settings are saved in it", Save(oddIni, 4, ThemeMode::Dark));
    struct stat oddStat;
    True(L"... in the folder with the odd name", stat((odd + "/hourcalc/hourcalc.ini").c_str(), &oddStat) == 0);
    Eq(L"... and are loaded from it", L"4 2", LoadedBoth(oddIni));
    unlink((odd + "/hourcalc/hourcalc.ini").c_str());
    rmdir((odd + "/hourcalc").c_str());
    rmdir(odd.c_str());
    // ... but a base folder two levels down is not made
    SetEnv("XDG_CONFIG_HOME", Narrow8(P(dir, { L"deep", L"xdgbase" })).c_str());
    False(L"a missing base folder two levels down is not made", Save(DefaultPath(), 3, ThemeMode::Light));
    False(L"... nothing of it", PathExists(P(dir, { L"deep" })));

    xdg.Restore();
    home.Restore();
    std::wstring def = DefaultPath();
    True(L"the default path of the process is a path or empty", def.empty() || def[0] == L'/');
    True(L"... and it ends in /hourcalc/hourcalc.ini", def.empty() || (def.size() > tail.size() && def.compare(def.size() - tail.size(), tail.size(), tail) == 0));
}

// The decimal separator of the user's locale, asked for the environment that selects it. Locales that are not installed are skipped
// (and said so): a build machine has C.UTF-8 and maybe en_US, often not nl_BE. Without root, a folder made with
// localedef -i nl_BE -f UTF-8 <folder>/nl_BE.UTF-8 and given as LOCPATH adds them.
void SeparatorLocaleTests()
{
    SavedEnv all("LC_ALL"), num("LC_NUMERIC"), lang("LANG");
    timemath::SetDecimalSeparatorOverride(L"");
    SetEnv("LC_NUMERIC", nullptr);
    SetEnv("LANG", nullptr);
    SetEnv("LC_ALL", "C");
    Eq(L"LC_ALL=C: the point", L".", timemath::DecimalSeparator());
    SetEnv("LC_ALL", "C.UTF-8");
    Eq(L"LC_ALL=C.UTF-8: the point", L".", timemath::DecimalSeparator());
    SetEnv("LC_ALL", "no_SUCH.UTF-8");
    Eq(L"a locale that does not exist: the point", L".", timemath::DecimalSeparator());
    SetEnv("LC_ALL", nullptr);
    Eq(L"nothing set: the point", L".", timemath::DecimalSeparator());
    // The global locale of the program is not the one that counts: a program may set LC_NUMERIC to "C" (Qt does).
    setlocale(LC_NUMERIC, "C");
    const struct { const char* locale; const wchar_t* sep; } wanted[] = {
        { "nl_BE.UTF-8", L"," }, { "de_DE.UTF-8", L"," }, { "fr_FR.UTF-8", L"," }, { "en_US.UTF-8", L"." }, { "de_CH.UTF-8", L"." }
    };
    for (size_t i = 0; i < sizeof(wanted) / sizeof(wanted[0]); ++i) {
        locale_t probe = newlocale(LC_NUMERIC_MASK, wanted[i].locale, static_cast<locale_t>(0));
        if (probe == static_cast<locale_t>(0)) {
            std::printf("note: locale %s is not installed here, its separator check is skipped\n", wanted[i].locale);
            continue;
        }
        freelocale(probe);
        std::wstring name = Wide(wanted[i].locale);
        SetEnv("LC_ALL", wanted[i].locale);
        Eq(L"LC_ALL=" + name + L": the separator of the locale", wanted[i].sep, timemath::DecimalSeparator());
        SetEnv("LC_ALL", nullptr);
        SetEnv("LANG", wanted[i].locale);
        Eq(L"LANG=" + name + L": the same", wanted[i].sep, timemath::DecimalSeparator());
        SetEnv("LC_NUMERIC", "C");
        Eq(L"... LC_NUMERIC=C wins over LANG", L".", timemath::DecimalSeparator());
        SetEnv("LC_NUMERIC", nullptr);
        SetEnv("LANG", nullptr);
    }
    all.Restore();
    num.Restore();
    lang.Restore();
}

#endif

void SettingsTests()
{
    using namespace settings;
    Eq(L"empty text", L"2", IntStr(ParseDecimalPlaces(L"")));
    Eq(L"DecimalPlaces=3", L"3", IntStr(ParseDecimalPlaces(L"DecimalPlaces=3")));
    Eq(L"with CRLF", L"4", IntStr(ParseDecimalPlaces(L"DecimalPlaces=4\r\n")));
    Eq(L"case and spaces", L"6", IntStr(ParseDecimalPlaces(L"  decimalplaces = 6  ")));
    Eq(L"comments and sections are skipped", L"5", IntStr(ParseDecimalPlaces(L"; comment\n# another\n[HourCalc]\nDecimalPlaces=5\n")));
    Eq(L"another key is ignored", L"2", IntStr(ParseDecimalPlaces(L"Other=4")));
    Eq(L"7 is invalid", L"2", IntStr(ParseDecimalPlaces(L"DecimalPlaces=7")));
    Eq(L"1 is invalid", L"2", IntStr(ParseDecimalPlaces(L"DecimalPlaces=1")));
    Eq(L"-3 is invalid", L"2", IntStr(ParseDecimalPlaces(L"DecimalPlaces=-3")));
    Eq(L"text is invalid", L"2", IntStr(ParseDecimalPlaces(L"DecimalPlaces=abc")));
    Eq(L"empty value is invalid", L"2", IntStr(ParseDecimalPlaces(L"DecimalPlaces=")));
    Eq(L"3.5 is invalid", L"2", IntStr(ParseDecimalPlaces(L"DecimalPlaces=3.5")));
    Eq(L"the first DecimalPlaces line decides", L"3", IntStr(ParseDecimalPlaces(L"DecimalPlaces=3\nDecimalPlaces=5")));
#ifdef _WIN32
    Eq(L"file name", L"HourCalc.ini", FileName);
#else
    Eq(L"file name", L"hourcalc.ini", FileName);
#endif

    // ---- the theme
    Eq(L"theme: empty text is auto", L"0", ThemeStr(ParseTheme(L"")));
    Eq(L"theme: the default is auto", L"0", ThemeStr(DefaultTheme));
    Eq(L"theme: Theme=auto", L"0", ThemeStr(ParseTheme(L"Theme=auto")));
    Eq(L"theme: Theme=light", L"1", ThemeStr(ParseTheme(L"Theme=light")));
    Eq(L"theme: Theme=dark", L"2", ThemeStr(ParseTheme(L"Theme=dark")));
    Eq(L"theme: key and value are not case sensitive", L"2", ThemeStr(ParseTheme(L"THEME=DARK")));
    Eq(L"theme: ... mixed case", L"1", ThemeStr(ParseTheme(L"tHeMe=LiGhT")));
    Eq(L"theme: spaces around key and value", L"1", ThemeStr(ParseTheme(L"  theme = Light  ")));
    Eq(L"theme: with CRLF", L"2", ThemeStr(ParseTheme(L"Theme=dark\r\n")));
    Eq(L"theme: comments and sections are skipped", L"2", ThemeStr(ParseTheme(L"; comment\n# another\n[HourCalc]\nTheme=dark\n")));
    Eq(L"theme: commented out", L"0", ThemeStr(ParseTheme(L";Theme=dark")));
    Eq(L"theme: a section line", L"0", ThemeStr(ParseTheme(L"[Theme=dark]")));
    Eq(L"theme: another key is ignored", L"0", ThemeStr(ParseTheme(L"Other=dark")));
    Eq(L"theme: a longer key is another key", L"0", ThemeStr(ParseTheme(L"Themes=dark")));
    Eq(L"theme: no value is auto", L"0", ThemeStr(ParseTheme(L"Theme=")));
    Eq(L"theme: no equals sign is auto", L"0", ThemeStr(ParseTheme(L"Theme")));
    Eq(L"theme: an unknown value is auto", L"0", ThemeStr(ParseTheme(L"Theme=blue")));
    Eq(L"theme: a longer value is auto", L"0", ThemeStr(ParseTheme(L"Theme=darker")));
    Eq(L"theme: two words are auto", L"0", ThemeStr(ParseTheme(L"Theme=dark light")));
    Eq(L"theme: a number is auto", L"0", ThemeStr(ParseTheme(L"Theme=2")));
    Eq(L"theme: the first Theme line decides", L"2", ThemeStr(ParseTheme(L"Theme=dark\nTheme=light")));
    Eq(L"theme: ... even when it is invalid", L"0", ThemeStr(ParseTheme(L"Theme=blue\nTheme=dark")));
    Eq(L"theme: a NUL after the value is not the value", L"0", ThemeStr(ParseTheme(std::wstring(L"Theme=dark") + L'\0')));
    Eq(L"theme: DecimalPlaces and Theme in either order (places)", L"4 2", BothOf(L"DecimalPlaces=4\r\nTheme=dark\r\n"));
    Eq(L"... the other order", L"5 1", BothOf(L"Theme=light\nDecimalPlaces=5"));
    Eq(L"... with duplicates the first of each key decides", L"3 2", BothOf(L"Theme=dark\nDecimalPlaces=3\nTheme=light\nDecimalPlaces=6"));
    Eq(L"an invalid theme does not change the places", L"3 0", BothOf(L"DecimalPlaces=3\nTheme=blue"));
    Eq(L"an invalid place count does not change the theme", L"2 2", BothOf(L"DecimalPlaces=9\nTheme=dark"));
    Eq(L"a file with only Theme has the default places", L"2 1", BothOf(L"Theme=light"));
    Eq(L"a file with only DecimalPlaces has the theme auto", L"6 0", BothOf(L"DecimalPlaces=6"));
    Eq(L"ThemeToString auto", L"auto", ThemeToString(ThemeMode::Auto));
    Eq(L"ThemeToString light", L"light", ThemeToString(ThemeMode::Light));
    Eq(L"ThemeToString dark", L"dark", ThemeToString(ThemeMode::Dark));
    Eq(L"ThemeToString of a value outside the enumeration is auto", L"auto", ThemeToString(static_cast<ThemeMode>(7)));
    // Letters outside ASCII do not fold onto the ASCII letters of the keys and values (OrdinalIgnoreCase of the reference).
    Eq(L"theme: a dotless i is no i", L"0", ThemeStr(ParseTheme(L"Theme=l\x0131ght")));
    Eq(L"theme: a Kelvin sign is no k", L"0", ThemeStr(ParseTheme(L"Theme=dar\x212A")));
    Eq(L"a long s is no s in a key", L"2", IntStr(ParseDecimalPlaces(L"DecimalPlace\x017F=4")));

    bool tempOk = false;
    std::wstring dir = NewTestFolderPath(tempOk);
    True(L"temp path", tempOk);
    Eq(L"a missing file", L"2", IntStr(LoadDecimalPlaces(P(dir, { L"missing.ini" }))));
    Eq(L"an empty path", L"2", IntStr(LoadDecimalPlaces(L"")));
    Eq(L"a missing file has the theme auto", L"0", ThemeStr(LoadTheme(P(dir, { L"missing.ini" }))));
    Eq(L"an empty path has the theme auto", L"0", ThemeStr(LoadTheme(L"")));
    {
        Values v = Load(P(dir, { L"missing.ini" }));
        Eq(L"Load of a missing file: the defaults", L"2 0", IntStr(v.decimalPlaces) + L" " + ThemeStr(v.theme));
        v = Load(L"");
        Eq(L"Load of an empty path: the defaults", L"2 0", IntStr(v.decimalPlaces) + L" " + ThemeStr(v.theme));
    }
    True(L"create the test folder", MakeFolder(dir));
    std::wstring path = P(dir, { L"HourCalc.ini" });
    True(L"save 4 and dark", Save(path, 4, ThemeMode::Dark));
    Eq(L"the file holds both settings, DecimalPlaces first", L"DecimalPlaces=4\r\nTheme=dark\r\n", Wide(ReadFileText(path)));
    Eq(L"load 4", L"4", IntStr(LoadDecimalPlaces(path)));
    Eq(L"load dark", L"2", ThemeStr(LoadTheme(path)));
    {
        Values v = Load(path);
        Eq(L"Load reads both", L"4 2", IntStr(v.decimalPlaces) + L" " + ThemeStr(v.theme));
    }
    True(L"save 6 and light", Save(path, 6, ThemeMode::Light));
    Eq(L"the file after a second save", L"DecimalPlaces=6\r\nTheme=light\r\n", Wide(ReadFileText(path)));
    Eq(L"load 6", L"6", IntStr(LoadDecimalPlaces(path)));
    Eq(L"load light", L"1", ThemeStr(LoadTheme(path)));
    True(L"save auto", Save(path, 3, ThemeMode::Auto));
    Eq(L"the file with the theme auto", L"DecimalPlaces=3\r\nTheme=auto\r\n", Wide(ReadFileText(path)));
    True(L"save an invalid value writes the default", Save(path, 12, ThemeMode::Dark));
    Eq(L"... which loads as the default", L"2", IntStr(LoadDecimalPlaces(path)));
    Eq(L"... and keeps the theme", L"2", ThemeStr(LoadTheme(path)));
    True(L"save a theme outside the enumeration", Save(path, 5, static_cast<ThemeMode>(9)));
    Eq(L"... is written as auto", L"DecimalPlaces=5\r\nTheme=auto\r\n", Wide(ReadFileText(path)));
    // Changing one setting never loses the other: read both, change one, save both.
    True(L"save 5 and dark", Save(path, 5, ThemeMode::Dark));
    {
        Values v = Load(path);
        True(L"change the places only", Save(path, 3, v.theme));
        Eq(L"... the theme survived", L"DecimalPlaces=3\r\nTheme=dark\r\n", Wide(ReadFileText(path)));
        v = Load(path);
        True(L"change the theme only", Save(path, v.decimalPlaces, ThemeMode::Light));
        Eq(L"... the places survived", L"DecimalPlaces=3\r\nTheme=light\r\n", Wide(ReadFileText(path)));
    }
    WriteFileText(path, "DecimalPlaces=3");
    Eq(L"a hand-written file", L"3", IntStr(LoadDecimalPlaces(path)));
    Eq(L"... without a Theme line", L"0", ThemeStr(LoadTheme(path)));
    WriteFileText(path, "theme = DARK\r\n; the places are missing\r\n");
    {
        Values v = Load(path);
        Eq(L"a hand-written file with only a theme", L"2 2", IntStr(v.decimalPlaces) + L" " + ThemeStr(v.theme));
    }
    WriteFileText(path, "\xEF\xBB\xBF" "DecimalPlaces=5\r\n");
    Eq(L"a file with a UTF-8 byte order mark", L"5", IntStr(LoadDecimalPlaces(path)));
    WriteFileText(path, "\xEF\xBB\xBF" "Theme=light\r\n");
    Eq(L"... and a theme in it", L"1", ThemeStr(LoadTheme(path)));
    WriteFileText(path, std::string("\xFF\xFE" "D\0e\0c\0i\0m\0a\0l\0P\0l\0a\0c\0e\0s\0=\0" "4\0", 32));
    Eq(L"a UTF-16 file", L"4", IntStr(LoadDecimalPlaces(path)));
    WriteFileText(path, std::string("\xFF\xFE" "T\0h\0e\0m\0e\0=\0" "d\0a\0r\0k\0", 22));
    Eq(L"a UTF-16 file with a theme", L"2", ThemeStr(LoadTheme(path)));
    // File.ReadAllText never flushes its decoder at the end of the file: an incomplete character at the very end is dropped,
    // so the value in front of it still counts. A complete character after the value makes it text, not a number.
    WriteFileText(path, "DecimalPlaces=3\xC3");
    Eq(L"an incomplete 2 byte UTF-8 sequence after the value is dropped", L"3", IntStr(LoadDecimalPlaces(path)));
    WriteFileText(path, "DecimalPlaces=3\xE2\x82");
    Eq(L"an incomplete 3 byte UTF-8 sequence after the value is dropped", L"3", IntStr(LoadDecimalPlaces(path)));
    WriteFileText(path, "DecimalPlaces=3\xF0\x9F\x98");
    Eq(L"an incomplete 4 byte UTF-8 sequence after the value is dropped", L"3", IntStr(LoadDecimalPlaces(path)));
    WriteFileText(path, "DecimalPlaces=3\xC3\xA9");
    Eq(L"a complete UTF-8 character after the value makes it invalid", L"2", IntStr(LoadDecimalPlaces(path)));
    WriteFileText(path, "DecimalPlaces=3\xE0\x80");
    Eq(L"a bad second byte is an error, not an incomplete sequence", L"2", IntStr(LoadDecimalPlaces(path)));
    WriteFileText(path, "Theme=dark\xC3");
    Eq(L"theme: an incomplete UTF-8 sequence after the value is dropped", L"2", ThemeStr(LoadTheme(path)));
    WriteFileText(path, "Theme=dark\xC3\xA9");
    Eq(L"theme: a complete UTF-8 character after the value makes it invalid", L"0", ThemeStr(LoadTheme(path)));
    // A bad byte does not swallow the byte after it: the key on the next line is still found.
    WriteFileText(path, "\xC3\r\nDecimalPlaces=4\r\n");
    Eq(L"a lead byte in front of a line end does not swallow it", L"4", IntStr(LoadDecimalPlaces(path)));
    WriteFileText(path, "\xE2\x82\nTheme=dark\n");
    Eq(L"... also an incomplete 3 byte sequence", L"2", ThemeStr(LoadTheme(path)));
    WriteFileText(path, std::string("\xFF\xFE" "D\0e\0c\0i\0m\0a\0l\0P\0l\0a\0c\0e\0s\0=\0" "3\0" "\x3D\xD8", 34));
    Eq(L"a lone high surrogate at the end of a UTF-16 file is dropped", L"3", IntStr(LoadDecimalPlaces(path)));
    WriteFileText(path, std::string("\xFF\xFE" "D\0e\0c\0i\0m\0a\0l\0P\0l\0a\0c\0e\0s\0=\0" "3\0" "\x3D\xD8" "\x00\xDE", 36));
    Eq(L"a complete surrogate pair after the value makes it invalid", L"2", IntStr(LoadDecimalPlaces(path)));
    True(L"a longer file is cut when saved", WriteFileText(path, "DecimalPlaces=3\r\nTheme=dark\r\nsome more text that goes\r\n") && Save(path, 5, ThemeMode::Light));
    Eq(L"... to the two settings", L"DecimalPlaces=5\r\nTheme=light\r\n", Wide(ReadFileText(path)));
    False(L"a folder whose parent does not exist is not an error, just not saved", Save(P(dir, { L"nope", L"deeper", L"HourCalc.ini" }), 3, ThemeMode::Auto));
    False(L"... and not made", PathExists(P(dir, { L"nope" })));
    False(L"an empty path is not saved", Save(L"", 3, ThemeMode::Auto));
    False(L"a path that is a folder is not saved", Save(dir, 3, ThemeMode::Auto));
    RemoveFile(path);
    SettingsSaveTests(dir);
    SettingsDefaultPathTests(dir);
    Eq(L"nothing is left in the test folder (no temporary files)", L"0", Num(CountTmpFiles(dir)));
    RemoveFolder(dir);
}

void RunAll(const wchar_t* sep)
{
    g_sep = sep;
    timemath::SetDecimalSeparatorOverride(sep);
    Eq(L"separator override", sep, timemath::DecimalSeparator());
    FormatTests();
    ConverterTableTests();
    ConverterFilterTests();
    ConverterRoundTripTests();
    CalcRequiredTests();
    CalcExpressionTests();
    CalcEntryTests();
    CalcStateTests();
    CalcErrorTests();
    CalcPasteTests();
    CalcPrecisionTests();
    CalcLimitTests();
    ConvertButtonTests();
    DecimalPlacesTests();
    DecTests();
    SettingsTests();
}

} // namespace

int main()
{
    RunAll(L",");
    RunAll(L".");
#ifndef _WIN32
    SeparatorLocaleTests();
#endif
    timemath::SetDecimalSeparatorOverride(L"");
    g_sep = L"user";
    True(L"the user's separator is not empty", !timemath::DecimalSeparator().empty());
    std::printf("\nHourCalc core tests: %d checks, %d failed.\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
