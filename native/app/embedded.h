// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the legal notices and the resource the exe carries: its own source code as a zip
// that also holds the license text, so a portable exe that is copied around stays compliant
// with the GPL. build-app.ps1 embeds the zip (RCDATA 101). With HC_NO_EMBEDDED_SOURCE
// (build-app.ps1 -NoEmbeddedSource) the exe carries only the license text, deflated (RCDATA 102), and
// shows the address of the source code (kSourceUrl).

#pragma once

#include <windows.h>
#include <stdint.h>
#include <string>
#include <vector>

namespace embedded {

extern const wchar_t* const kProductName;     // L"HourCalc"
extern const wchar_t* const kCopyright;       // L"Copyright (c) 2026 Optimey CommV"
extern const wchar_t* const kNotice;          // the GPL section 0/5d notice
extern const wchar_t* const kVersion;         // L"1.0.0"
#ifdef HC_NO_EMBEDDED_SOURCE
extern const wchar_t* const kSourceUrl;       // where the source code of this version is published
#endif
std::wstring SourceFileName();                // HourCalc-1.0.0-source.zip

// The embedded source archive, or false when this build does not carry it (always false with HC_NO_EMBEDDED_SOURCE).
bool OpenSource(const uint8_t** data, size_t* size);
// The full license text (GPL-3.0-or-later, UTF-8 decoded) from the embedded archive (HC_NO_EMBEDDED_SOURCE: from the deflated resource).
bool ReadLicense(std::wstring& text);
// Copies the embedded source archive to a file.
bool SaveSource(const wchar_t* path);

// Zip reading (stored and deflated entries) and the inflate it needs.
bool ZipNames(const uint8_t* zip, size_t size, std::vector<std::string>& names);
bool ZipRead(const uint8_t* zip, size_t size, const char* name, std::vector<uint8_t>& out);
bool Inflate(const uint8_t* in, size_t inLen, uint8_t* out, size_t outLen);

}  // namespace embedded
