// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - text on the clipboard, see clipboard.h.

#include "clipboard.h"

#include <string.h>
#include <wchar.h>

namespace clip {

static const Ops kReal = { ::OpenClipboard, ::CloseClipboard, ::EmptyClipboard, ::SetClipboardData, ::GetClipboardData,
                           ::IsClipboardFormatAvailable, kRetryDelayMs };
static const Ops* g_ops = &kReal;

const Ops* SetOps(const Ops* ops) {
    const Ops* before = g_ops;
    g_ops = ops ? ops : &kReal;
    return before;
}

bool Open(HWND owner) {
    for (int i = 0; i < kTries; i++) {
        if (g_ops->open(owner)) return true;
        if (i + 1 < kTries) Sleep(g_ops->retryDelayMs);
    }
    return false;
}

bool SetText(HWND owner, const std::wstring& text) {
    if (text.empty()) return false;
    if (!Open(owner)) return false;
    bool ok = false;
    g_ops->empty();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (g) {
        wchar_t* p = static_cast<wchar_t*>(GlobalLock(g));
        if (p) {
            memcpy(p, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
            GlobalUnlock(g);
            ok = g_ops->setData(CF_UNICODETEXT, g) != nullptr;
        }
        if (!ok) GlobalFree(g);       // the clipboard owns the block only when SetClipboardData took it
    }
    g_ops->close();
    return ok;
}

bool GetText(HWND owner, std::wstring& text) {
    if (!g_ops->available(CF_UNICODETEXT)) return false;
    if (!Open(owner)) return false;
    bool ok = false;
    HANDLE h = g_ops->getData(CF_UNICODETEXT);
    if (h) {
        const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(h));
        if (p) {
            // The text should end with a NUL, but the block is another program's: never read past its size.
            size_t room = GlobalSize(h) / sizeof(wchar_t);
            text.assign(p, wcsnlen(p, room));
            GlobalUnlock(h);
            ok = true;
        }
    }
    g_ops->close();
    return ok;
}

bool HasText() {
    return g_ops->available(CF_UNICODETEXT) != FALSE;
}

}  // namespace clip
