// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// hc_cli - console front end of the HourCalc core, for the differential test (tests\Differential.cs)
// and for poking at the core by hand.
//
// Line protocol. Stdin is read until EOF; every request line gets exactly one answer line on stdout.
// Output is flushed whenever the input has run dry, i.e. before the program would block, so a
// request/answer conversation sees every answer at once and a big batch is written in big blocks.
//
// A request is a command and its arguments separated by single spaces. Everything is plain ASCII;
// inside an argument or a field these escapes stand for the characters that cannot appear raw:
//     \\  backslash     \s  space     \n  LF     \r  CR     \t  tab     \p  '|'
//     \uXXXX  any UTF-16 code unit (4 hex digits)      \e  the empty string (a whole token only)
// The answer is a token (or several, separated by spaces) in the same escaping, or an error:
// !OVERFLOW !DIVZERO !FORMAT !EXC (an exception of the core), !UNKNOWN (no such command), !ARGS.
// <dec> is an invariant decimal ("-12.50", scale kept); booleans are 1 and 0; <field> is 0 (left,
// hours:minutes) or 1 (right, decimal hours); [places] is optional: absent = the overload without it.
//
//   sep <str>                      decimal separator override; \e clears (user culture)   -> ok
//   ping                                                                                    -> pong
//   os                                                                                      -> windows | linux
//   tm.sep | tm.limit | tm.consts
//   tm.isdigit <c> | tm.alldigits <s> | tm.clamp <n>
//   tm.roundmin <dec> | tm.roundh <dec> [places] | tm.hm <dec> | tm.dec <dec> [places]
//   tm.words <dec> | tm.factor <dec> | tm.parse <entry>                       -> <ok 1/0> <minutes>
//   cv.filter <field> <text> | cv.filterfield <field> <text>
//   cv.getmin <field> <text>                                                  -> <ok 1/0> <minutes>
//   cv.format <field> <dec> [places] | cv.norm <field> <text> [places]
//   cv.compute <field> <text> [places]                                        -> <other> <caption>
//   cv.convert <field> <text> <places>                                        -> <norm> <other> <caption>
//   cv.all <text> <k>              every converter function on one text, for both fields (bulk test):
//        places = 2 + k mod 5; the overloads without places are used when (k / 5) mod 4 = 0.
//        -> per field, fields joined by '|': Filter|FilterField|ok|minutes|Normalize|Compute other|
//           Compute caption|Convert normalized|Convert other|Convert caption  (TryGetMinutes runs
//           on the FilterField text); the 10 values of field 0 are followed by the 10 of field 1.
//   dec.add|sub|mul|div <a> <b> | dec.cmp <a> <b> | dec.round <a> <n> | dec.rounde <a> <n>
//   dec.trunc <a> | dec.abs <a> | dec.neg <a> | dec.int64 <a> | dec.parse <a> | dec.iszero <a> | dec.isneg <a>
//   ce.run <places|-> <op>...      a new CalcEngine (- = default constructor), then the ops in order:
//        k:<key> Press   p:<text> Paste   d:<n> DecimalPlaces = n
//        -> one record per op, then a final record. Record = fields joined by '|', records by ' '.
//        op record:    Expression|MainText|DecimalText|Caption|IsError|HasEntry|DecimalPlaces|PasteResult
//                      (PasteResult is 1/0 after a p: op and - after the others)
//        final record: CopyMainText|CopyDecimalText|IsEnabled of every key in kAllKeys as 1/0 digits
//   st.parse <text>                 decimal places of an ini text            -> <n>
//   st.parsetheme <text>            theme of an ini text: 0 auto, 1 light, 2 dark   -> <t>
//   st.load <path> | st.loadtheme <path> | st.loadall <path>   (one read: -> <n> <t>)
//   st.themestr <t>                 the value written for a theme (any number: out of range = auto)
//   st.save <path> <n> <t>          the combined save: DecimalPlaces=<n> CR LF Theme=<value> CR LF   -> 1/0
//   st.saveplaces <path> <n> <t>    a changed place count: the file is read again for the theme, <t> is used only when there
//                                   is no file; nothing is written when the file is there but cannot be read   -> 1/0
//   st.savetheme <path> <t> <n>     a changed theme, the same way (<n> is the place count used when there is no file)  -> 1/0
//   st.defpath | st.file
//
// Test support, Linux only (the differential tests use them when they drive the Linux hc_cli from Windows, where the files of
// the program under test live on the other side of a process boundary). Paths are ASCII. Bytes travel as hexadecimal text.
//   fs.mktemp                      a new folder under /tmp, named HourCalcTest-XXXXXX   -> its path
//   fs.rmrf <path>                 removes a file or a folder with everything in it      -> 1/0
//   fs.write <path> <hex>          (re)writes a file, <hex> = \e for an empty file        -> 1/0
//   fs.read <path>                 the bytes of a file as hex (\e = empty), - when there is no such file
//   fs.exists <path> | fs.isdir <path>                                                  -> 1/0
//   fs.del <path>                  removes one file                                      -> 1/0
//   fs.count <folder> <suffix>     the number of entries in a folder whose name ends in <suffix>
//   fs.chmod <path> <octal>                                                              -> 1/0
//   env.set <name> <value>         sets (or, with \e, removes) an environment variable   -> ok

#include "../core/hc_core.h"

#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

using namespace hc;

namespace {

struct BadArgs {};

// ------------------------------------------------------------------ escaping

std::string Esc(const std::wstring& s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string o;
    o.reserve(s.size() + 8);
    for (size_t i = 0; i < s.size(); ++i) {
        wchar_t c = s[i];
        switch (c) {
        case L'\\': o += "\\\\"; break;
        case L' ': o += "\\s"; break;
        case L'\n': o += "\\n"; break;
        case L'\r': o += "\\r"; break;
        case L'\t': o += "\\t"; break;
        case L'|': o += "\\p"; break;
        default:
            if (c < 0x20 || c > 0x7E) {
                o += "\\u";
                o += hex[(c >> 12) & 15];
                o += hex[(c >> 8) & 15];
                o += hex[(c >> 4) & 15];
                o += hex[c & 15];
            } else {
                o += static_cast<char>(c);
            }
        }
    }
    return o;
}

std::string Tok(const std::wstring& s)
{
    return s.empty() ? std::string("\\e") : Esc(s);
}

int HexVal(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    throw BadArgs();
}

std::wstring Unesc(const char* b, const char* e)
{
    std::wstring o;
    if (e - b == 2 && b[0] == '\\' && b[1] == 'e') return o;
    while (b < e) {
        char c = *b++;
        if (c != '\\') {
            o += static_cast<wchar_t>(static_cast<unsigned char>(c));
            continue;
        }
        if (b >= e) throw BadArgs();
        char k = *b++;
        switch (k) {
        case '\\': o += L'\\'; break;
        case 's': o += L' '; break;
        case 'n': o += L'\n'; break;
        case 'r': o += L'\r'; break;
        case 't': o += L'\t'; break;
        case 'p': o += L'|'; break;
        case 'u': {
            if (e - b < 4) throw BadArgs();
            int v = (HexVal(b[0]) << 12) | (HexVal(b[1]) << 8) | (HexVal(b[2]) << 4) | HexVal(b[3]);
            b += 4;
            o += static_cast<wchar_t>(v);
            break;
        }
        default: throw BadArgs();
        }
    }
    return o;
}

// ------------------------------------------------------------------ helpers

int ToInt(const std::wstring& s)
{
    size_t i = 0;
    bool neg = false;
    if (i < s.size() && s[i] == L'-') { neg = true; ++i; }
    if (i >= s.size()) throw BadArgs();
    long long v = 0;
    for (; i < s.size(); ++i) {
        if (s[i] < L'0' || s[i] > L'9') throw BadArgs();
        v = v * 10 + (s[i] - L'0');
        if (v > 1000000000ll) v = 1000000000ll;
    }
    return static_cast<int>(neg ? -v : v);
}

std::string Bool(bool b) { return b ? "1" : "0"; }

std::string D(const Dec& d) { return Esc(d.ToInvariant()); }

// Every key whose IsEnabled the final record reports (the same list is in tests\Differential.cs).
const wchar_t* const kAllKeys[] = {
    L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L":", L",", L".", L"u", L"U", L"h", L"H",
    L"+", L"-", L"*", L"/", L"=", L"CE", L"C", L"BACK", L"Back", L"BACKSPACE", L"ce", L"c", L"x", L"", L"Ce", L"bAck"
};

std::string State(const CalcEngine& e, const char* pasteResult)
{
    std::string s = Esc(e.Expression());
    s += '|'; s += Esc(e.MainText());
    s += '|'; s += Esc(e.DecimalText());
    s += '|'; s += Esc(e.Caption());
    s += '|'; s += Bool(e.IsError());
    s += '|'; s += Bool(e.HasEntry());
    s += '|'; s += std::to_string(e.DecimalPlaces());
    s += '|'; s += pasteResult;
    return s;
}

std::string FinalState(const CalcEngine& e)
{
    std::string s = Esc(e.CopyMainText());
    s += '|'; s += Esc(e.CopyDecimalText());
    s += '|';
    for (size_t i = 0; i < sizeof(kAllKeys) / sizeof(kAllKeys[0]); ++i) s += Bool(e.IsEnabled(kAllKeys[i]));
    return s;
}

std::string ConvAll(const std::wstring& text, int k)
{
    int p = 2 + k % 5;
    bool plain = (k / 5) % 4 == 0;
    std::string o;
    for (int f = 0; f < 2; ++f) {
        if (f != 0) o += '|';
        std::wstring ff = converter::FilterField(f, text);
        Dec m;
        bool ok = converter::TryGetMinutes(f, ff, m);
        std::wstring norm, other, caption, cnorm, cother, ccaption;
        if (plain) {
            norm = converter::Normalize(f, text);
            converter::Compute(f, text, other, caption);
        } else {
            norm = converter::Normalize(f, text, p);
            converter::Compute(f, text, p, other, caption);
        }
        converter::Convert(f, text, p, cnorm, cother, ccaption);
        o += Esc(converter::Filter(f, text)); o += '|';
        o += Esc(ff); o += '|';
        o += Bool(ok); o += '|';
        o += D(m); o += '|';
        o += Esc(norm); o += '|';
        o += Esc(other); o += '|';
        o += Esc(caption); o += '|';
        o += Esc(cnorm); o += '|';
        o += Esc(cother); o += '|';
        o += Esc(ccaption);
    }
    return o;
}

std::string Run(const std::vector<std::wstring>& a)
{
    if (a.size() < 2) throw BadArgs();
    CalcEngine e = a[1] == L"-" ? CalcEngine() : CalcEngine(ToInt(a[1]));
    std::string out;
    for (size_t i = 2; i < a.size(); ++i) {
        const std::wstring& op = a[i];
        if (op.size() < 2 || op[1] != L':') throw BadArgs();
        const char* paste = "-";
        std::wstring arg = op.substr(2);
        switch (op[0]) {
        case L'k': e.Press(arg); break;
        case L'p': paste = e.Paste(arg) ? "1" : "0"; break;
        case L'd': e.SetDecimalPlaces(ToInt(arg)); break;
        default: throw BadArgs();
        }
        out += State(e, paste);
        out += ' ';
    }
    out += FinalState(e);
    return out;
}


// ------------------------------------------------------------------ test support (Linux)

#ifndef _WIN32

// A path or name from the line: ASCII only (anything else is refused).
bool AsciiPath(const std::wstring& w, std::string& out)
{
    out.clear();
    for (size_t i = 0; i < w.size(); ++i) {
        if (w[i] <= 0 || w[i] > 0x7E) return false;
        out += static_cast<char>(w[i]);
    }
    return !out.empty();
}

std::string Hex(const std::string& bytes)
{
    static const char hex[] = "0123456789abcdef";
    std::string o;
    for (size_t i = 0; i < bytes.size(); ++i) {
        unsigned char c = static_cast<unsigned char>(bytes[i]);
        o += hex[c >> 4];
        o += hex[c & 15];
    }
    return o.empty() ? std::string("\\e") : o;
}

std::string FromHex(const std::wstring& w)
{
    std::string o;
    if (w.empty()) return o;
    if (w.size() % 2 != 0) throw BadArgs();
    for (size_t i = 0; i < w.size(); i += 2) {
        if (w[i] > 0x7E || w[i + 1] > 0x7E) throw BadArgs();
        o += static_cast<char>((HexVal(static_cast<char>(w[i])) << 4) | HexVal(static_cast<char>(w[i + 1])));
    }
    return o;
}

std::string FsCommand(const std::wstring& c, const std::vector<std::wstring>& a)
{
    size_t n = a.size();
    std::string p;
    if (c == L"fs.mktemp") {
        if (n != 1) throw BadArgs();
        char tmpl[] = "/tmp/HourCalcTest-XXXXXX";
        if (!mkdtemp(tmpl)) return "!EXC";
        return tmpl;
    }
    if (c == L"fs.rmrf") {
        if (n != 2 || !AsciiPath(a[1], p)) throw BadArgs();
        std::error_code ec;
        std::filesystem::permissions(p, std::filesystem::perms::owner_all, std::filesystem::perm_options::add, ec);
        std::filesystem::remove_all(p, ec);
        return Bool(!ec);
    }
    if (c == L"fs.write") {
        if (n != 3 || !AsciiPath(a[1], p)) throw BadArgs();
        std::string bytes = FromHex(a[2]);
        int fd = open(p.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0666);
        if (fd < 0) return "0";
        bool ok = true;
        for (size_t done = 0; ok && done < bytes.size();) {
            ssize_t put = write(fd, bytes.data() + done, bytes.size() - done);
            if (put < 0) ok = false; else done += static_cast<size_t>(put);
        }
        if (close(fd) != 0) ok = false;
        return Bool(ok);
    }
    if (c == L"fs.read") {
        if (n != 2 || !AsciiPath(a[1], p)) throw BadArgs();
        int fd = open(p.c_str(), O_RDONLY | O_CLOEXEC);
        if (fd < 0) return "-";
        std::string bytes;
        char buf[4096];
        ssize_t got;
        while ((got = read(fd, buf, sizeof(buf))) > 0) bytes.append(buf, static_cast<size_t>(got));
        close(fd);
        return got < 0 ? std::string("-") : Hex(bytes);
    }
    if (c == L"fs.exists" || c == L"fs.isdir") {
        if (n != 2 || !AsciiPath(a[1], p)) throw BadArgs();
        struct stat st;
        bool ok = lstat(p.c_str(), &st) == 0;
        if (c == L"fs.isdir") ok = ok && S_ISDIR(st.st_mode);
        return Bool(ok);
    }
    if (c == L"fs.del") {
        if (n != 2 || !AsciiPath(a[1], p)) throw BadArgs();
        return Bool(unlink(p.c_str()) == 0);
    }
    if (c == L"fs.count") {
        std::string suffix;
        if (n != 3 || !AsciiPath(a[1], p) || !AsciiPath(a[2], suffix)) throw BadArgs();
        int count = 0;
        if (DIR* d = opendir(p.c_str())) {
            while (struct dirent* e = readdir(d)) {
                std::string name = e->d_name;
                if (name.size() >= suffix.size() && name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0) ++count;
            }
            closedir(d);
        }
        return std::to_string(count);
    }
    if (c == L"fs.chmod") {
        if (n != 3 || !AsciiPath(a[1], p)) throw BadArgs();
        return Bool(chmod(p.c_str(), static_cast<mode_t>(std::strtol(std::string(a[2].begin(), a[2].end()).c_str(), nullptr, 8))) == 0);
    }
    if (c == L"env.set") {
        std::string name, value;
        if (n != 3 || !AsciiPath(a[1], name)) throw BadArgs();
        if (a[2].empty()) { unsetenv(name.c_str()); return "ok"; }
        if (!AsciiPath(a[2], value)) throw BadArgs();
        setenv(name.c_str(), value.c_str(), 1);
        return "ok";
    }
    return "!UNKNOWN";
}

#endif

// ------------------------------------------------------------------ dispatch

std::string Execute(const std::vector<std::wstring>& a)
{
    const std::wstring& c = a[0];
    size_t n = a.size();
    auto need = [&](size_t min, size_t max) { if (n < min || n > max) throw BadArgs(); };

    if (c == L"ping") return "pong";
#ifdef _WIN32
    if (c == L"os") return "windows";
#else
    if (c == L"os") return "linux";
#endif
    if (c == L"sep") { need(2, 2); timemath::SetDecimalSeparatorOverride(a[1]); return "ok"; }

    // ---- TimeMath
    if (c == L"tm.sep") return Tok(timemath::DecimalSeparator());
    if (c == L"tm.limit") return D(timemath::LimitMinutes());
    if (c == L"tm.consts") {
        return std::to_string(timemath::MaxHourDigits) + " " + std::to_string(timemath::MaxMinuteDigits) + " " +
               std::to_string(timemath::MaxDecimals) + " " + std::to_string(timemath::DefaultDecimals) + " " +
               std::to_string(timemath::MinDisplayDecimals) + " " + std::to_string(timemath::MaxDisplayDecimals);
    }
    if (c == L"tm.isdigit") { need(2, 2); if (a[1].size() != 1) throw BadArgs(); return Bool(timemath::IsDigit(a[1][0])); }
    if (c == L"tm.alldigits") { need(2, 2); return Bool(timemath::AllDigits(a[1])); }
    if (c == L"tm.clamp") { need(2, 2); return std::to_string(timemath::ClampDecimals(ToInt(a[1]))); }
    if (c == L"tm.roundmin") { need(2, 2); return D(timemath::RoundMinutes(Dec::Parse(a[1]))); }
    if (c == L"tm.roundh") {
        need(2, 3);
        return n == 2 ? D(timemath::RoundHours(Dec::Parse(a[1]))) : D(timemath::RoundHours(Dec::Parse(a[1]), ToInt(a[2])));
    }
    if (c == L"tm.hm") { need(2, 2); return Tok(timemath::FormatHm(Dec::Parse(a[1]))); }
    if (c == L"tm.dec") {
        need(2, 3);
        return Tok(n == 2 ? timemath::FormatDecimal(Dec::Parse(a[1])) : timemath::FormatDecimal(Dec::Parse(a[1]), ToInt(a[2])));
    }
    if (c == L"tm.words") { need(2, 2); return Tok(timemath::FormatWords(Dec::Parse(a[1]))); }
    if (c == L"tm.factor") { need(2, 2); return Tok(timemath::FormatFactor(Dec::Parse(a[1]))); }
    if (c == L"tm.parse") {
        need(2, 2);
        Dec m;
        bool ok = timemath::TryParseEntry(a[1], m);
        return Bool(ok) + " " + D(m);
    }

    // ---- Converter
    if (c == L"cv.filter") { need(3, 3); return Tok(converter::Filter(ToInt(a[1]), a[2])); }
    if (c == L"cv.filterfield") { need(3, 3); return Tok(converter::FilterField(ToInt(a[1]), a[2])); }
    if (c == L"cv.getmin") {
        need(3, 3);
        Dec m;
        bool ok = converter::TryGetMinutes(ToInt(a[1]), a[2], m);
        return Bool(ok) + " " + D(m);
    }
    if (c == L"cv.format") {
        need(3, 4);
        return Tok(n == 3 ? converter::Format(ToInt(a[1]), Dec::Parse(a[2])) : converter::Format(ToInt(a[1]), Dec::Parse(a[2]), ToInt(a[3])));
    }
    if (c == L"cv.norm") {
        need(3, 4);
        return Tok(n == 3 ? converter::Normalize(ToInt(a[1]), a[2]) : converter::Normalize(ToInt(a[1]), a[2], ToInt(a[3])));
    }
    if (c == L"cv.compute") {
        need(3, 4);
        std::wstring other, caption;
        if (n == 3) converter::Compute(ToInt(a[1]), a[2], other, caption);
        else converter::Compute(ToInt(a[1]), a[2], ToInt(a[3]), other, caption);
        return Tok(other) + " " + Tok(caption);
    }
    if (c == L"cv.convert") {
        need(4, 4);
        std::wstring norm, other, caption;
        converter::Convert(ToInt(a[1]), a[2], ToInt(a[3]), norm, other, caption);
        return Tok(norm) + " " + Tok(other) + " " + Tok(caption);
    }

    if (c == L"cv.all") { need(3, 3); return ConvAll(a[1], ToInt(a[2])); }

    // ---- Dec
    if (c == L"dec.add") { need(3, 3); return D(Dec::Parse(a[1]) + Dec::Parse(a[2])); }
    if (c == L"dec.sub") { need(3, 3); return D(Dec::Parse(a[1]) - Dec::Parse(a[2])); }
    if (c == L"dec.mul") { need(3, 3); return D(Dec::Parse(a[1]) * Dec::Parse(a[2])); }
    if (c == L"dec.div") { need(3, 3); return D(Dec::Parse(a[1]) / Dec::Parse(a[2])); }
    if (c == L"dec.cmp") {
        need(3, 3);
        Dec x = Dec::Parse(a[1]), y = Dec::Parse(a[2]);
        // The six comparison operators as one digit string: == != < <= > >=
        return Bool(x == y) + Bool(x != y) + Bool(x < y) + Bool(x <= y) + Bool(x > y) + Bool(x >= y);
    }
    if (c == L"dec.round") { need(3, 3); return D(Dec::Parse(a[1]).RoundAwayFromZero(ToInt(a[2]))); }
    if (c == L"dec.rounde") { need(3, 3); return D(Dec::Parse(a[1]).RoundToEven(ToInt(a[2]))); }
    if (c == L"dec.trunc") { need(2, 2); return D(Dec::Parse(a[1]).Truncate()); }
    if (c == L"dec.abs") { need(2, 2); return D(Dec::Parse(a[1]).Abs()); }
    if (c == L"dec.neg") { need(2, 2); return D(-Dec::Parse(a[1])); }
    if (c == L"dec.int64") { need(2, 2); return std::to_string(Dec::Parse(a[1]).ToInt64()); }
    if (c == L"dec.parse") { need(2, 2); return D(Dec::Parse(a[1])); }
    if (c == L"dec.iszero") { need(2, 2); return Bool(Dec::Parse(a[1]).IsZero()); }
    if (c == L"dec.isneg") { need(2, 2); return Bool(Dec::Parse(a[1]).IsNegative()); }

    // ---- CalcEngine
    if (c == L"ce.run") return Run(a);

    // ---- Settings
    if (c == L"st.parse") { need(2, 2); return std::to_string(settings::ParseDecimalPlaces(a[1])); }
    if (c == L"st.parsetheme") { need(2, 2); return std::to_string(static_cast<int>(settings::ParseTheme(a[1]))); }
    if (c == L"st.load") { need(2, 2); return std::to_string(settings::LoadDecimalPlaces(a[1])); }
    if (c == L"st.loadtheme") { need(2, 2); return std::to_string(static_cast<int>(settings::LoadTheme(a[1]))); }
    if (c == L"st.loadall") {
        need(2, 2);
        settings::Values v = settings::Load(a[1]);
        return std::to_string(v.decimalPlaces) + " " + std::to_string(static_cast<int>(v.theme));
    }
    if (c == L"st.themestr") { need(2, 2); return Tok(settings::ThemeToString(static_cast<settings::ThemeMode>(ToInt(a[1])))); }
    if (c == L"st.save") { need(4, 4); return Bool(settings::Save(a[1], ToInt(a[2]), static_cast<settings::ThemeMode>(ToInt(a[3])))); }
    if (c == L"st.saveplaces") { need(4, 4); return Bool(settings::SavePlaces(a[1], ToInt(a[2]), static_cast<settings::ThemeMode>(ToInt(a[3])))); }
    if (c == L"st.savetheme") { need(4, 4); return Bool(settings::SaveTheme(a[1], static_cast<settings::ThemeMode>(ToInt(a[2])), ToInt(a[3]))); }
    if (c == L"st.defpath") return Tok(settings::DefaultPath());
    if (c == L"st.file") return Tok(settings::FileName);

#ifndef _WIN32
    if (c.compare(0, 3, L"fs.") == 0 || c.compare(0, 4, L"env.") == 0) return FsCommand(c, a);
#endif

    return "!UNKNOWN";
}

std::string HandleLine(const char* b, const char* e)
{
    std::vector<std::wstring> args;
    try {
        const char* p = b;
        while (p < e) {
            while (p < e && *p == ' ') ++p;
            if (p >= e) break;
            const char* q = p;
            while (q < e && *q != ' ') ++q;
            args.push_back(Unesc(p, q));
            p = q;
        }
        if (args.empty()) return "!ARGS";
        return Execute(args);
    } catch (const BadArgs&) {
        return "!ARGS";
    } catch (const DecOverflow&) {
        return "!OVERFLOW";
    } catch (const DecDivideByZero&) {
        return "!DIVZERO";
    } catch (const DecFormat&) {
        return "!FORMAT";
    } catch (...) {
        return "!EXC";
    }
}

// Raw standard input and output (no text mode, no buffering of their own): the protocol is bytes.
#ifdef _WIN32

struct StdIo {
    HANDLE in;
    HANDLE out;
    StdIo() : in(GetStdHandle(STD_INPUT_HANDLE)), out(GetStdHandle(STD_OUTPUT_HANDLE)) {}
    long Read(char* buf, size_t n)
    {
        DWORD got = 0;
        if (!ReadFile(in, buf, static_cast<DWORD>(n), &got, nullptr)) return -1;
        return static_cast<long>(got);
    }
    void Write(const char* data, size_t n)
    {
        size_t done = 0;
        while (done < n) {
            DWORD chunk = static_cast<DWORD>(n - done > (1u << 20) ? (1u << 20) : n - done);
            DWORD written = 0;
            if (!WriteFile(out, data + done, chunk, &written, nullptr) || written == 0) ExitProcess(2);
            done += written;
        }
    }
};

#else

struct StdIo {
    long Read(char* buf, size_t n)
    {
        for (;;) {
            ssize_t got = read(0, buf, n);
            if (got < 0 && errno == EINTR) continue;
            return static_cast<long>(got);
        }
    }
    void Write(const char* data, size_t n)
    {
        size_t done = 0;
        while (done < n) {
            ssize_t put = write(1, data + done, n - done);
            if (put < 0 && errno == EINTR) continue;
            if (put <= 0) _exit(2);
            done += static_cast<size_t>(put);
        }
    }
};

#endif

} // namespace

int main()
{
    StdIo io;
    std::string pending;      // bytes read, not yet a whole line
    std::string output;       // answers not yet written
    std::vector<char> buf(1 << 16);
    bool bomChecked = false;
    for (;;) {
        long got = io.Read(buf.data(), buf.size());
        if (got <= 0) break;
        pending.append(buf.data(), static_cast<size_t>(got));
        // The C# test drivers write their requests through Process.StandardInput, which puts a UTF-8 byte order mark in front
        // of the first request when the console code page is 65001 (UTF-8): that request would answer !UNKNOWN. Ignore it.
        if (!bomChecked && pending.size() >= 3) {
            bomChecked = true;
            if (pending.compare(0, 3, "\xEF\xBB\xBF") == 0) pending.erase(0, 3);
        }
        size_t start = 0;
        for (;;) {
            size_t nl = pending.find('\n', start);
            if (nl == std::string::npos) break;
            size_t end = nl;
            if (end > start && pending[end - 1] == '\r') --end;
            output += HandleLine(pending.data() + start, pending.data() + end);
            output += '\n';
            start = nl + 1;
            if (output.size() > (1u << 20)) { io.Write(output.data(), output.size()); output.clear(); }
        }
        pending.erase(0, start);
        if (!output.empty()) { io.Write(output.data(), output.size()); output.clear(); }
    }
    if (!pending.empty()) {          // a last line without a newline
        size_t end = pending.size();
        if (pending[end - 1] == '\r') --end;
        output += HandleLine(pending.data(), pending.data() + end);
        output += '\n';
    }
    if (!output.empty()) io.Write(output.data(), output.size());
    return 0;
}
