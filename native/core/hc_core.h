// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc core - the time arithmetic, the converter field logic, the calculator state machine
// and the settings file. No UI, no platform headers: the same sources build on Windows (MSVC) and on
// Linux (g++); the few platform calls (user's decimal separator, settings path, file I/O) live in
// platform_win.cpp and platform_posix.cpp. A line-by-line port of the C# reference in oracle\*.cs, which
// stays the specification: for every input both must produce the same strings
// (tests\Differential.cs checks that).
//
// Numbers are hc::Dec, an own implementation of the 96-bit scaled decimal of System.Decimal (the
// reference's arithmetic) on 32-bit limbs. No binary floating point anywhere. Text is std::wstring:
// wchar_t is UTF-16 on Windows and UTF-32 on Linux, so a front end converts at its edge.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace hc {

// ------------------------------------------------------------------ decimal

// A System.Decimal compatible value: a 96-bit unsigned mantissa, a scale 0..28 (the number of fraction
// digits) and a sign, with the arithmetic of System.Decimal (.NET Framework 4.8): add and subtract
// align the scales and, when the exact result needs more than 96 bits, drop the surplus digits with
// rounding to the nearest even digit (the sticky rule of the runtime); multiply does the same with the
// scales added; divide builds the quotient digit by digit up to the largest scale (at most 28) at which it
// still fits 96 bits, rounds to even, strips the trailing zeros of an inexact quotient and keeps those of
// an exact one down to the preferred scale (the scale of the dividend minus that of the divisor, at least
// 0): 10.00m / 4m is 2.50 and 1m / 8m is 0.125. The results are bit for bit those of System.Decimal,
// scale included, quirks and all (a zero product of operands larger than 32 bits is a plain 0 and a product that
// loses 20 or more digits too; a quotient whose last rounding would carry out of 96 bits is rounded one digit
// earlier; a quotient whose divisor has more than 64 bits is rounded with the runtime's slip, which loses the carry out of a
// limb of exactly 2^31 when it doubles the remainder, see RoundsUpWide in dec.cpp): tests\Differential.cs and
// tests\DifferentialReview.cs prove it over tens of millions of operations on Windows and on Linux (the first one has a suite
// of built near-half divisions for that slip). Operations that overflow the 96-bit range throw DecOverflow.
//
// The same code runs on both platforms (it replaced a wrapper around oleaut32, whose division rounds one
// digit early or reports an overflow for an exact quotient that still fits when the 96-bit mantissa lies within 2^37
// of 2^96). Add, sub, mul, compare, rounding, truncation, ToInt64 and Parse have no known difference, for text that
// is a number with at most 28 fraction digits and at most 29 significant digits.
struct DecOverflow {};
struct DecFormat {};                     // thrown by Dec::Parse on text that is not a number

class Dec {
public:
    Dec();                               // 0
    Dec(long long v);                    // exact integer
    // "123.456", "-0.5"; invariant '.' only; throws DecFormat on bad text, DecOverflow on a value that does not fit.
    // Only text that is plain digits with at most one '.' and an optional leading '-' is in the contract, with
    // at most 28 fraction digits and at most 29 significant digits (what the core produces and what Filter
    // lets through). decimal.Parse of the reference also takes white space, a trailing sign and thousands
    // separators (rejected here) and rounds an excess of digits half to even, where this rounds half up.
    static Dec Parse(const std::wstring& invariant);
    // Exact text, '.' separator, exactly like decimal.ToString(InvariantCulture): the scale is kept
    // (7.50m prints "7.50"), a zero has no sign.
    std::wstring ToInvariant() const;

    Dec operator+(const Dec& o) const;
    Dec operator-(const Dec& o) const;
    Dec operator*(const Dec& o) const;
    Dec operator/(const Dec& o) const;   // throws DecDivideByZero on o == 0
    Dec operator-() const;
    bool operator==(const Dec& o) const; bool operator!=(const Dec& o) const;
    bool operator<(const Dec& o) const;  bool operator<=(const Dec& o) const;
    bool operator>(const Dec& o) const;  bool operator>=(const Dec& o) const;

    bool IsZero() const;
    bool IsNegative() const;             // < 0 (a negative zero counts as zero)
    Dec Abs() const;
    Dec Truncate() const;                // toward zero
    // Math.Round(value, places, MidpointRounding.AwayFromZero) with 0 <= places <= 28.
    Dec RoundAwayFromZero(int places) const;
    // Math.Round(value, places) (banker's rounding), when the reference uses it.
    Dec RoundToEven(int places) const;
    long long ToInt64() const;           // truncates; throws DecOverflow when out of range

private:
    int Scale() const { return static_cast<int>((flags_ >> 16) & 0xFFu); }
    Dec AddSub(const Dec& o, bool subtract) const;
    int Compare(const Dec& o) const;
    static Dec Make(const uint32_t* mantissa, int scale, bool negative);
    static Dec Fit(uint32_t* wide, int scale, bool negative);   // wide: 7 limbs, see dec.cpp

    uint32_t m_[3];                      // the 96-bit mantissa, m_[0] = lowest 32 bits
    uint32_t flags_;                     // the scale in bits 16..23, the sign in bit 31 (the System.Decimal layout)
};
struct DecDivideByZero {};

// ------------------------------------------------------------------ TimeMath (oracle\TimeMath.cs)

namespace timemath {
    const int MaxHourDigits = 7;
    const int MaxMinuteDigits = 2;
    const int MaxDecimals = 6;
    const int DefaultDecimals = 2;
    const int MinDisplayDecimals = 2;
    const int MaxDisplayDecimals = 6;
    Dec LimitMinutes();                  // 10 000 000 h * 60

    // The user's decimal separator, unless an override is set (tests and the CLI emulate cultures with it). Windows:
    // GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL), read on every call. Linux: the radix character of the
    // numeric locale that the environment selects (LC_ALL, LC_NUMERIC, LANG), read with newlocale, so that a program that
    // keeps its own LC_NUMERIC at "C" (Qt does) still gets the user's separator; "." when the locale is not installed.
    std::wstring DecimalSeparator();
    void SetDecimalSeparatorOverride(const std::wstring& sep);   // "" clears the override

    bool IsDigit(wchar_t c);
    bool AllDigits(const std::wstring& s);
    int ClampDecimals(int places);
    Dec RoundMinutes(const Dec& minutes);
    Dec RoundHours(const Dec& hours);
    Dec RoundHours(const Dec& hours, int places);
    std::wstring FormatHm(const Dec& minutes);
    std::wstring FormatDecimal(const Dec& minutes);
    std::wstring FormatDecimal(const Dec& minutes, int places);
    std::wstring FormatWords(const Dec& minutes);
    std::wstring FormatFactor(const Dec& minutes);
    bool TryParseEntry(const std::wstring& entry, Dec& minutes);
}

// ------------------------------------------------------------------ Converter (oracle\Converter.cs)

namespace converter {
    const int Left = 0;
    const int Right = 1;
    std::wstring Filter(int field, const std::wstring& text);
    std::wstring FilterField(int field, const std::wstring& text);
    // For text that went through Filter / FilterField (what every function of the core and the UI does
    // before it calls this). Unfiltered text is outside the contract: the Left field without a ':' throws
    // DecFormat on a character that is not a digit, where the reference accepts a few (a trailing '+' or
    // '-', white space, a ',' as thousands separator), and see Dec::Parse for the digits beyond 28 decimals.
    bool TryGetMinutes(int field, const std::wstring& text, Dec& minutes);
    std::wstring Format(int field, const Dec& minutes);
    std::wstring Format(int field, const Dec& minutes, int places);
    std::wstring Normalize(int field, const std::wstring& text);
    std::wstring Normalize(int field, const std::wstring& text, int places);
    void Compute(int sourceField, const std::wstring& sourceText, std::wstring& otherText, std::wstring& caption);
    void Compute(int sourceField, const std::wstring& sourceText, int places, std::wstring& otherText, std::wstring& caption);
    void Convert(int field, const std::wstring& text, int places, std::wstring& normalized, std::wstring& otherText, std::wstring& caption);
}

// ------------------------------------------------------------------ CalcEngine (oracle\CalcEngine.cs)

class CalcEngine {
public:
    static const wchar_t* DivideByZeroText;   // L"Cannot divide by zero"
    static const wchar_t* OverflowText;       // L"Overflow"

    CalcEngine();
    explicit CalcEngine(int decimalPlaces);

    int DecimalPlaces() const;
    void SetDecimalPlaces(int places);        // clamps like the reference setter

    std::wstring Expression() const;
    std::wstring MainText() const;
    std::wstring DecimalText() const;
    std::wstring Caption() const;
    bool IsError() const;
    bool HasEntry() const;
    bool IsEnabled(const std::wstring& key) const;
    std::wstring CopyMainText() const;
    std::wstring CopyDecimalText() const;

    // Keys as in the reference: L"0".."9", L":", L",", L"+", L"-", L"*", L"/", L"=", L"CE", L"C",
    // L"BACK", and the aliases the reference's Canon() accepts.
    void Press(const std::wstring& key);
    bool Paste(const std::wstring& text);

private:
    // The reference's private state and helpers, one to one.
    Dec CurrentMinutes() const;
    std::wstring DisplayEntry() const;
    void Digit(wchar_t d);
    void Separator(wchar_t s);
    void BeginEntry();
    void Backspace();
    void ClearEntry();
    void ResetAll();
    void StartNew();
    void Operator(wchar_t o);
    void Equal();
    void Finish(wchar_t o, const Dec& v);
    void ClearEntryState();
    Dec EntryMinutes() const;
    bool Apply(wchar_t o, const Dec& operandMinutes);

    int places_;
    std::vector<std::wstring> expr_;
    std::wstring entry_;
    bool entryActive_;
    bool hasAcc_;
    Dec acc_;
    wchar_t pendingOp_;
    wchar_t lastOp_;
    Dec lastOperand_;
    bool hasLast_;
    bool justEq_;
    const wchar_t* error_;
    // (add further members if the reference has them; keep the names recognisable)
};

// ------------------------------------------------------------------ Settings (oracle\Settings.cs)

// The settings file holds two settings, "DecimalPlaces=<n>" and "Theme=<auto|light|dark>". On Windows it is HourCalc.ini in
// %LOCALAPPDATA%\HourCalc\ (SPEC 6c), on Linux hourcalc.ini in $XDG_CONFIG_HOME/hourcalc/ ($HOME/.config/hourcalc/ without that
// variable). It is read at startup and rewritten (always with both lines, DecimalPlaces first, the lines ended by CR LF on
// both systems so that the file is byte for byte the same) when a setting changes. A changed setting is written with SavePlaces
// or SaveTheme, which read the file again and keep the other setting as it is on disk, so one setting never loses the other,
// not even when a second HourCalc has changed it in the meantime. The file is replaced in one step (a temporary file, then a
// rename), so a reader never sees half a file. A missing or invalid file, key or value means the defaults (2 places, theme Auto).
namespace settings {
    // Which theme the window uses: follow the system theme (Auto), or force Light or Dark. The numbers are part of the
    // test protocol (hc_cli): 0, 1, 2.
    enum class ThemeMode { Auto = 0, Light = 1, Dark = 2 };
    const ThemeMode DefaultTheme = ThemeMode::Auto;

    // Both settings, as read from the file.
    struct Values {
        int decimalPlaces;
        ThemeMode theme;
    };

#ifdef _WIN32
    const wchar_t* const FileName = L"HourCalc.ini";
    const wchar_t* const FolderName = L"HourCalc";    // the folder in %LOCALAPPDATA% that holds it
#else
    const wchar_t* const FileName = L"hourcalc.ini";
    const wchar_t* const FolderName = L"hourcalc";    // the folder in $XDG_CONFIG_HOME that holds it
#endif
    // The settings file (the folder is created by the first save); "" when there is no folder for it, and the settings then only
    // last for the session. Windows: %LOCALAPPDATA%\HourCalc\HourCalc.ini, the folder being the environment variable LOCALAPPDATA
    // when that holds an absolute path (it costs nothing; SHGetKnownFolderPath gives the same folder but loads the shell storage
    // DLLs, about 20 ms and a dozen modules at start-up); without such a variable there is no folder. Linux:
    // $XDG_CONFIG_HOME/hourcalc/hourcalc.ini when that variable holds an absolute path, otherwise $HOME/.config/hourcalc/hourcalc.ini
    // (HOME from the environment or the password database); the first save also makes the .config folder itself when it is missing.
    std::wstring DefaultPath();
    int ParseDecimalPlaces(const std::wstring& text);
    ThemeMode ParseTheme(const std::wstring& text);   // "Theme=auto|light|dark", any case; anything else = Auto
    int LoadDecimalPlaces(const std::wstring& path);  // "" path or missing/invalid file = default
    ThemeMode LoadTheme(const std::wstring& path);
    Values Load(const std::wstring& path);            // both settings, one read
    // "auto", "light" or "dark" (a value outside the enumeration is written as "auto").
    const wchar_t* ThemeToString(ThemeMode theme);
    // Writes the file with both settings, "DecimalPlaces=<n>\r\nTheme=<value>\r\n" (a place count outside 2..6 is written as the
    // default, 2). The whole file is made in a temporary file next to it, which then replaces it in one step. The folder is created
    // when it does not exist (one level; its parent must exist; on Linux the .config folder of the user is the one exception). False
    // when the file cannot be written: a read-only file (on Linux: one without any write permission bit), a path that is a folder, a
    // folder that cannot be made (the setting then lasts for the session only). This is the primitive; the UI changes a single
    // setting with SavePlaces and SaveTheme.
    bool Save(const std::wstring& path, int places, ThemeMode theme);
    // A single setting changed: reads the file again and writes it with the new value of that setting and the OTHER setting as it
    // is in the file now (what another HourCalc may have saved since this one started). The other setting that the caller still
    // has ('currentTheme', 'currentPlaces') is used only when there is no file. When the file exists but cannot be read (locked
    // by an editor or a virus scanner) nothing is written and the result is false: the defaults that a failed read would give
    // must never replace what is on disk.
    bool SavePlaces(const std::wstring& path, int places, ThemeMode currentTheme);
    bool SaveTheme(const std::wstring& path, ThemeMode theme, int currentPlaces);
}

} // namespace hc
