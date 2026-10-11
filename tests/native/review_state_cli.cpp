// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Review tool, not part of the product: a console front end like native\cli\hc_cli.cpp, but one that also
// reports the PRIVATE state of hc::CalcEngine after every operation (the unrounded accumulator with its
// scale, the pending operator, the repeat state, the entry text). tests\DifferentialReview.cs --state
// compares that with the private fields of the C# reference (by reflection), which the printed
// outputs of hc_cli cannot show: a change that only moves the 28th digit of the accumulator.
//
// It reads the access to the data members by turning 'private' into 'public' while hc_core.h is
// included; only data members are touched, so the layout is the one of the real class.
//
// Line protocol (same escaping as hc_cli.cpp):
//   ping                                  -> pong
//   sep <str>                             decimal separator override (\e = clear)  -> ok
//   ce.state <places|-> <op>...           ops: k:<key> Press, p:<text> Paste, d:<n> DecimalPlaces = n
//        -> one record per op, then a final record, records joined by ' ', fields by '|':
//        Expression|MainText|DecimalText|Caption|IsError|HasEntry|DecimalPlaces|PasteResult|
//        acc|hasAcc|pendingOp|lastOp|lastOperand|hasLast|justEq|entry|entryActive|error
//        (operators as decimal code points, '-' for no error; the final record has CopyMainText|CopyDecimalText
//        and IsEnabled of every key as 1/0 digits, like hc_cli.exe)

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

#define private public
#include "../../native/core/hc_core.h"
#undef private

using namespace hc;

namespace {

struct BadArgs {};

std::string Esc(const std::wstring& s)
{
    static const char hex[] = "0123456789ABCDEF";
    std::string o;
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
    // the private state
    s += '|'; s += Esc(e.acc_.ToInvariant());
    s += '|'; s += Bool(e.hasAcc_);
    s += '|'; s += std::to_string(static_cast<int>(e.pendingOp_));
    s += '|'; s += std::to_string(static_cast<int>(e.lastOp_));
    s += '|'; s += Esc(e.lastOperand_.ToInvariant());
    s += '|'; s += Bool(e.hasLast_);
    s += '|'; s += Bool(e.justEq_);
    s += '|'; s += e.entry_.empty() ? std::string("\\e") : Esc(e.entry_);
    s += '|'; s += Bool(e.entryActive_);
    s += '|'; s += e.error_ == nullptr ? std::string("-") : Esc(e.error_);
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
        const std::wstring& c = args[0];
        if (c == L"ping") return "pong";
#ifdef _WIN32
        if (c == L"os") return "windows";
#else
        if (c == L"os") return "linux";
#endif
        if (c == L"tm.sep") {
            std::wstring sep = timemath::DecimalSeparator();
            return sep.empty() ? std::string("\\e") : Esc(sep);
        }
        if (c == L"sep") {
            if (args.size() != 2) throw BadArgs();
            timemath::SetDecimalSeparatorOverride(args[1]);
            return "ok";
        }
        if (c == L"ce.state") return Run(args);
        return "!UNKNOWN";
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
