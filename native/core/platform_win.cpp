// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// The Windows side of platform.h. Only the Win32 file API is used (no CRT file I/O) and no exception ever leaves this file.

#ifdef _WIN32

#include "hc_core.h"
#include "platform.h"

#include <windows.h>
#include <cwchar>

namespace hc {
namespace platform {

std::wstring UserDecimalSeparator()
{
    wchar_t buf[16];
    int n = GetLocaleInfoEx(LOCALE_NAME_USER_DEFAULT, LOCALE_SDECIMAL, buf, 16);
    if (n <= 1) return std::wstring(L".");
    return std::wstring(buf, static_cast<size_t>(n - 1));
}

// The folder of the user's local application data (no trailing slash): the environment variable LOCALAPPDATA, which Windows sets for
// every session, when it holds an absolute path ("X:\..." or "\\..."); "" otherwise, and then the settings only last for the
// session. SHGetKnownFolderPath (SPEC 6c) gives the same folder but not for free: the first call of a process loads the shell storage
// DLLs (measured on the owner's PC with the DLLs of this app already loaded: 22 ms and thirteen more modules, one of them a
// third-party shell extension), and shell32 and ole32 would come with it. A fallback for an environment that was cleaned out
// (the value that the shell keeps in the registry) cost 250 bytes of an exe that is held to 200,000.
std::wstring SettingsPath()
{
    wchar_t buf[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", buf, MAX_PATH);
    if (n < 3 || n >= MAX_PATH || !((buf[1] == L':' && buf[2] == L'\\') || (buf[0] == L'\\' && buf[1] == L'\\'))) return std::wstring();
    std::wstring dir(buf, n);
    while (!dir.empty() && dir.back() == L'\\') dir.pop_back();
    if (dir.empty()) return dir;                      // slashes only
    return dir + L"\\" + settings::FolderName + L"\\" + settings::FileName;
}

ReadResult ReadFileBytes(const std::wstring& path, std::string& bytes)
{
    bytes.clear();
    if (path.empty()) return ReadResult::Absent;
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        return err == ERROR_FILE_NOT_FOUND || err == ERROR_PATH_NOT_FOUND ? ReadResult::Absent : ReadResult::Failed;
    }
    char buf[4096];
    bool ok = true;
    for (;;) {
        DWORD got = 0;
        if (!ReadFile(h, buf, sizeof(buf), &got, nullptr)) { ok = false; break; }
        if (got == 0) break;
        bytes.append(buf, got);
        if (bytes.size() > (64u << 20)) { ok = false; break; }
    }
    CloseHandle(h);
    if (!ok) { bytes.clear(); return ReadResult::Failed; }
    return ReadResult::Read;
}

bool WriteFileAtomic(const std::wstring& path, const char* text, size_t n)
{
    if (path.empty()) return false;
    // The first save makes the folder (one level: its parent must exist; an existing folder is no error).
    size_t slash = path.rfind(L'\\');
    if (slash != std::wstring::npos && slash > 0) CreateDirectoryW(path.substr(0, slash).c_str(), nullptr);
    // The new contents are written to a file of their own next to the settings file (named with the low 16 bits of the id of this
    // process: no other running program is likely to use that name, and a leftover of an earlier crash is simply overwritten) and are on the disk
    // when WriteFile returns (write through), and that file replaces the settings file in one step: a reader sees the old file or the
    // new one, never an empty or half written file, also when this process is killed in the middle of a save (a leftover *.tmp is
    // all there is).
    std::wstring tmp = path + L".";
    for (DWORD id = GetCurrentProcessId(), i = 0; i < 4; ++i, id >>= 4) tmp += static_cast<wchar_t>(L'a' + (id & 15));
    tmp += L".tmp";
    HANDLE h = CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH, nullptr);
    if (h == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(h, text, static_cast<DWORD>(n), &written, nullptr) && written == n;
    if (!CloseHandle(h)) ok = false;
    // A reader of the old file (a second HourCalc that is starting, a virus scanner) can keep the rename from working for a
    // moment: try again a few times. (A read-only file, which the user means to stay as it is, and a folder never work: they
    // fail after the last try, half a second later. 160 ms was not enough on a machine with a real-time scanner: about one save in
    // seventy of a plain loop, a save right after a read of the file, was still held after that.)
    for (int tries = 0; ok && tries < 25; ++tries) {
        if (MoveFileExW(tmp.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING)) return true;
        Sleep(20);
    }
    DeleteFileW(tmp.c_str());
    return false;
}

std::wstring DecodeUtf8(const char* bytes, size_t n)
{
    std::wstring out;
    int len = MultiByteToWideChar(CP_UTF8, 0, bytes, static_cast<int>(n), nullptr, 0);
    if (len <= 0) return out;
    out.resize(static_cast<size_t>(len));
    MultiByteToWideChar(CP_UTF8, 0, bytes, static_cast<int>(n), &out[0], len);
    return out;
}

bool EqualsIgnoreCase(const std::wstring& s, const wchar_t* word)
{
    int len = static_cast<int>(wcslen(word));
    if (static_cast<int>(s.size()) != len) return false;
    return CompareStringOrdinal(s.data(), len, word, len, TRUE) == CSTR_EQUAL;
}

} // namespace platform
} // namespace hc

#endif // _WIN32
