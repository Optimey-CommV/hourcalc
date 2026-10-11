// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - colours of the light and the dark theme, see theme.h.

#include "theme.h"

#include <dwmapi.h>
#ifdef HC_TESTHOOKS
#include <vector>
#endif

namespace theme {

using gfx::Rgb;
using gfx::RgbA;

#ifdef HC_TESTHOOKS
static bool g_fake = false, g_fakeDark = false;
static Argb g_fakeAccent[2] = { 0, 0 };     // for the light and the dark theme

void SetSystemForTest(bool on, bool dark, Argb accentLight, Argb accentDark) {
    g_fake = on;
    g_fakeDark = dark;
    g_fakeAccent[0] = accentLight;
    g_fakeAccent[1] = accentDark;
}
#endif

bool SystemIsDark() {
#ifdef HC_TESTHOOKS
    if (g_fake) return g_fakeDark;
#endif
    DWORD v = 1, size = sizeof v;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize",
                     L"AppsUseLightTheme", RRF_RT_REG_DWORD, nullptr, &v, &size) == ERROR_SUCCESS)
        return v == 0;
    return false;
}

Argb SystemAccent(bool dark) {
#ifdef HC_TESTHOOKS
    if (g_fake) return g_fakeAccent[dark ? 1 : 0];
#endif
    BYTE p[64];
    DWORD size = sizeof p;
    if (RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer\\Accent",
                     L"AccentPalette", RRF_RT_REG_BINARY, nullptr, p, &size) == ERROR_SUCCESS && size >= 32) {
        int i = (dark ? 1 : 4) * 4;
        return Rgb(p[i], p[i + 1], p[i + 2]);
    }
    return dark ? Rgb(0x4C, 0xC2, 0xFF) : Rgb(0x00, 0x67, 0xC0);
}

bool IsDarkFor(hc::settings::ThemeMode mode) {
    if (mode == hc::settings::ThemeMode::Light) return false;
    if (mode == hc::settings::ThemeMode::Dark) return true;
    return SystemIsDark();
}

Palette Create(bool dark, Argb accent) {
    Palette p;
    p.dark = dark;
    p.accent = accent;
    if (dark) {
        p.bg = Rgb(0x20, 0x20, 0x20);
        p.func = Rgb(0x32, 0x32, 0x32);
        p.funcHover = Rgb(0x3C, 0x3C, 0x3C);
        p.digit = Rgb(0x3B, 0x3B, 0x3B);
        p.digitHover = Rgb(0x45, 0x45, 0x45);
        p.edgeTop = Rgb(0x3F, 0x3F, 0x3F);
        p.edgeBottom = Rgb(0x2A, 0x2A, 0x2A);
        p.text = Rgb(0xFF, 0xFF, 0xFF);
        p.textSecondary = Rgb(0xC5, 0xC5, 0xC5);
        p.textDisabled = Rgb(0x80, 0x80, 0x80);
        p.accentText = Rgb(0x00, 0x00, 0x00);
        p.accentEdgeBottom = gfx::Mix(accent, Rgb(255, 255, 255), 0.20f);
        p.subtle = RgbA(0x14, 0xFF, 0xFF, 0xFF);
        p.subtlePressed = RgbA(0x0C, 0xFF, 0xFF, 0xFF);
        p.flyout = Rgb(0x2B, 0x2B, 0x2B);
        p.flyoutEdge = Rgb(0x45, 0x45, 0x45);
    } else {
        p.bg = Rgb(0xF0, 0xF4, 0xF7);
        p.func = Rgb(0xF7, 0xF9, 0xFB);
        p.funcHover = Rgb(0xF3, 0xF5, 0xF7);
        p.digit = Rgb(0xFF, 0xFF, 0xFF);
        p.digitHover = Rgb(0xF9, 0xF9, 0xF9);
        p.edgeTop = Rgb(0xE5, 0xE8, 0xEA);
        p.edgeBottom = Rgb(0xD5, 0xD7, 0xDA);
        p.text = Rgb(0x1A, 0x1B, 0x1B);
        p.textSecondary = Rgb(0x5F, 0x61, 0x62);
        p.textDisabled = Rgb(0xA3, 0xA5, 0xA7);
        p.accentText = Rgb(0xFF, 0xFF, 0xFF);
        p.accentEdgeBottom = gfx::Mix(accent, Rgb(255, 255, 255), 0.27f);
        p.subtle = RgbA(0x0F, 0x00, 0x00, 0x00);
        p.subtlePressed = RgbA(0x08, 0x00, 0x00, 0x00);
        p.flyout = Rgb(0xFA, 0xFB, 0xFC);
        p.flyoutEdge = Rgb(0xD5, 0xD7, 0xDA);
    }
    return p;
}

#ifdef HC_TESTHOOKS
struct CaptionRecord {
    HWND hwnd;
    CaptionState state;
};
static std::vector<CaptionRecord> g_captions;

CaptionState LastCaptionForTest(HWND hwnd) {
    for (const CaptionRecord& r : g_captions)
        if (r.hwnd == hwnd) return r.state;
    CaptionState none = {};
    return none;
}
#endif

void ApplyCaption(HWND hwnd, const Palette& p) {
    if (!hwnd) return;
    BOOL dark = p.dark ? TRUE : FALSE;
    // DWMWA_USE_IMMERSIVE_DARK_MODE is 20 from Windows 10 2004 on; Windows 10 1809 to 1909 know the same switch as the
    // undocumented 19.
    HRESULT hrDark = DwmSetWindowAttribute(hwnd, 20, &dark, sizeof dark);
    if (FAILED(hrDark)) hrDark = DwmSetWindowAttribute(hwnd, 19, &dark, sizeof dark);
    COLORREF caption = gfx::ToColorRef(p.bg);
    HRESULT hrCaption = DwmSetWindowAttribute(hwnd, 35 /* DWMWA_CAPTION_COLOR */, &caption, sizeof caption);
    COLORREF text = gfx::ToColorRef(p.text);
    HRESULT hrText = DwmSetWindowAttribute(hwnd, 36 /* DWMWA_TEXT_COLOR */, &text, sizeof text);
#ifdef HC_TESTHOOKS
    CaptionState st = { true, p.dark, caption, text, SUCCEEDED(hrDark) && SUCCEEDED(hrCaption) && SUCCEEDED(hrText) };
    for (CaptionRecord& r : g_captions)
        if (r.hwnd == hwnd) {
            r.state = st;
            return;
        }
    g_captions.push_back({ hwnd, st });
#else
    (void)hrDark; (void)hrCaption; (void)hrText;
#endif
}

}  // namespace theme
