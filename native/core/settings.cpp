// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Port of oracle\Settings.cs: the settings (decimal places and theme), kept in HourCalc.ini in %LOCALAPPDATA%\HourCalc (Windows) or
// hourcalc.ini in $XDG_CONFIG_HOME/hourcalc (Linux). The parsing and the file format are the same on both; everything that touches
// the operating system (where the file is, reading it, replacing it, comparing text without case) is in platform.h. No exception
// ever leaves this file.

#include "hc_internal.h"
#include "platform.h"

namespace hc {
namespace settings {

namespace {

const wchar_t* const Key = L"DecimalPlaces";
const wchar_t* const ThemeKey = L"Theme";

// int.TryParse(s, NumberStyles.None, InvariantCulture): ASCII digits only (no sign, no white
// space); the runtime also accepts trailing NUL characters. Overflow is a failure.
bool TryParseInt(const std::wstring& s, int& n)
{
    size_t end = s.size();
    while (end > 0 && s[end - 1] == L'\0') --end;
    if (end == 0) return false;
    long long v = 0;
    for (size_t i = 0; i < end; ++i) {
        if (s[i] < L'0' || s[i] > L'9') return false;
        v = v * 10 + (s[i] - L'0');
        if (v > 2147483647ll) return false;
    }
    n = static_cast<int>(v);
    return true;
}

// Length of the incomplete UTF-8 sequence at the end of the data, 0 when there is none. An incomplete tail
// is a well-formed start of a sequence (a lead byte and fewer continuation bytes than it needs): File.ReadAllText
// never flushes the decoder of its StreamReader at the end of the file, so these bytes are not decoded at all
// (MultiByteToWideChar would turn them into U+FFFD). A bad byte inside the tail makes it an error, not a tail.
size_t IncompleteUtf8Tail(const unsigned char* p, size_t n)
{
    for (size_t back = 1; back <= 3 && back <= n; ++back) {
        unsigned char lead = p[n - back];
        if ((lead & 0xC0) == 0x80) continue;                       // a continuation byte: the lead byte is further back
        size_t need;
        if (lead >= 0xC2 && lead <= 0xDF) need = 2;
        else if (lead >= 0xE0 && lead <= 0xEF) need = 3;
        else if (lead >= 0xF0 && lead <= 0xF4) need = 4;
        else return 0;                                              // ASCII or a byte that can never start a sequence
        if (back >= need) return 0;
        for (size_t i = 1; i < back; ++i) {
            unsigned char lo = 0x80, hi = 0xBF;
            if (i == 1) {                                           // the second byte has a narrower range after these leads
                if (lead == 0xE0) lo = 0xA0;
                else if (lead == 0xED) hi = 0x9F;
                else if (lead == 0xF0) lo = 0x90;
                else if (lead == 0xF4) hi = 0x8F;
            }
            unsigned char c = p[n - back + i];
            if (c < lo || c > hi) return 0;
        }
        return back;
    }
    return 0;
}

// A high surrogate at the end of UTF-16 text is kept back by the decoder in the same way.
void DropLoneHighSurrogate(std::wstring& s)
{
    if (!s.empty() && s.back() >= 0xD800 && s.back() <= 0xDBFF) s.pop_back();
}

// File.ReadAllText: UTF-8 unless a byte order mark says otherwise.
std::wstring Decode(const std::string& bytes)
{
    const unsigned char* p = reinterpret_cast<const unsigned char*>(bytes.data());
    size_t n = bytes.size();
    std::wstring out;
    // (Little and big endian share their loops: the exe is measured in bytes, docs\SIZE.md.)
    bool u32le = n >= 4 && p[0] == 0xFF && p[1] == 0xFE && p[2] == 0 && p[3] == 0;
    bool u32be = n >= 4 && p[0] == 0 && p[1] == 0 && p[2] == 0xFE && p[3] == 0xFF;
    if (u32le || u32be) {                                                            // UTF-32
        for (size_t i = 4; i + 3 < n; i += 4) {
            unsigned long c = 0;
            for (size_t k = 0; k < 4; ++k) c = (c << 8) | p[i + (u32be ? k : 3 - k)];
            if (c >= 0x10000 && c <= 0x10FFFF) {
                c -= 0x10000;
                out += static_cast<wchar_t>(0xD800 + (c >> 10));
                out += static_cast<wchar_t>(0xDC00 + (c & 0x3FF));
            } else if (c < 0x10000) {
                out += static_cast<wchar_t>(c);
            } else {
                out += static_cast<wchar_t>(0xFFFD);
            }
        }
        return out;
    }
    bool u16le = n >= 2 && p[0] == 0xFF && p[1] == 0xFE;
    bool u16be = n >= 2 && p[0] == 0xFE && p[1] == 0xFF;
    if (u16le || u16be) {                                                            // UTF-16
        for (size_t i = 2; i + 1 < n; i += 2) out += static_cast<wchar_t>(u16be ? (p[i] << 8) | p[i + 1] : p[i] | (p[i + 1] << 8));
        DropLoneHighSurrogate(out);
        return out;
    }
    size_t skip = (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) ? 3 : 0;  // UTF-8 (BOM optional)
    if (n <= skip) return out;
    size_t size = n - skip;
    size -= IncompleteUtf8Tail(p + skip, size);
    if (size == 0) return out;
    return platform::DecodeUtf8(bytes.data() + skip, size);
}

// string.Equals(s, word, StringComparison.OrdinalIgnoreCase)
bool EqualsIgnoreCase(const std::wstring& s, const wchar_t* word)
{
    return platform::EqualsIgnoreCase(s, word);
}

// The value of the first line "key=value" of an ini text (the key is not case sensitive, key and value are trimmed). Blank
// lines, ';' and '#' comments and [sections] are ignored. False when no line has the key.
bool TryFindValue(const std::wstring& text, const wchar_t* key, std::wstring& value)
{
    value.clear();
    if (text.empty()) return false;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t nl = text.find(L'\n', pos);
        size_t end = nl == std::wstring::npos ? text.size() : nl;
        std::wstring line = detail::Trim(text.substr(pos, end - pos));
        pos = end + 1;
        if (line.empty() || line[0] == L';' || line[0] == L'#' || line[0] == L'[') continue;
        size_t eq = line.find(L'=');
        if (eq == std::wstring::npos || eq == 0) continue;
        if (!EqualsIgnoreCase(detail::Trim(line.substr(0, eq)), key)) continue;
        value = detail::Trim(line.substr(eq + 1));
        return true;
    }
    return false;
}

// How reading the file went: Read (text has the contents), Absent (there is no such file, or no such folder: nothing to lose), or
// Failed (something is there that cannot be read: locked, no access, a folder). Loading treats the last two alike (the defaults);
// saving must not: a file that is there but unreadable must not be overwritten with defaults.
typedef platform::ReadResult ReadResult;

// The whole file as text (File.ReadAllText).
ReadResult ReadText(const std::wstring& path, std::wstring& text)
{
    text.clear();
    std::string bytes;
    ReadResult r = platform::ReadFileBytes(path, bytes);
    if (r == ReadResult::Read) text = Decode(bytes);
    return r;
}

// One setting changed: the file is read again for the other one (see SavePlaces / SaveTheme in hc_core.h).
bool SaveChanged(const std::wstring& path, bool placesChanged, int places, ThemeMode theme)
{
    if (path.empty()) return false;
    std::wstring text;
    ReadResult r = ReadText(path, text);
    if (r == ReadResult::Failed) return false;             // there, but not readable: do not overwrite what cannot be seen
    if (r == ReadResult::Read) {
        if (placesChanged) theme = ParseTheme(text);
        else places = ParseDecimalPlaces(text);
    }
    return Save(path, places, theme);
}

} // namespace

std::wstring DefaultPath()
{
    return platform::SettingsPath();
}

int ParseDecimalPlaces(const std::wstring& text)
{
    std::wstring value;
    if (!TryFindValue(text, Key, value)) return timemath::DefaultDecimals;
    int n;
    if (TryParseInt(value, n) && n >= timemath::MinDisplayDecimals && n <= timemath::MaxDisplayDecimals) return n;
    return timemath::DefaultDecimals;
}

ThemeMode ParseTheme(const std::wstring& text)
{
    std::wstring value;
    if (!TryFindValue(text, ThemeKey, value)) return DefaultTheme;
    if (EqualsIgnoreCase(value, L"light")) return ThemeMode::Light;
    if (EqualsIgnoreCase(value, L"dark")) return ThemeMode::Dark;
    return ThemeMode::Auto;
}

int LoadDecimalPlaces(const std::wstring& path)
{
    std::wstring text;
    if (ReadText(path, text) != ReadResult::Read) return timemath::DefaultDecimals;
    return ParseDecimalPlaces(text);
}

ThemeMode LoadTheme(const std::wstring& path)
{
    std::wstring text;
    if (ReadText(path, text) != ReadResult::Read) return DefaultTheme;
    return ParseTheme(text);
}

Values Load(const std::wstring& path)
{
    Values v;
    v.decimalPlaces = timemath::DefaultDecimals;
    v.theme = DefaultTheme;
    std::wstring text;
    if (ReadText(path, text) == ReadResult::Read) {
        v.decimalPlaces = ParseDecimalPlaces(text);
        v.theme = ParseTheme(text);
    }
    return v;
}

const wchar_t* ThemeToString(ThemeMode theme)
{
    if (theme == ThemeMode::Light) return L"light";
    if (theme == ThemeMode::Dark) return L"dark";
    return L"auto";
}

bool Save(const std::wstring& path, int places, ThemeMode theme)
{
    // "DecimalPlaces=<n>" CR LF "Theme=<value>" CR LF, plain ASCII (the reference writes UTF-8 without a mark).
    char text[40] = "DecimalPlaces=";
    size_t n = 14;
    text[n++] = static_cast<char>('0' + timemath::ClampDecimals(places));
    for (const char* p = "\r\nTheme="; *p; ++p) text[n++] = *p;
    for (const wchar_t* p = ThemeToString(theme); *p; ++p) text[n++] = static_cast<char>(*p);
    text[n++] = '\r';
    text[n++] = '\n';
    return platform::WriteFileAtomic(path, text, n);
}

bool SavePlaces(const std::wstring& path, int places, ThemeMode currentTheme)
{
    return SaveChanged(path, true, places, currentTheme);
}

bool SaveTheme(const std::wstring& path, ThemeMode theme, int currentPlaces)
{
    return SaveChanged(path, false, currentPlaces, theme);
}

} // namespace settings
} // namespace hc
