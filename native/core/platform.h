// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// The few things the core asks of the operating system, implemented once per platform: platform_win.cpp (Win32) and
// platform_posix.cpp (Linux and other POSIX systems). Not part of the contract with the UI (hc_core.h is).

#pragma once

#include <cstddef>
#include <string>

namespace hc {
namespace platform {

// The decimal separator of the user's locale; "." when the system does not say.
std::wstring UserDecimalSeparator();

// Where the settings file lives (settings::DefaultPath); "" when there is no folder for it.
std::wstring SettingsPath();

// How reading a file went: Read (the bytes are in the buffer), Absent (there is no such file, or no such folder: nothing to
// lose), or Failed (something is there that cannot be read: locked, no access, a folder, too big).
enum class ReadResult { Read, Absent, Failed };
ReadResult ReadFileBytes(const std::wstring& path, std::string& bytes);

// Writes the bytes to a file of their own next to 'path' (the folder of 'path' is made first, one level), which then replaces
// 'path' in one step: a reader sees the old file or the new one, never half of it. False (and no file left over) when that cannot
// be done: an empty path, a path that is a folder, a file that is marked read-only (Windows: the attribute; Linux: no write
// permission bit at all), a folder that does not exist and cannot be made, no permission.
bool WriteFileAtomic(const std::wstring& path, const char* data, size_t size);

// UTF-8 (the whole of 'n' bytes, no byte order mark) to the UTF-16 code units of the reference runtime, one per wchar_t;
// a bad byte becomes U+FFFD and the byte after it is looked at again.
std::wstring DecodeUtf8(const char* bytes, size_t n);

// string.Equals(s, word, StringComparison.OrdinalIgnoreCase) for a word of ASCII letters.
bool EqualsIgnoreCase(const std::wstring& s, const wchar_t* word);

} // namespace platform
} // namespace hc
