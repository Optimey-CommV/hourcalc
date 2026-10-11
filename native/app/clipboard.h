// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - text on the clipboard. Another program (a clipboard manager, the Windows clipboard history, a
// remote desktop or virtual machine sync) can have the clipboard open for a few milliseconds when we want it, and
// then OpenClipboard fails. The .NET Clipboard class of the WPF build tries 10 times 100 ms apart; this does
// the same with a shorter step, so the copy is not lost and the window is not stuck for long.

#pragma once

#include <windows.h>
#include <string>

namespace clip {

// The Win32 calls of this module. The defaults are the real ones; the self test swaps in an in-memory
// clipboard (it must neither touch the owner's clipboard nor depend on it being open-able).
struct Ops {
    BOOL (WINAPI *open)(HWND);
    BOOL (WINAPI *close)();
    BOOL (WINAPI *empty)();
    HANDLE (WINAPI *setData)(UINT, HANDLE);
    HANDLE (WINAPI *getData)(UINT);
    BOOL (WINAPI *available)(UINT);
    DWORD retryDelayMs;       // the wait between two tries of OpenClipboard
};

const int kTries = 10;
const DWORD kRetryDelayMs = 40;

// Uses the given calls from now on; null goes back to the real ones. Returns the ones in use before.
const Ops* SetOps(const Ops* ops);

// OpenClipboard, tried up to kTries times.
bool Open(HWND owner);
// Puts text on the clipboard (nothing for an empty text). False when the clipboard could not be opened or written.
bool SetText(HWND owner, const std::wstring& text);
// Reads the text of the clipboard. False when there is none or the clipboard could not be opened; text is then unchanged.
bool GetText(HWND owner, std::wstring& text);
// Whether the clipboard holds text, without opening it.
bool HasText();

}  // namespace clip
