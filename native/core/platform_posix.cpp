// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// The POSIX (Linux) side of platform.h. Plain POSIX calls (open, read, write, rename), no exception ever leaves this file.
// Text crosses this file as UTF-8 on the system side and as std::wstring (UTF-32, one wchar_t per code point) on the core side;
// the arguments of the test front end hc_cli, which hold UTF-16 code units, are accepted too (a surrogate pair is joined).

#ifndef _WIN32

#include "hc_core.h"
#include "platform.h"

#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <mutex>

#include <fcntl.h>
#include <langinfo.h>
#include <locale.h>
#include <pwd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace hc {
namespace platform {

namespace {

// ------------------------------------------------------------------ UTF-8

void AppendUtf8(std::string& out, unsigned long c)
{
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else if (c < 0x800) {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += static_cast<char>(0xE0 | (c >> 12));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (c >> 18));
        out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

std::string ToUtf8(const std::wstring& w)
{
    std::string out;
    out.reserve(w.size());
    for (size_t i = 0; i < w.size(); ++i) {
        unsigned long c = static_cast<uint32_t>(w[i]);
        if (c >= 0xD800 && c <= 0xDBFF && i + 1 < w.size()) {
            unsigned long d = static_cast<uint32_t>(w[i + 1]);
            if (d >= 0xDC00 && d <= 0xDFFF) {
                c = 0x10000 + ((c - 0xD800) << 10) + (d - 0xDC00);
                ++i;
            }
        }
        if (c >= 0xDC80 && c <= 0xDCFF) {           // a byte of a path that was not UTF-8 (see FromUtf8): back to the byte
            out += static_cast<char>(c - 0xDC00);
            continue;
        }
        if (c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) c = 0xFFFD;
        AppendUtf8(out, c);
    }
    return out;
}

// UTF-8 to code points; units16: a code point above U+FFFF becomes a surrogate pair (the reference runtime's view of the text).
// A byte that cannot continue the sequence is not used up: it is looked at again. A byte that is not part of valid UTF-8 becomes
// U+FFFD, or with 'keepBytes' (the names of files: Linux allows any bytes in them) U+DC00 + the byte, which ToUtf8 turns back.
std::wstring FromUtf8(const char* p, size_t n, bool units16, bool keepBytes)
{
    std::wstring out;
    out.reserve(n);
    size_t i = 0;
    while (i < n) {
        size_t start = i;
        unsigned char b = static_cast<unsigned char>(p[i++]);
        unsigned long cp = b;
        int need = 0;
        bool bad = false;
        unsigned char lo = 0x80, hi = 0xBF;
        if (b >= 0xC2 && b <= 0xDF) { need = 1; cp = b & 0x1F; }
        else if (b >= 0xE0 && b <= 0xEF) { need = 2; cp = b & 0x0F; if (b == 0xE0) lo = 0xA0; if (b == 0xED) hi = 0x9F; }
        else if (b >= 0xF0 && b <= 0xF4) { need = 3; cp = b & 0x07; if (b == 0xF0) lo = 0x90; if (b == 0xF4) hi = 0x8F; }
        else if (b >= 0x80) { bad = true; }
        for (int k = 0; !bad && k < need; ++k) {
            if (i >= n || static_cast<unsigned char>(p[i]) < lo || static_cast<unsigned char>(p[i]) > hi) { bad = true; break; }
            cp = (cp << 6) | (static_cast<unsigned char>(p[i++]) & 0x3Fu);
            lo = 0x80;
            hi = 0xBF;
        }
        if (bad) {
            if (keepBytes) for (size_t j = start; j < i; ++j) out += static_cast<wchar_t>(0xDC00 + static_cast<unsigned char>(p[j]));
            else out += static_cast<wchar_t>(0xFFFD);
            continue;
        }
        if (cp > 0xFFFF && units16) {
            out += static_cast<wchar_t>(0xD800 + ((cp - 0x10000) >> 10));
            out += static_cast<wchar_t>(0xDC00 + ((cp - 0x10000) & 0x3FF));
        } else {
            out += static_cast<wchar_t>(cp);
        }
    }
    return out;
}

// ------------------------------------------------------------------ the environment

std::string Env(const char* name)
{
    const char* v = std::getenv(name);
    return v ? std::string(v) : std::string();
}

// $XDG_CONFIG_HOME when it is an absolute path (the specification says to ignore any other value), else $HOME/.config, HOME from the
// environment or the password database; false when there is none. No trailing slash (so "" is the root folder).
bool ConfigHome(std::string& base)
{
    base = Env("XDG_CONFIG_HOME");
    if (base.empty() || base[0] != '/') {
        std::string home = Env("HOME");
        if (home.empty() || home[0] != '/') {
            home.clear();
            struct passwd* pw = getpwuid(getuid());
            if (pw && pw->pw_dir && pw->pw_dir[0] == '/') home = pw->pw_dir;
        }
        if (home.empty()) return false;
        while (!home.empty() && home[home.size() - 1] == '/') home.erase(home.size() - 1);
        base = home + "/.config";
    }
    while (!base.empty() && base[base.size() - 1] == '/') base.erase(base.size() - 1);
    return true;
}

// Makes one folder (an existing one is no error). The base folder of the settings, ~/.config, is made too when it is missing:
// it is the only parent that is ever made, a chain of missing folders stays missing.
void MakeFolder(const std::string& dir)
{
    if (mkdir(dir.c_str(), 0777) == 0 || errno != ENOENT) return;
    size_t slash = dir.rfind('/');
    if (slash == std::string::npos || slash == 0) return;
    std::string parent = dir.substr(0, slash), config;
    if (ConfigHome(config) && parent == config && mkdir(parent.c_str(), 0700) == 0) mkdir(dir.c_str(), 0777);
}

} // namespace

// ------------------------------------------------------------------ platform.h

// The radix character of the numeric locale of the environment, asked from a locale object of its own: the global locale of the
// program is not touched (and is not trusted: Qt sets LC_NUMERIC to "C" after it started). The answer is kept until one of the
// variables that select the locale changes.
std::wstring UserDecimalSeparator()
{
    static std::mutex lock;
    static std::string lastKey;
    static std::wstring lastValue;
    static bool have = false;
    std::string key = Env("LC_ALL") + '\x01' + Env("LC_NUMERIC") + '\x01' + Env("LANG");
    std::lock_guard<std::mutex> guard(lock);
    if (have && key == lastKey) return lastValue;
    std::wstring value = L".";
    locale_t loc = newlocale(LC_NUMERIC_MASK, "", static_cast<locale_t>(0));
    if (loc != static_cast<locale_t>(0)) {
        const char* radix = nl_langinfo_l(RADIXCHAR, loc);
        const char* codeset = nl_langinfo_l(CODESET, loc);
        if (radix && radix[0] != '\0') {
            bool ascii = true;
            for (const char* c = radix; *c; ++c) if (static_cast<unsigned char>(*c) >= 0x80) ascii = false;
            bool utf8 = codeset && (std::strcmp(codeset, "UTF-8") == 0 || std::strcmp(codeset, "utf8") == 0);
            if (ascii || utf8) value = FromUtf8(radix, std::strlen(radix), false, false);
        }
        freelocale(loc);
    }
    lastKey = key;
    lastValue = value;
    have = true;
    return value;
}

std::wstring SettingsPath()
{
    std::string path;
    if (!ConfigHome(path)) return std::wstring();
    path += "/hourcalc/hourcalc.ini";
    return FromUtf8(path.data(), path.size(), false, true);
}

ReadResult ReadFileBytes(const std::wstring& wpath, std::string& bytes)
{
    bytes.clear();
    if (wpath.empty()) return ReadResult::Absent;
    std::string path = ToUtf8(wpath);
    if (path.find('\0') != std::string::npos) return ReadResult::Absent;
    int fd = open(path.c_str(), O_RDONLY | O_CLOEXEC | O_NONBLOCK);
    if (fd < 0) return errno == ENOENT || errno == ENOTDIR ? ReadResult::Absent : ReadResult::Failed;
    struct stat st;
    if (fstat(fd, &st) != 0 || !S_ISREG(st.st_mode)) {
        close(fd);
        return ReadResult::Failed;
    }
    char buf[4096];
    bool ok = true;
    for (;;) {
        ssize_t got = read(fd, buf, sizeof(buf));
        if (got < 0) {
            if (errno == EINTR) continue;
            ok = false;
            break;
        }
        if (got == 0) break;
        bytes.append(buf, static_cast<size_t>(got));
        if (bytes.size() > (64u << 20)) { ok = false; break; }
    }
    close(fd);
    if (!ok) { bytes.clear(); return ReadResult::Failed; }
    return ReadResult::Read;
}

bool WriteFileAtomic(const std::wstring& wpath, const char* data, size_t size)
{
    if (wpath.empty()) return false;
    std::string path = ToUtf8(wpath);
    if (path.find('\0') != std::string::npos) return false;
    // A symbolic link to the file (dotfiles managers make them) is followed: the file behind it is replaced, the link stays.
    char* real = realpath(path.c_str(), nullptr);
    if (real) {
        path = real;
        std::free(real);
    }
    mode_t mode = 0666;                        // minus the umask, for a new file
    struct stat st;
    bool existed = stat(path.c_str(), &st) == 0;
    if (existed) {
        if (S_ISDIR(st.st_mode)) return false;
        if ((st.st_mode & 0222) == 0) return false;     // read-only is the user's mark: leave the file alone
        mode = st.st_mode & 07777;
    }
    size_t slash = path.rfind('/');
    if (slash != std::string::npos && slash > 0) MakeFolder(path.substr(0, slash));
    // A file of its own next to it, named with the low 16 bits of the process id (a leftover of a crash is replaced), written to
    // the disk, then renamed over the settings file: a reader sees the old file or the new one, never half of it.
    std::string tmp = path + ".";
    for (unsigned id = static_cast<unsigned>(getpid()), i = 0; i < 4; ++i, id >>= 4) tmp += static_cast<char>('a' + (id & 15));
    tmp += ".tmp";
    unlink(tmp.c_str());
    int fd = open(tmp.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, mode);
    if (fd < 0) return false;
    bool ok = true;
    for (size_t done = 0; ok && done < size;) {
        ssize_t put = write(fd, data + done, size - done);
        if (put < 0) {
            if (errno == EINTR) continue;
            ok = false;
        } else {
            done += static_cast<size_t>(put);
        }
    }
    if (ok && existed && fchmod(fd, mode) != 0) ok = false;
    if (ok && fsync(fd) != 0) ok = false;
    if (close(fd) != 0) ok = false;
    if (ok && rename(tmp.c_str(), path.c_str()) == 0) return true;
    unlink(tmp.c_str());
    return false;
}

std::wstring DecodeUtf8(const char* bytes, size_t n)
{
    return FromUtf8(bytes, n, true, false);
}

bool EqualsIgnoreCase(const std::wstring& s, const wchar_t* word)
{
    size_t i = 0;
    for (; word[i] != L'\0'; ++i) {
        if (i >= s.size()) return false;
        wchar_t a = s[i], b = word[i];
        if (a >= L'a' && a <= L'z') a = static_cast<wchar_t>(a - 32);
        if (b >= L'a' && b <= L'z') b = static_cast<wchar_t>(b - 32);
        if (a != b) return false;
    }
    return i == s.size();
}

} // namespace platform
} // namespace hc

#endif // !_WIN32
