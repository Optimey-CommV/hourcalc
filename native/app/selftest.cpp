// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - UI-level behaviour tests of the test build (--selftest). A port of the window parts of
// legacy\wpf\tests\UiTests.cs and ReviewTests.cs: the real window object and window procedure are
// driven with synthetic input on windows that are never shown, in a ',' and a '.' decimal-separator
// culture. Exit code 0 only when every check passes.

#include "selftest.h"
#include "app.h"
#include "clipboard.h"
#include "dialogs.h"
#include "embedded.h"
#include "flyout.h"
#include "ui.h"

#include <dwmapi.h>
#include <tlhelp32.h>
#include <wincrypt.h>
#include <memory>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <string>
#include <vector>

namespace selftest {

using app::MainWindow;
using gfx::RectF;
using ui::Rn;
namespace tm = hc::timemath;

static int g_checks = 0, g_failures = 0;
static bool g_trace = false;
static std::wstring g_culture;
static HINSTANCE g_inst = nullptr;

// ------------------------------------------------------------------ helpers

static std::wstring I(long long v) { return std::to_wstring(v); }

static void Eq(const std::wstring& what, const std::wstring& expected, const std::wstring& actual) {
    g_checks++;
    if (expected != actual) {
        g_failures++;
        wprintf(L"FAIL [%s] %s: expected <%s> but got <%s>\n", g_culture.c_str(), what.c_str(), expected.c_str(), actual.c_str());
    }
}

static void True(const std::wstring& what, bool value) {
    g_checks++;
    if (!value) {
        g_failures++;
        wprintf(L"FAIL [%s] %s: expected true\n", g_culture.c_str(), what.c_str());
    }
}

// An expected string with ',' as written in the tests, in the separator of the culture under test.
static std::wstring S(const std::wstring& s) {
    std::wstring sep = tm::DecimalSeparator(), r;
    for (wchar_t c : s) {
        if (c == L',') r += sep;
        else r += c;
    }
    return r;
}

static std::wstring F2(double v) {
    wchar_t buf[64];
    swprintf_s(buf, L"%.2f", v);
    return buf;
}

typedef std::unique_ptr<MainWindow> Win;

// No settings file; the window is laid out at the default client size and counts as the active window,
// so that the keyboard focus rules apply as they do in a window that is in use.
static Win New() {
    Win w(new MainWindow(nullptr));
    w->ApplyTheme(false);
    w->SetWindowActive(true);
    w->LayoutAt(324, 521, 1.0f);
    return w;
}

static void Keys(MainWindow& w, const wchar_t* keys) {
    for (const wchar_t* p = keys; *p; p++)
        if (*p != L' ') w.Press(std::wstring(1, *p));
}

// Text as the keyboard delivers it (WM_CHAR), and keys (WM_KEYDOWN).
static void RaiseText(MainWindow& w, const std::wstring& text) {
    for (wchar_t c : text) w.OnChar(c);
}

static void RaiseKey(MainWindow& w, UINT vk) {
    w.OnKeyDown(vk, false, false);
}

static void TypeInto(MainWindow& w, int field, const std::wstring& text) {
    for (wchar_t c : text) w.TypeText(field, std::wstring(1, c));
}

static std::wstring LT(const MainWindow& w) { return w.GetConverterText(0); }
static std::wstring RT(const MainWindow& w) { return w.GetConverterText(1); }

static bool Near(float a, float b, float tol) { return a >= b - tol && a <= b + tol; }

static std::wstring ReadTextFile(const std::wstring& path) {
    HANDLE h = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return L"<missing>";
    char buf[512];
    DWORD n = 0;
    ReadFile(h, buf, sizeof buf - 1, &n, nullptr);
    CloseHandle(h);
    std::wstring s;
    for (DWORD i = 0; i < n; i++) s += (wchar_t)(unsigned char)buf[i];
    while (!s.empty() && (s.back() == L'\r' || s.back() == L'\n' || s.back() == L' ')) s.pop_back();
    return s;
}

static bool FileExists(const std::wstring& path) {
    return GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES;
}

static bool WriteTextFile(const std::wstring& path, const char* text) {
    HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD n = 0;
    bool ok = WriteFile(f, text, (DWORD)strlen(text), &n, nullptr) != 0;
    CloseHandle(f);
    return ok;
}

// The menu item with an id (an item that is not there reads as an unchecked button with the id -1).
static const flyout::Item* ItemById(const flyout::Flyout& m, int id) {
    static const flyout::Item none = { flyout::Item::Button, L"", -1, false, {} };
    for (auto& it : m.Items())
        if (it.id == id) return &it;
    return &none;
}

static std::wstring TempFile(const wchar_t* ext) {
    wchar_t dir[MAX_PATH];
    GetTempPathW(MAX_PATH, dir);
    static int counter = 0;
    return std::wstring(dir) + L"HourCalcSelfTest-" + I(GetCurrentProcessId()) + L"-" + I(++counter) + ext;
}

static std::string Sha256Hex(const void* data, size_t n) {
    HCRYPTPROV prov = 0;
    HCRYPTHASH hash = 0;
    std::string hex;
    if (CryptAcquireContextW(&prov, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT) && CryptCreateHash(prov, CALG_SHA_256, 0, 0, &hash) &&
        CryptHashData(hash, static_cast<const BYTE*>(data), (DWORD)n, 0)) {
        BYTE buf[32];
        DWORD len = sizeof buf;
        if (CryptGetHashParam(hash, HP_HASHVAL, buf, &len, 0)) {
            const char* digits = "0123456789abcdef";
            for (DWORD i = 0; i < len; i++) {
                hex += digits[buf[i] >> 4];
                hex += digits[buf[i] & 15];
            }
        }
    }
    if (hash) CryptDestroyHash(hash);
    if (prov) CryptReleaseContext(prov, 0);
    return hex;
}

// ------------------------------------------------------------------ the legacy Grid distribution

static void GridDistribution() {
    g_culture = L"-";
    // The order equal stars are resolved in (measured on the .NET Framework that produced the WPF snapshots).
    const int expect[][9] = {
        { 2, 1, 0 }, { 3, 2, 1, 0 }, { 4, 2, 3, 0, 1 }, { 5, 3, 4, 2, 0, 1 }, { 6, 3, 4, 5, 0, 1, 2 }, { 7, 4, 5, 6, 3, 0, 1, 2 }, { 8, 5, 4, 7, 6, 1, 0, 3, 2 }
    };
    for (auto& e : expect) {
        std::vector<int> order;
        app::TieOrder(e[0], order);
        bool ok = (int)order.size() == e[0];
        for (int i = 0; ok && i < e[0]; i++) ok = order[(size_t)i] == e[1 + i];
        True(L"tie order of " + I(e[0]) + L" equal stars", ok);
    }
    // Sizes that the WPF Grid gave for equal stars (obj\geom data): total, count, sizes.
    struct Row { int total, n; int sizes[8]; };
    const Row rows[] = {
        { 319, 3, { 107, 106, 106 } }, { 319, 4, { 80, 79, 80, 80 } }, { 279, 4, { 70, 69, 70, 70 } }, { 579, 4, { 144, 145, 145, 145 } },
        { 357, 5, { 72, 71, 71, 71, 72 } }, { 597, 5, { 120, 119, 119, 119, 120 } }, { 364, 6, { 61, 60, 61, 61, 61, 60 } },
        { 21, 2, { 11, 10 } }, { 23, 2, { 11, 12 } }, { 22, 4, { 6, 5, 6, 5 } }, { 23, 3, { 7, 8, 8 } }, { 22, 3, { 7, 8, 7 } }, { 25, 3, { 9, 8, 8 } },
    };
    for (auto& r : rows) {
        std::vector<float> sizes;
        app::StarSizes((double)r.total, r.n, sizes);
        bool ok = (int)sizes.size() == r.n;
        for (int i = 0; ok && i < r.n; i++) ok = (int)sizes[(size_t)i] == r.sizes[i];
        True(L"star sizes " + I(r.total) + L" / " + I(r.n), ok);
    }
}

// ------------------------------------------------------------------ geometry against the WPF build

struct GeoRect { const wchar_t* name; float x, y, w, h; };

static void CheckRects(const wchar_t* tag, MainWindow& w, const std::vector<RectF>& want) {
    const app::Layout& lay = w.GetLayout();
    for (size_t i = 0; i < want.size() && i < lay.keys.size(); i++) {
        const RectF& a = lay.keys[i];
        const RectF& b = want[i];
        True(std::wstring(tag) + L": key " + I((long long)i) + L" at (" + I((long long)a.x) + L"," + I((long long)a.y) + L"," + I((long long)a.w) + L"," + I((long long)a.h) +
                 L"), the WPF build has (" + I((long long)b.x) + L"," + I((long long)b.y) + L"," + I((long long)b.w) + L"," + I((long long)b.h) + L")",
             a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h);
    }
}

static void GeometryAgainstWpf() {
    g_culture = L"-";
    // From the layout dump of the WPF build (obj\geom\geom.txt), converter at 324 x 521.
    {
        Win w = New();
        w->SetConverterText(0, L"08:30");
        const app::Layout& L = w->GetLayout();
        True(L"conv header height 48", L.headerH == 48);
        True(L"conv menu button (2,2,40,40)", L.menuBtn.x == 2 && L.menuBtn.y == 2 && L.menuBtn.w == 40 && L.menuBtn.h == 40);
        True(L"conv pin (281,2,40,40)", L.pin.x == 281 && L.pin.y == 2 && L.pin.w == 40 && L.pin.h == 40);
        True(L"conv tabs (44,0,92,48) and (138,0,93,48)", L.tab[0].x == 44 && L.tab[0].w == 92 && L.tab[1].x == 138 && L.tab[1].w == 93);
        True(L"conv value boxes (14,70,143,53) and (169,70,143,53)",
             L.box[0].x == 14 && L.box[0].y == 70 && L.box[0].w == 143 && L.box[0].h == 53 && L.box[1].x == 169 && L.box[1].w == 143);
        True(L"conv underline (14,123,141,2)", L.under[0].x == 14 && L.under[0].y == 123 && L.under[0].w == 141 && L.under[0].h == 2);
        std::vector<RectF> keys = { { 4, 155, 104, 59 }, { 110, 155, 103, 59 }, { 216, 155, 103, 59 }, { 4, 216, 104, 58 }, { 110, 216, 103, 58 }, { 216, 216, 103, 58 },
                                    { 4, 276, 104, 59 }, { 110, 276, 103, 59 }, { 216, 276, 103, 59 }, { 4, 337, 104, 59 }, { 110, 337, 103, 59 }, { 216, 337, 103, 59 },
                                    { 4, 398, 104, 59 }, { 110, 398, 103, 59 }, { 216, 398, 103, 59 }, { 4, 459, 316, 58 } };
        CheckRects(L"converter 324x521", *w, keys);
    }
    {
        Win w = New();
        w->SelectTab(1);
        Keys(*w, L"7:30+7:20=");
        const app::Layout& L = w->GetLayout();
        True(L"calc expression line (14,48,296,19)", L.expr.x == 14 && L.expr.y == 48 && L.expr.w == 296 && L.expr.h == 19);
        True(L"calc values (14,83,141,53) and (169,83,141,53)",
             L.calcVal[0].x == 14 && L.calcVal[0].y == 83 && L.calcVal[0].w == 141 && L.calcVal[0].h == 53 && L.calcVal[1].x == 169);
        std::vector<RectF> keys = { { 4, 162, 77, 70 }, { 84, 162, 76, 70 }, { 162, 162, 77, 70 }, { 242, 162, 77, 70 },
                                    { 4, 234, 77, 69 }, { 84, 234, 76, 69 }, { 162, 234, 77, 69 } };
        CheckRects(L"calculator 324x521", *w, keys);
    }
    {
        Win w = New();
        w->LayoutAt(284, 441, 1.0f);
        std::vector<RectF> keys = { { 4, 135, 90, 49 }, { 96, 135, 90, 49 }, { 190, 135, 90, 49 } };
        CheckRects(L"converter 284x441", *w, keys);
    }
}

// ------------------------------------------------------------------ converter

static void Check(MainWindow& w, int field, const wchar_t* input, const wchar_t* expectedOther) {
    w.Press(L"RESET");
    w.SetConverterText(field, S(input));
    Eq(L"field " + I(field) + L" <" + input + L"> other", S(expectedOther), w.GetConverterText(1 - field));
}

static void ConverterWiring() {
    Win wp = New();
    MainWindow& w = *wp;
    Eq(L"starts on the converter", L"0", I(w.CurrentTab()));
    Eq(L"left field is active at start", L"0", I(w.ActiveField()));
    Eq(L"left empty", L"", LT(w));
    Eq(L"right empty", L"", RT(w));

    Keys(w, L"830");
    Eq(L"keypad left", L"830", LT(w));
    Eq(L"keypad right follows", S(L"8,50"), RT(w));
    Eq(L"caption", L"8 h 30 min", w.ConverterCaption());
    w.Press(L"CONVERT");
    Eq(L"convert normalizes left", L"08:30", LT(w));
    Eq(L"convert keeps right", S(L"8,50"), RT(w));

    w.Press(L"RESET");
    Keys(w, L"8,");
    Eq(L"',' is ignored while left is active", L"8", LT(w));
    Keys(w, L":3");
    Eq(L"':' accepted on the left", L"8:3", LT(w));
    Eq(L"right follows 8:3", S(L"8,05"), RT(w));
    w.Press(L"0");
    Eq(L"8:30", L"8:30", LT(w));
    Eq(L"8:30 right", S(L"8,50"), RT(w));
    w.Press(L"5");
    Eq(L"third minute digit is rejected", L"8:30", LT(w));

    w.SetConverterText(1, S(L"1,5"));
    Eq(L"right made active", L"1", I(w.ActiveField()));
    w.Press(L"RESET");
    Eq(L"reset left", L"", LT(w));
    Eq(L"reset right", L"", RT(w));
    Eq(L"reset caption", L"", w.ConverterCaption());
    Eq(L"reset activates left", L"0", I(w.ActiveField()));

    w.SetConverterText(1, L"");
    Keys(w, L"8,50");
    Eq(L"keypad right", S(L"8,50"), RT(w));
    Eq(L"left follows", L"08:30", LT(w));
    w.Press(L":");
    Eq(L"':' is ignored while right is active", S(L"8,50"), RT(w));
    w.Press(L",");
    Eq(L"second ',' is ignored", S(L"8,50"), RT(w));

    w.Press(L"BACK");
    Eq(L"backspace right", S(L"8,5"), RT(w));
    Eq(L"backspace left follows", L"08:30", LT(w));
    w.Press(L"BACK");
    w.Press(L"BACK");
    Eq(L"backspace to the separator", L"8", RT(w));
    w.Press(L"CE");
    Eq(L"CE clears the active field", L"", RT(w));
    Eq(L"CE clears the other field", L"", LT(w));
    Eq(L"CE clears the caption", L"", w.ConverterCaption());

    w.SetConverterText(1, S(L"8,5"));
    w.Press(L"CONVERT");
    Eq(L"convert normalizes right", S(L"8,50"), RT(w));
    Eq(L"convert left", L"08:30", LT(w));
    w.SetConverterText(0, L"0:90");
    w.Press(L"CONVERT");
    Eq(L"convert 0:90", L"01:30", LT(w));
    Eq(L"convert 0:90 right", S(L"1,50"), RT(w));

    Check(w, 0, L"08:30", L"8,50");
    Check(w, 1, L"8,50", L"08:30");
    Check(w, 1, L"8,5", L"08:30");
    Check(w, 0, L"08:20", L"8,33");
    Check(w, 1, L"8,33", L"08:20");
    Check(w, 0, L"830", L"8,50");
    Check(w, 0, L"8.30", L"8,50");
    Check(w, 0, L"8u30", L"8,50");
    Check(w, 0, L"0:45", L"0,75");
    Check(w, 1, L"0,1", L"00:06");
    Check(w, 0, L"123:45", L"123,75");
    Check(w, 1, L"8,51", L"08:31");
    Check(w, 0, L"0:90", L"1,50");
}

static void ConverterEditing() {
    Win wp = New();
    MainWindow& w = *wp;
    True(L"colon key exists", w.KeyIndex(L":") >= 0);
    True(L"decimal key exists", w.KeyIndex(L",") >= 0);
    True(L"colon enabled when left is active", w.KeyEnabled(L":"));
    True(L"decimal disabled when left is active", !w.KeyEnabled(L","));
    w.SetConverterText(1, L"");
    True(L"colon disabled when right is active", !w.KeyEnabled(L":"));
    True(L"decimal enabled when right is active", w.KeyEnabled(L","));
    Eq(L"decimal key label is the culture's separator", tm::DecimalSeparator(), w.KeyAt(w.KeyIndex(L",")).label);
    w.Press(L"RESET");
    True(L"reset re-enables the colon", w.KeyEnabled(L":"));

    w.SetConverterText(0, L"8,30");
    Eq(L"comma ignored in the left field", L"830", LT(w));
    w.SetConverterText(0, L"8h30");
    Eq(L"h becomes a colon", L"8:30", LT(w));
    w.SetConverterText(1, L"8:50");
    Eq(L"colon ignored in the right field", L"850", RT(w));
    w.SetConverterText(1, L"8.5");
    Eq(L"dot shown as the culture's separator", S(L"8,5"), RT(w));
    w.SetConverterText(1, L"0,1234567");
    Eq(L"at most 6 decimals", S(L"0,123456"), RT(w));
    w.SetConverterText(0, L"abc");
    Eq(L"letters rejected", L"", LT(w));
    Eq(L"nothing to convert", L"", RT(w));
}

// ------------------------------------------------------------------ calculator

static void CalculatorWiring() {
    Win wp = New();
    MainWindow& w = *wp;
    w.SelectTab(1);
    Eq(L"calculator tab", L"1", I(w.CurrentTab()));
    Keys(w, L"7:30+7:20=");
    Eq(L"calc left", L"14:50", w.Engine().MainText());
    Eq(L"calc right", S(L"14,83"), w.Engine().DecimalText());
    Eq(L"calc expression", L"07:30 + 07:20 =", w.Engine().Expression());
    w.Press(L"C");
    Eq(L"calc cleared", L"00:00", w.Engine().MainText());

    Keys(w, L"7:30/0=");
    Eq(L"div0 text", L"Cannot divide by zero", w.Engine().MainText());
    True(L"plus disabled in the error state", !w.KeyEnabled(L"+"));
    True(L"equals disabled in the error state", !w.KeyEnabled(L"="));
    w.Press(L"C");
    True(L"plus enabled again after C", w.KeyEnabled(L"+"));
    True(L"equals enabled again after C", w.KeyEnabled(L"="));

    w.SelectTab(0);
    Keys(w, L"12");
    Eq(L"converter after calculator use", L"12", LT(w));
    Eq(L"converter value", S(L"12,00"), RT(w));
    w.SelectTab(1);
    Eq(L"calculator state survives a tab switch", L"00:00", w.Engine().MainText());
}

// ------------------------------------------------------------------ keyboard routing

static void KeyboardRouting() {
    {
        Win wp = New();
        MainWindow& w = *wp;
        w.SelectTab(1);
        RaiseText(w, L"7:30+7:20");
        Eq(L"calc typed expression", L"07:30 +", w.Engine().Expression());
        Eq(L"calc typed entry", L"7:20", w.Engine().MainText());
        RaiseKey(w, VK_RETURN);
        Eq(L"calc Enter is equals", L"14:50", w.Engine().MainText());
        Eq(L"calc Enter expression", L"07:30 + 07:20 =", w.Engine().Expression());
        RaiseKey(w, VK_ESCAPE);
        Eq(L"calc Esc is C", L"00:00", w.Engine().MainText());
        RaiseText(w, L"7u30");
        Eq(L"calc u is a time separator", L"7:30", w.Engine().MainText());
        RaiseText(w, L"x");
        Eq(L"calc unknown character ignored", L"7:30", w.Engine().MainText());
        RaiseKey(w, VK_BACK);
        Eq(L"calc Backspace", L"7:3", w.Engine().MainText());
        RaiseKey(w, VK_DELETE);
        Eq(L"calc Delete is CE", L"0", w.Engine().MainText());
        RaiseText(w, L"8");
        RaiseText(w, L".");
        RaiseText(w, L"5");
        RaiseText(w, L"=");
        Eq(L"calc decimal point and =", L"08:30", w.Engine().MainText());
        RaiseText(w, L"*2=");
        Eq(L"calc * as text", L"17:00", w.Engine().MainText());
        RaiseText(w, L"/0=");
        Eq(L"calc / as text", L"Cannot divide by zero", w.Engine().MainText());
        RaiseText(w, L"5");
        Eq(L"calc digit after error", L"5", w.Engine().MainText());
    }
    {
        Win wp = New();
        MainWindow& w = *wp;
        RaiseText(w, L"8u30");
        Eq(L"converter text without focus", L"8:30", LT(w));
        Eq(L"converter text without focus, right", S(L"8,50"), RT(w));
        w.SetConverterText(0, L"830");
        RaiseKey(w, VK_RETURN);
        Eq(L"converter Enter converts", L"08:30", LT(w));
        RaiseKey(w, VK_ESCAPE);
        Eq(L"converter Esc resets left", L"", LT(w));
        Eq(L"converter Esc resets right", L"", RT(w));
        Eq(L"converter Esc activates left", L"0", I(w.ActiveField()));
        w.SetConverterText(1, L"");
        RaiseText(w, L"8,5");
        Eq(L"converter text goes to the active (right) field", S(L"8,5"), RT(w));
        Eq(L"converter right to left", L"08:30", LT(w));
    }
    {
        // Ctrl+1 / Ctrl+2 / Ctrl+Tab switch the tab.
        Win wp = New();
        MainWindow& w = *wp;
        w.OnKeyDown('2', true, false);
        Eq(L"Ctrl+2 selects the calculator", L"1", I(w.CurrentTab()));
        w.OnKeyDown('1', true, false);
        Eq(L"Ctrl+1 selects the converter", L"0", I(w.CurrentTab()));
        w.OnKeyDown(VK_TAB, true, false);
        Eq(L"Ctrl+Tab switches to the other tab", L"1", I(w.CurrentTab()));
        w.OnKeyDown(VK_TAB, true, false);
        Eq(L"Ctrl+Tab switches back", L"0", I(w.CurrentTab()));
        w.OnKeyDown(VK_NUMPAD2, true, false);
        Eq(L"Ctrl+Numpad2 selects the calculator", L"1", I(w.CurrentTab()));
    }
}

// ------------------------------------------------------------------ theme and the real window

static void ThemeAndTabs() {
    Win wp = New();
    MainWindow& w = *wp;
    w.ApplyTheme(true);
    Eq(L"dark background", L"FF202020", [&] { wchar_t b[16]; swprintf_s(b, L"%08X", w.Palette().bg); return std::wstring(b); }());
    w.ApplyTheme(false);
    Eq(L"light background", L"FFF0F4F7", [&] { wchar_t b[16]; swprintf_s(b, L"%08X", w.Palette().bg); return std::wstring(b); }());
    True(L"accent is opaque", gfx::A(w.Accent()) == 255);
    True(L"dark palette text colours", [&] { w.ApplyTheme(true); bool ok = w.Palette().textDisabled == gfx::Rgb(0x80, 0x80, 0x80); w.ApplyTheme(false); return ok; }());

    // The real window, never shown.
    MainWindow real(nullptr);
    True(L"the window can be created", real.Create(g_inst, true));
    HWND h = real.hwnd();
    wchar_t title[64] = {};
    GetWindowTextW(h, title, 64);
    Eq(L"window title", L"HourCalc", title);
    float d = GetDpiForWindow(h) / 96.0f;
    RECT rc;
    GetWindowRect(h, &rc);
    True(L"default size 340 x 560 DIP", rc.right - rc.left == (int)Rn(340 * d) && rc.bottom - rc.top == (int)Rn(560 * d));
    MINMAXINFO mm = {};
    SendMessageW(h, WM_GETMINMAXINFO, 0, (LPARAM)&mm);
    True(L"minimum size 300 x 480 DIP", mm.ptMinTrackSize.x == (int)Rn(300 * d) && mm.ptMinTrackSize.y == (int)Rn(480 * d));
    True(L"the window is not visible", !IsWindowVisible(h));
}

// The real window procedure, driven with synthetic messages.
static void RealWindow() {
    MainWindow w(nullptr);
    True(L"real window created", w.Create(g_inst, true));
    HWND h = w.hwnd();
    // typing through WM_CHAR and keys through WM_KEYDOWN
    for (wchar_t c : std::wstring(L"8u30")) SendMessageW(h, WM_CHAR, c, 0);
    Eq(L"WM_CHAR into the left field", L"8:30", LT(w));
    Eq(L"WM_CHAR: the right field follows", S(L"8,50"), RT(w));
    SendMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
    Eq(L"WM_KEYDOWN Esc resets", L"", LT(w));
    SendMessageW(h, WM_KEYDOWN, '2', 0);      // no Ctrl: nothing happens
    Eq(L"a digit key code without Ctrl does not switch the tab", L"0", I(w.CurrentTab()));
    // the window is sized by messages
    SetWindowPos(h, nullptr, 0, 0, 500, 700, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOZORDER);
    RECT cr;
    GetClientRect(h, &cr);
    True(L"WM_SIZE lays the window out at the client size", w.GetLayout().W == cr.right && w.GetLayout().H == cr.bottom);
    // a paint request into a memory DC: the Convert key is the accent colour
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    SendMessageW(h, WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
    const app::Layout& lay = w.GetLayout();
    const RectF& convert = lay.keys.back();
    COLORREF px = GetPixel(mem, (int)(convert.x + 6), (int)(convert.y + convert.h / 2));
    gfx::Argb accent = w.Palette().accent;
    True(L"WM_PRINTCLIENT paints the accent Convert key", GetRValue(px) == gfx::R(accent) && GetGValue(px) == gfx::G(accent) && GetBValue(px) == gfx::B(accent));
    COLORREF bg = GetPixel(mem, 0, (int)lay.headerH + 1);
    True(L"WM_PRINTCLIENT paints the window colour", GetRValue(bg) == gfx::R(w.Palette().bg));
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

// Mouse input through the real window procedure (hidden window: no popup is opened, nothing is shown).
static void Click(HWND h, float x, float y, int times = 1) {
    LPARAM lp = MAKELPARAM((int)x, (int)y);
    for (int i = 0; i < times; i++) {
        SendMessageW(h, WM_MOUSEMOVE, 0, lp);
        SendMessageW(h, i == 0 ? WM_LBUTTONDOWN : WM_LBUTTONDBLCLK, MK_LBUTTON, lp);
        SendMessageW(h, WM_LBUTTONUP, 0, lp);
    }
}

static void ClickKey(MainWindow& w, const wchar_t* key) {
    const RectF& r = w.GetLayout().keys[(size_t)w.KeyIndex(key)];
    Click(w.hwnd(), r.x + r.w / 2, r.y + r.h / 2);
}

static void RealWindowMouse() {
    MainWindow w(nullptr);
    True(L"real window created for the mouse tests", w.Create(g_inst, true));
    HWND h = w.hwnd();
    w.SetWindowActive(true);
    ClickKey(w, L"8");
    ClickKey(w, L"3");
    ClickKey(w, L"0");
    Eq(L"clicking keypad keys types into the active field", L"830", LT(w));
    Eq(L"... and the other field follows", S(L"8,50"), RT(w));
    ClickKey(w, L",");
    Eq(L"a disabled key does nothing", L"830", LT(w));
    ClickKey(w, L"CONVERT");
    Eq(L"the Convert key converts", L"08:30", LT(w));
    ClickKey(w, L"RESET");
    Eq(L"the Reset key resets", L"", LT(w));

    // clicking a field: the first click selects all, a click in the focused field places the caret
    w.SetConverterText(0, L"08:30");
    w.GotFocus(0, false);
    const RectF& b1 = w.GetLayout().box[1];
    Click(h, b1.x + b1.w / 2, b1.y + b1.h / 2);
    Eq(L"a click on the other field focuses it", L"1", I(w.FocusField()));
    Eq(L"... and selects all of its text", I((long long)RT(w).size()), I(w.FieldRef(1).selLength()));
    Eq(L"... and makes it the active field", L"1", I(w.ActiveField()));
    const RectF& b0 = w.GetLayout().box[0];
    Click(h, b0.x + b0.w / 2, b0.y + b0.h / 2);
    Click(h, b0.x + b0.w / 2, b0.y + b0.h / 2);
    Eq(L"a second click in the focused field places the caret (no selection)", L"0", I(w.FieldRef(0).selLength()));
    Click(h, b0.x + b0.w / 2, b0.y + b0.h / 2, 2);
    Eq(L"a double click selects all", I((long long)LT(w).size()), I(w.FieldRef(0).selLength()));
    // a drag selects a range
    w.GotFocus(0, false);
    w.FieldRef(0).anchor = w.FieldRef(0).caret = 0;
    float y = b0.y + b0.h / 2;
    SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((int)(b0.right() - 3), (int)y));
    SendMessageW(h, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM((int)(b0.right() - 40), (int)y));
    SendMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM((int)(b0.right() - 40), (int)y));
    True(L"dragging inside the focused field selects a range", w.FieldRef(0).selLength() > 0 && w.FieldRef(0).selLength() < (int)LT(w).size());

    // the tabs
    const RectF& tab = w.GetLayout().tab[1];
    Click(h, tab.x + tab.w / 2, tab.y + tab.h / 2);
    Eq(L"clicking the Calculator tab selects it", L"1", I(w.CurrentTab()));
    ClickKey(w, L"7");
    ClickKey(w, L":");
    ClickKey(w, L"3");
    ClickKey(w, L"0");
    ClickKey(w, L"+");
    ClickKey(w, L"7");
    ClickKey(w, L":");
    ClickKey(w, L"2");
    ClickKey(w, L"0");
    ClickKey(w, L"=");
    Eq(L"the calculator keys work with the mouse", L"14:50", w.Engine().MainText());
    const RectF& tab0 = w.GetLayout().tab[0];
    Click(h, tab0.x + tab0.w / 2, tab0.y + tab0.h / 2);
    Eq(L"clicking the Converter tab selects it", L"0", I(w.CurrentTab()));

    // the keep-on-top button toggles the state (the window is only hidden, so nothing moves on the screen)
    const RectF& pin = w.GetLayout().pin;
    Click(h, pin.x + pin.w / 2, pin.y + pin.h / 2);
    True(L"the pin button toggles keep on top", w.Topmost());
    Click(h, pin.x + pin.w / 2, pin.y + pin.h / 2);
    True(L"... and back", !w.Topmost());

    // a press that is released elsewhere is not a click
    std::wstring beforePress = LT(w);
    const RectF& k5 = w.GetLayout().keys[(size_t)w.KeyIndex(L"5")];
    SendMessageW(h, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM((int)(k5.x + 5), (int)(k5.y + 5)));
    SendMessageW(h, WM_LBUTTONUP, 0, MAKELPARAM(1, 1));
    Eq(L"a press released outside the key is not a click", beforePress, LT(w));
    // Tab moves between the fields through the window procedure
    SendMessageW(h, WM_SETFOCUS, 0, 0);
    SendMessageW(h, WM_KEYDOWN, VK_TAB, 0);
    Eq(L"Tab through the window procedure moves to the other field", L"1", I(w.FocusField()));
}

// ------------------------------------------------------------------ header, keypad geometry

static void HeaderAndGeometry() {
    Win wp = New();
    MainWindow& w = *wp;
    const app::Layout& L = w.GetLayout();
    True(L"the menu button is at the far left of the header", L.menuBtn.x >= 0 && L.menuBtn.x <= 4);
    True(L"the pin is at the far right of the header", 324 - L.pin.right() >= 0 && 324 - L.pin.right() <= 4);
    True(L"the tabs start after the menu button", L.tab[0].x >= L.menuBtn.right() - 0.5f);
    auto key = [&](const wchar_t* k) { return L.keys[(size_t)w.KeyIndex(k)]; };
    RectF k7 = key(L"7"), k8 = key(L"8"), k9 = key(L"9"), k4 = key(L"4"), k1 = key(L"1");
    float hGap = ((k8.x - k7.right()) + (k9.x - k8.right())) / 2, vGap = ((k4.y - k7.bottom()) + (k1.y - k4.bottom())) / 2;
    True(L"keys are 3 px apart horizontally (" + F2(hGap) + L")", hGap >= 2.5f && hGap <= 3.5f);
    True(L"keys are 2 px apart vertically (" + F2(vGap) + L")", vGap >= 1.5f && vGap <= 2.5f);
    True(L"the first key is 4 px from the left edge", k7.x >= 3.5f && k7.x <= 4.5f);
    True(L"the last key column ends 4-5 px from the right edge", 324 - k9.right() >= 3.5f && 324 - k9.right() <= 5.5f);
}

static void KeypadStaysPut() {
    const int sizes[][2] = { { 284, 441 }, { 324, 521 }, { 584, 861 } };
    for (auto& s : sizes) {
        std::wstring tag = I(s[0]) + L"x" + I(s[1]);
        {
            Win wp = New();
            MainWindow& w = *wp;
            w.LayoutAt(s[0], s[1], 1.0f);
            float top = w.GetLayout().keys[(size_t)w.KeyIndex(L"RESET")].y;
            for (const wchar_t* text : { L"8", L"08:30", L"123456", L"1234567", L"1234567:5", L"1234567:59", L"0:45" }) {
                w.SetConverterText(0, text);
                w.LayoutAt(s[0], s[1], 1.0f);
                Eq(std::wstring(L"converter keypad stays put, left <") + text + L"> at " + tag, F2(top), F2(w.GetLayout().keys[(size_t)w.KeyIndex(L"RESET")].y));
            }
            for (const wchar_t* text : { L"8,5", L"1234567", L"1234567,1", L"1234567,123456", L"0,01" }) {
                w.SetConverterText(1, S(text));
                w.LayoutAt(s[0], s[1], 1.0f);
                Eq(std::wstring(L"converter keypad stays put, right <") + text + L"> at " + tag, F2(top), F2(w.GetLayout().keys[(size_t)w.KeyIndex(L"RESET")].y));
            }
            w.Press(L"RESET");
            w.LayoutAt(s[0], s[1], 1.0f);
            Eq(L"converter keypad stays put after Reset at " + tag, F2(top), F2(w.GetLayout().keys[(size_t)w.KeyIndex(L"RESET")].y));
        }
        {
            Win wp = New();
            MainWindow& w = *wp;
            w.SelectTab(1);
            w.LayoutAt(s[0], s[1], 1.0f);
            float top = w.GetLayout().keys[(size_t)w.KeyIndex(L"/")].y;
            std::wstring pressed;
            for (const wchar_t* keys : { L"7", L"12345", L"67", L":", L"5", L"9", L"+", L"7:30+7:20+8:00+1:15+2:45+3:10+4:00+5:00+6:00+", L"=", L"*0=", L"/0=", L"C" }) {
                for (const wchar_t* p = keys; *p; p++) w.Press(std::wstring(1, *p));
                pressed += keys;
                w.LayoutAt(s[0], s[1], 1.0f);
                Eq(L"calculator keypad stays put after <" + pressed + L"> at " + tag, F2(top), F2(w.GetLayout().keys[(size_t)w.KeyIndex(L"/")].y));
            }
        }
    }
}

// Two values side by side use one font size, so a long value does not sit next to a bigger one.
static void ValuePairsShareASize() {
    const int sizes[][2] = { { 284, 441 }, { 324, 521 }, { 584, 861 } };
    for (auto& s : sizes) {
        std::wstring tag = I(s[0]) + L"x" + I(s[1]);
        Win wp = New();
        MainWindow& w = *wp;
        w.LayoutAt(s[0], s[1], 1.0f);
        float max = 40.0f * w.GetLayout().s;
        w.SetConverterText(1, S(L"1234567,123456"));
        True(L"a long converter value shrinks the pair at " + tag + L" (" + F2(w.ConverterFontPx()) + L")", w.ConverterFontPx() < max - 1.0f);
        w.SetConverterText(0, L"8:30");
        True(L"short values use the largest size at " + tag, Near(w.ConverterFontPx(), max, 0.01f));
        Win cp = New();
        MainWindow& c = *cp;
        c.SelectTab(1);
        c.LayoutAt(s[0], s[1], 1.0f);
        for (wchar_t ch : std::wstring(L"1234567:59")) c.Press(std::wstring(1, ch));
        True(L"a long calculator entry shrinks the pair at " + tag, c.CalcFontPx() < 40.0f * c.GetLayout().s - 1.0f);
        for (wchar_t ch : std::wstring(L"+1=")) c.Press(std::wstring(1, ch));
        True(L"the pair is fitted after = at " + tag, c.CalcFontPx() > 0);
    }
}

// A disabled key (error state) must look disabled whatever it shows: a string or a glyph.
static void DisabledAndPressedColors() {
    Win wp = New();
    MainWindow& w = *wp;
    w.SelectTab(1);
    const theme::Palette& p = w.Palette();
    const wchar_t* keys[] = { L"+", L"-", L"*", L"/", L"BACK", L"=", L":", L"," };
    for (const wchar_t* k : keys)
        True(std::wstring(L"enabled key ") + k + L" uses its normal text colour", w.KeyForeground(w.KeyIndex(k)) == (std::wstring(k) == L"=" ? p.accentText : p.text));
    Keys(w, L"7:30/0=");
    for (const wchar_t* k : keys) {
        True(std::wstring(L"key ") + k + L" is disabled in the error state", !w.KeyEnabled(k));
        True(std::wstring(L"disabled key ") + k + L" (glyph or text) uses the disabled text colour", w.KeyForeground(w.KeyIndex(k)) == p.textDisabled);
    }
    for (const wchar_t* k : { L"0", L"5", L"CE", L"C" })
        True(std::wstring(L"key ") + k + L" stays enabled and normal", w.KeyForeground(w.KeyIndex(k)) == p.text);
    True(L"the disabled colour differs from the normal colours", p.textDisabled != p.text && p.textDisabled != p.accentText);
    w.Press(L"C");
    for (const wchar_t* k : keys)
        True(std::wstring(L"key ") + k + L" is back to its normal text colour after C", w.KeyForeground(w.KeyIndex(k)) == (std::wstring(k) == L"=" ? p.accentText : p.text));
    True(L"a pressed key uses the pressed colour", w.KeyForeground(w.KeyIndex(L"5"), true) == p.textSecondary);
}

static void ExpressionLineFitsItsSlot() {
    Win wp = New();
    MainWindow& w = *wp;
    w.SelectTab(1);
    for (wchar_t c : std::wstring(L"7:30+7:20+8:00+1:15+2:45+3:10+4:00+5:00+6:00+")) w.Press(std::wstring(1, c));
    const int steps[][2] = { { 324, 521 }, { 584, 861 }, { 324, 521 }, { 284, 441 }, { 324, 521 } };
    for (auto& s : steps) {
        w.LayoutAt(s[0], s[1], 1.0f);
        std::wstring tag = L" at " + I(s[0]) + L"x" + I(s[1]);
        const std::wstring& e = w.ExpressionShown();
        True(L"chain starts with an ellipsis" + tag, e.compare(0, 2, L"\x2026 ") == 0 || (s[0] > 500 && !e.empty()));
        True(L"chain ends with the pending operator" + tag, e.size() >= 7 && e.compare(e.size() - 7, 7, L"06:00 +") == 0);
        float needed = gfx::TextWidth(e.c_str(), (int)e.size(), ui::TextFont(14.0f * w.GetLayout().s * w.GetLayout().d));
        True(L"the expression text fits the display" + tag + L" (" + F2(needed) + L" of " + F2(w.GetLayout().calcDisplay.w) + L")", needed <= w.GetLayout().calcDisplay.w + 0.5f);
    }
}

// Every text must fit the element that shows it, at every size.
static void Fitting() {
    const int sizes[][2] = { { 284, 441 }, { 324, 521 }, { 584, 861 }, { 900, 700 } };
    for (auto& s : sizes) {
        std::wstring tag = I(s[0]) + L"x" + I(s[1]);
        {
            Win wp = New();
            wp->SetConverterText(0, L"9999999:59");
            wp->LayoutAt(s[0], s[1], 1.0f);
            const app::Layout& lay = wp->GetLayout();
            for (int f = 0; f < 2; f++) {
                const std::wstring t = wp->GetConverterText(f);
                float need = gfx::TextWidth(t.c_str(), (int)t.size(), ui::DisplayFont(wp->ConverterFontPx()));
                True(L"converter long left <" + t + L"> fits its box at " + tag + L" (" + F2(need) + L" of " + F2(lay.box[f].w) + L")", need <= lay.box[f].w + 0.5f);
            }
        }
        {
            Win wp = New();
            wp->SetConverterText(1, S(L"9999999,999999"));
            wp->LayoutAt(s[0], s[1], 1.0f);
            const app::Layout& lay = wp->GetLayout();
            for (int f = 0; f < 2; f++) {
                const std::wstring t = wp->GetConverterText(f);
                float need = gfx::TextWidth(t.c_str(), (int)t.size(), ui::DisplayFont(wp->ConverterFontPx()));
                True(L"converter long right <" + t + L"> fits its box at " + tag, need <= lay.box[f].w + 0.5f);
            }
        }
        const wchar_t* sequences[] = { L"9999999:59+9999999=", L"7:30/0=", L"1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+1+" };
        for (const wchar_t* seq : sequences) {
            Win wp = New();
            wp->SelectTab(1);
            Keys(*wp, seq);
            wp->LayoutAt(s[0], s[1], 1.0f);
            const app::Layout& lay = wp->GetLayout();
            float need = gfx::TextWidth(wp->CalcMain().c_str(), (int)wp->CalcMain().size(), ui::DisplayFont(wp->CalcFontPx()));
            True(std::wstring(L"calculator <") + seq + L"> main value fits at " + tag, need <= lay.calcVal[0].w + 0.5f);
            if (wp->CalcRightVisible()) {
                float needR = gfx::TextWidth(wp->CalcDecimal().c_str(), (int)wp->CalcDecimal().size(), ui::DisplayFont(wp->CalcFontPx()));
                True(std::wstring(L"calculator <") + seq + L"> decimal value fits at " + tag, needR <= lay.calcVal[1].w + 0.5f);
            }
            RectF plus = lay.keys[(size_t)wp->KeyIndex(L"+")];
            True(L"plus key size at " + tag, plus.w >= 40 && plus.h >= 30);
        }
    }
}

// ------------------------------------------------------------------ Advanced menu, About, source code

static void AdvancedMenu() {
    std::wstring ini = TempFile(L".ini");
    {
        MainWindow w(ini.c_str());
        w.ApplyTheme(false);
        w.SetWindowActive(true);
        w.LayoutAt(324, 521, 1.0f);
        Eq(L"default decimal places", L"2", I(w.DecimalPlaces()));
        True(L"no ini file until the setting changes", !FileExists(ini));

        flyout::Flyout& m = w.MenuFlyout();
        const std::vector<flyout::Item>& items = m.Items();
        std::vector<const flyout::Item*> radios;
        bool header = false, label = false, about = false;
        for (auto& it : items) {
            // (the decimal places items; the Theme section has radio items too, see ThemeMenu)
            if (it.type == flyout::Item::Radio && it.id >= tm::MinDisplayDecimals && it.id <= tm::MaxDisplayDecimals) radios.push_back(&it);
            if (it.type == flyout::Item::Header && it.text == L"Advanced") header = true;
            if (it.type == flyout::Item::Label && it.text == L"Decimal places") label = true;
            if (it.type == flyout::Item::Button && it.text == L"About HourCalc") about = true;
        }
        Eq(L"five decimal places items", L"5", I((long long)radios.size()));
        if (radios.size() == 5) {
            Eq(L"first item is the default", L"2 (default)", radios[0]->text);
            Eq(L"last item", L"6", radios[4]->text);
            True(L"2 is checked", radios[0]->checked);
        }
        True(L"menu has the Advanced header", header);
        True(L"menu has the Decimal places label", label);
        True(L"menu has About HourCalc", about);

        w.SetConverterText(0, L"08:20");
        Eq(L"08:20 at 2 places", S(L"8,33"), RT(w));
        w.OnFlyoutCommand(3, 0);
        Eq(L"setting 3 places", L"3", I(w.DecimalPlaces()));
        Eq(L"08:20 at 3 places", S(L"8,333"), RT(w));
        True(L"3 is checked", ItemById(m, 3)->checked && !ItemById(m, 2)->checked);
        Eq(L"the ini file holds the setting (and the theme)", L"DecimalPlaces=3\r\nTheme=auto", ReadTextFile(ini));
        const wchar_t* expect[] = { L"8,33", L"8,333", L"8,3333", L"8,33333", L"8,333333" };
        for (int p = 2; p <= 6; p++) {
            w.SetDecimalPlaces(p);
            Eq(L"08:20 at " + I(p) + L" places", S(expect[p - 2]), RT(w));
            Eq(L"... the left field and the caption do not move", L"08:20", LT(w));
            Eq(L"... caption", L"8 h 20 min", w.ConverterCaption());
        }
        w.SetDecimalPlaces(2);
        Eq(L"back to 2 places, from the exact value (not from the rounded text)", S(L"8,33"), RT(w));

        w.SetConverterText(1, S(L"8,058"));
        Eq(L"typed 8,058 gives 08:03", L"08:03", LT(w));
        w.SetDecimalPlaces(4);
        Eq(L"the typed right text is not reformatted", S(L"8,058"), RT(w));
        Eq(L"... and the left keeps its value", L"08:03", LT(w));
        w.Press(L"CONVERT");
        Eq(L"Convert normalizes to 4 places", S(L"8,0580"), RT(w));
        Eq(L"... and keeps the left", L"08:03", LT(w));
        w.SetDecimalPlaces(2);

        w.SelectTab(1);
        Keys(w, L"7:30/4=");
        Eq(L"7:30 / 4 at 2 places", S(L"1,88"), w.Engine().DecimalText());
        w.SetDecimalPlaces(3);
        Eq(L"7:30 / 4 at 3 places", S(L"1,875"), w.Engine().DecimalText());
        Eq(L"... shown in the right field", S(L"1,875"), w.CalcDecimal());
        w.SetDecimalPlaces(4);
        Eq(L"7:30 / 4 at 4 places", S(L"1,8750"), w.Engine().DecimalText());
        Eq(L"... the left field is unchanged", L"01:53", w.Engine().MainText());
        w.SetDecimalPlaces(2);
        Eq(L"7:30 / 4 at 2 places again, from the exact value", S(L"1,88"), w.Engine().DecimalText());
        w.Press(L"C");
        Keys(w, L"0:1");
        w.SetDecimalPlaces(4);
        Eq(L"0:01 at 4 places", S(L"0,0167"), w.Engine().DecimalText());
        w.SetDecimalPlaces(2);
        Eq(L"0:01 at 2 places", S(L"0,02"), w.Engine().DecimalText());

        w.SetDecimalPlaces(5);
        MainWindow next(ini.c_str());
        Eq(L"the next window reads the ini file", L"5", I(next.DecimalPlaces()));
        True(L"... and its menu shows it", ItemById(next.MenuFlyout(), 5)->checked && !ItemById(next.MenuFlyout(), 2)->checked);
        HANDLE f = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        DWORD n;
        WriteFile(f, "DecimalPlaces=9\r\n", 17, &n, nullptr);
        CloseHandle(f);
        Eq(L"an invalid value gives the default", L"2", I(MainWindow(ini.c_str()).DecimalPlaces()));
        f = CreateFileW(ini.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        WriteFile(f, "garbage", 7, &n, nullptr);
        CloseHandle(f);
        Eq(L"garbage gives the default", L"2", I(MainWindow(ini.c_str()).DecimalPlaces()));
        DeleteFileW(ini.c_str());
        w.SetDecimalPlaces(5);
        True(L"choosing the current value writes nothing", !FileExists(ini));
        DeleteFileW(ini.c_str());
        w.SetDecimalPlaces(2);
        w.SetDecimalPlaces(6);
        True(L"a change writes the file", FileExists(ini));
        DeleteFileW(ini.c_str());
    }
    MainWindow nowhere((TempFile(L"") + L"\\nope\\HourCalc.ini").c_str());      // the folder cannot be made: its parent is not there either
    nowhere.SetDecimalPlaces(4);
    Eq(L"an unwritable folder: the setting still applies this session", L"4", I(nowhere.DecimalPlaces()));
    nowhere.SetConverterText(0, L"08:20");
    Eq(L"... and works", S(L"8,3333"), RT(nowhere));
    MainWindow plain(nullptr);
    Eq(L"a window without a settings file keeps the default", L"2", I(plain.DecimalPlaces()));

    // The popups' geometry at 100 %: the menu box is 236 x 441 (the Theme section is 145 px of it: the header 24, three items of
    // 36 and a separator of 13; without it the box was 296), the Copy menu fits its text.
    {
        Win wp = New();
        flyout::Flyout& m = wp->MenuFlyout();
        m.Layout(nullptr, 1.0f);
        True(L"the menu box is 236 x 441 (" + I(m.BoxWidth()) + L" x " + I(m.BoxHeight()) + L")", m.BoxWidth() == 236 && m.BoxHeight() == 441);
        flyout::Flyout& c = wp->CopyFlyout();
        c.Layout(nullptr, 1.0f);
        True(L"the Copy menu box is about 87 x 50 (" + I(c.BoxWidth()) + L" x " + I(c.BoxHeight()) + L")", c.BoxWidth() >= 80 && c.BoxWidth() <= 96 && c.BoxHeight() == 50);
        flyout::Flyout& t = wp->TipFlyout();
        t.SetText(L"Keep on top");
        t.Layout(nullptr, 1.0f);
        True(L"the tool tip has no shadow room", t.ShadowRoom() == 0 && t.BoxHeight() > 20);
        True(L"the menu has the shadow room", m.ShadowRoom() == 14);
    }
}

// ------------------------------------------------------------------ the theme choice: Auto / Light / Dark (SPEC 6b)

// The colours of SPEC 3: window, function key, digit key, text. (The accent is the one of the simulated Windows: dark 1 for the
// light theme, light 2 for the dark theme; the self test gives Windows the two accents of this machine's default.)
struct Spec3 {
    unsigned bg, func, digit, text;
};
static const Spec3 kSpecLight = { 0xF0F4F7, 0xF7F9FB, 0xFFFFFF, 0x1A1B1B };
static const Spec3 kSpecDark = { 0x202020, 0x323232, 0x3B3B3B, 0xFFFFFF };
static const gfx::Argb kAccentLight1 = gfx::Rgb(0x00, 0x67, 0xC0), kAccentDark2 = gfx::Rgb(0x4C, 0xC2, 0xFF);

// Windows, as the test build pretends it is: the light or the dark app theme, the accent of each theme.
static void FakeWindows(bool dark, gfx::Argb accentLight = kAccentLight1, gfx::Argb accentDark = kAccentDark2) {
    theme::SetSystemForTest(true, dark, accentLight, accentDark);
}

static void RealWindows() {
    theme::SetSystemForTest(false, false, 0, 0);
}

static std::wstring HexColor(gfx::Argb c) {
    wchar_t b[16];
    swprintf_s(b, L"%06X", (unsigned)(c & 0xFFFFFFu));
    return b;
}

static gfx::Argb PixelOf(const gfx::Canvas& c, int x, int y) {
    const uint8_t* p = c.Row(y) + 4 * x;          // top-down BGRA
    return gfx::Rgb(p[2], p[1], p[0]);
}

// What a window of the converter tab really paints, read from the rendered pixels: its background, a function key (Reset), a digit
// key (7) and the accent key (Convert), as "bg func digit accent".
static std::wstring PaintedColors(MainWindow& w) {
    w.SelectTab(0);
    w.LayoutAt(324, 521, 1.0f);
    gfx::Canvas c;
    w.Render(c);
    const app::Layout& L = w.GetLayout();
    auto key = [&](const wchar_t* k) {
        const RectF& r = L.keys[(size_t)w.KeyIndex(k)];
        return HexColor(PixelOf(c, (int)(r.x + 6), (int)(r.y + r.h / 2)));
    };
    return HexColor(PixelOf(c, 1, 1)) + L" " + key(L"RESET") + L" " + key(L"7") + L" " + key(L"CONVERT");
}

static std::wstring SpecColors(const Spec3& s, gfx::Argb accent) {
    return HexColor(gfx::Rgb((s.bg >> 16) & 255, (s.bg >> 8) & 255, s.bg & 255)) + L" " +
           HexColor(gfx::Rgb((s.func >> 16) & 255, (s.func >> 8) & 255, s.func & 255)) + L" " +
           HexColor(gfx::Rgb((s.digit >> 16) & 255, (s.digit >> 8) & 255, s.digit & 255)) + L" " + HexColor(accent);
}

static bool Windows11OrLater() {
    typedef LONG(WINAPI * RtlGetVersionFn)(OSVERSIONINFOW*);
    RtlGetVersionFn fn = (RtlGetVersionFn)GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "RtlGetVersion");
    OSVERSIONINFOW vi = { sizeof vi };
    return fn && fn(&vi) == 0 && vi.dwBuildNumber >= 22000;
}

// The title bar of a window is set to a theme. The immersive dark mode switch is read back from the window manager (DWM). The
// caption and text colour attributes can only be written, so the test build remembers what theme::ApplyCaption set (and whether
// DWM accepted it: it does on Windows 11, where the two colours exist).
static bool CaptionIs(HWND h, const Spec3& s) {
    BOOL dark = FALSE;
    if (FAILED(DwmGetWindowAttribute(h, 20, &dark, sizeof dark))) return false;
    theme::CaptionState st = theme::LastCaptionForTest(h);
    bool wantDark = s.bg == kSpecDark.bg;
    return st.known && (st.accepted || !Windows11OrLater()) && (dark != FALSE) == wantDark && st.dark == wantDark &&
           st.caption == RGB((s.bg >> 16) & 255, (s.bg >> 8) & 255, s.bg & 255) && st.text == RGB((s.text >> 16) & 255, (s.text >> 8) & 255, s.text & 255);
}

static std::wstring MenuSignature(const flyout::Flyout& m) {
    std::wstring s;
    for (auto& it : m.Items()) {
        if (!s.empty()) s += L"|";
        switch (it.type) {
        case flyout::Item::Header: s += L"H:" + it.text; break;
        case flyout::Item::Label: s += L"L:" + it.text; break;
        case flyout::Item::Radio: s += L"R:" + it.text; break;
        case flyout::Item::Button: s += L"B:" + it.text; break;
        case flyout::Item::Separator: s += L"S"; break;
        }
    }
    return s;
}

static std::wstring CheckedRadios(const flyout::Flyout& m) {
    std::wstring s;
    for (auto& it : m.Items())
        if (it.type == flyout::Item::Radio && it.checked) s += (s.empty() ? L"" : L",") + it.text;
    return s;
}

// The menu: its contents and order, the radio state, and what choosing each item does to the window that is painted.
static void ThemeMenu() {
    FakeWindows(false);
    {
        Win wp = New();
        MainWindow& w = *wp;
        flyout::Flyout& m = w.MenuFlyout();
        Eq(L"the menu: Theme section, Advanced section and About, with their separators",
           L"H:Theme|R:Auto (follow Windows)|R:Light|R:Dark|S|H:Advanced|L:Decimal places|R:2 (default)|R:3|R:4|R:5|R:6|S|B:About HourCalc", MenuSignature(m));
        Eq(L"by default Auto and 2 places are checked", L"Auto (follow Windows),2 (default)", CheckedRadios(m));
        Eq(L"the theme items have their own ids", L"110,111,112", I(ItemById(m, app::kCmdThemeAuto)->id) + L"," + I(ItemById(m, app::kCmdThemeLight)->id) + L"," + I(ItemById(m, app::kCmdThemeDark)->id));
        True(L"the theme items are not the decimal places items (own group)", ItemById(m, app::kCmdThemeDark)->group != ItemById(m, 3)->group);
        Eq(L"the choice is Auto", L"0", I((int)w.ThemeChoice()));
        Eq(L"Windows is light: painted colours of the light theme", SpecColors(kSpecLight, kAccentLight1), PaintedColors(w));

        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        Eq(L"Dark: the choice", L"2", I((int)w.ThemeChoice()));
        Eq(L"Dark: the radio items (the places stay)", L"Dark,2 (default)", CheckedRadios(m));
        True(L"Dark: the window is dark", w.IsDark());
        Eq(L"Dark on a light Windows: background, function keys, digit keys and the accent key are painted like SPEC 3",
           SpecColors(kSpecDark, kAccentDark2), PaintedColors(w));
        w.OnFlyoutCommand(app::kCmdThemeLight, 0);
        Eq(L"Light: the radio items", L"Light,2 (default)", CheckedRadios(m));
        True(L"Light: the window is light", !w.IsDark());
        Eq(L"Light: painted like SPEC 3", SpecColors(kSpecLight, kAccentLight1), PaintedColors(w));
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        FakeWindows(true);
        w.OnFlyoutCommand(app::kCmdThemeLight, 0);
        Eq(L"Light on a dark Windows: painted like the light theme of SPEC 3", SpecColors(kSpecLight, kAccentLight1), PaintedColors(w));
        w.OnFlyoutCommand(app::kCmdThemeAuto, 0);
        Eq(L"Auto: the radio items", L"Auto (follow Windows),2 (default)", CheckedRadios(m));
        True(L"Auto on a dark Windows: the window is dark at once", w.IsDark());
        Eq(L"Auto on a dark Windows: painted like the dark theme of SPEC 3", SpecColors(kSpecDark, kAccentDark2), PaintedColors(w));
        FakeWindows(false);
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        w.OnFlyoutCommand(app::kCmdThemeAuto, 0);
        True(L"Auto on a light Windows: the window is light at once", !w.IsDark());
        Eq(L"Auto on a light Windows: painted like the light theme of SPEC 3", SpecColors(kSpecLight, kAccentLight1), PaintedColors(w));

        // Choosing a theme leaves the decimal places alone and the other way round.
        w.SetDecimalPlaces(4);
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        Eq(L"the radio items of both groups", L"Dark,4", CheckedRadios(m));
        w.OnFlyoutCommand(3, 0);
        Eq(L"choosing a number of places does not touch the theme", L"Dark,3", CheckedRadios(m));
        True(L"... the window is still dark", w.IsDark());
        // The menu and the other popups are re-themed with the window: what they paint is the new theme.
        w.CopyFlyout();
        w.FieldMenuFlyout();
        w.TipFlyout();
        w.OnFlyoutCommand(app::kCmdThemeLight, 0);
        const theme::Palette lightPal = theme::Create(false, kAccentLight1), darkPal = theme::Create(true, kAccentDark2);
        True(L"the menu, the Copy menu, the field menu and the tool tip have the light palette",
             m.Palette().flyout == lightPal.flyout && w.CopyFlyout().Palette().flyout == lightPal.flyout &&
                 w.FieldMenuFlyout().Palette().flyout == lightPal.flyout && w.TipFlyout().Palette().flyout == lightPal.flyout);
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        True(L"... and the dark palette after Dark",
             m.Palette().flyout == darkPal.flyout && w.CopyFlyout().Palette().flyout == darkPal.flyout &&
                 w.FieldMenuFlyout().Palette().flyout == darkPal.flyout && w.TipFlyout().Palette().flyout == darkPal.flyout);
        // The menu paints that palette: its background and its text, read from its pixels.
        m.Layout(nullptr, 1.0f);
        gfx::Canvas c;
        c.Resize(400, 500);
        c.Clear(0xFF808080);
        m.ComposeOver(c, 20, 20);
        True(L"the dark menu paints the dark flyout colour", PixelOf(c, 20 + 100, 20 + 5) == darkPal.flyout);
        w.OnFlyoutCommand(app::kCmdThemeLight, 0);
        c.Clear(0xFF808080);
        m.ComposeOver(c, 20, 20);
        True(L"the light menu paints the light flyout colour", PixelOf(c, 20 + 100, 20 + 5) == lightPal.flyout);
    }
    RealWindows();
}

// Auto follows Windows live; Light and Dark ignore the app theme of Windows but follow its accent colour. Windows is simulated
// (theme::SetSystemForTest): nothing of the machine's settings is touched. The window procedure gets the message that Windows sends.
static void ThemeFollowsWindows() {
    const gfx::Argb greenLight = gfx::Rgb(0x10, 0x7C, 0x10), greenDark = gfx::Rgb(0x6C, 0xCB, 0x5F);
    FakeWindows(false);
    MainWindow w(nullptr);
    True(L"the real window is created", w.Create(g_inst, true));
    HWND h = w.hwnd();
    auto settingChange = [&]() { SendMessageW(h, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet"); };
    True(L"the window starts with the light theme of Windows", !w.IsDark() && w.ThemeChoice() == hc::settings::ThemeMode::Auto);
    True(L"... its title bar is set to it (DWM)", CaptionIs(h, kSpecLight));

    // Auto follows the theme and the accent.
    FakeWindows(true);
    settingChange();
    True(L"Auto follows Windows to the dark theme", w.IsDark());
    Eq(L"... and paints it", SpecColors(kSpecDark, kAccentDark2), PaintedColors(w));
    True(L"... the title bar follows (DWM)", CaptionIs(h, kSpecDark));
    FakeWindows(true, greenLight, greenDark);
    settingChange();
    True(L"Auto follows the accent of Windows (dark theme: light 2)", w.IsDark() && w.Palette().accent == greenDark);
    Eq(L"... and paints it", SpecColors(kSpecDark, greenDark), PaintedColors(w));
    FakeWindows(false, greenLight, greenDark);
    settingChange();
    True(L"Auto follows Windows back to the light theme (accent: dark 1)", !w.IsDark() && w.Palette().accent == greenLight);
    True(L"... the title bar follows (DWM)", CaptionIs(h, kSpecLight));

    // Light ignores the dark theme of Windows, but follows the accent.
    FakeWindows(false);
    settingChange();
    w.OnFlyoutCommand(app::kCmdThemeLight, 0);
    FakeWindows(true);
    settingChange();
    True(L"Light ignores Windows going dark", !w.IsDark());
    Eq(L"... and keeps painting the light theme", SpecColors(kSpecLight, kAccentLight1), PaintedColors(w));
    True(L"... the title bar stays light (DWM)", CaptionIs(h, kSpecLight));
    FakeWindows(true, greenLight, greenDark);
    settingChange();
    True(L"Light follows the accent of Windows (the light theme uses dark 1)", !w.IsDark() && w.Palette().accent == greenLight);
    Eq(L"... and paints it", SpecColors(kSpecLight, greenLight), PaintedColors(w));
    FakeWindows(false, greenLight, greenDark);
    settingChange();
    True(L"Light stays light when Windows goes light", !w.IsDark() && w.Palette().accent == greenLight);

    // Dark ignores the light theme of Windows, but follows the accent.
    w.OnFlyoutCommand(app::kCmdThemeDark, 0);
    True(L"Dark: the window is dark although Windows is light, with the accent of the dark theme", w.IsDark() && w.Palette().accent == greenDark);
    True(L"... the title bar is dark (DWM)", CaptionIs(h, kSpecDark));
    FakeWindows(false);
    settingChange();
    True(L"Dark follows the accent of Windows (light 2)", w.IsDark() && w.Palette().accent == kAccentDark2);
    Eq(L"... and paints it", SpecColors(kSpecDark, kAccentDark2), PaintedColors(w));
    FakeWindows(true, greenLight, greenDark);
    settingChange();
    True(L"Dark stays dark when Windows goes dark", w.IsDark() && w.Palette().accent == greenDark);
    True(L"... the title bar stays dark (DWM)", CaptionIs(h, kSpecDark));
    // Other messages that Windows sends when something changes (theme changed, colourisation) are handled the same way.
    FakeWindows(false, greenLight, greenDark);
    SendMessageW(h, WM_THEMECHANGED, 0, 0);
    True(L"Dark ignores WM_THEMECHANGED with a light Windows too", w.IsDark());

    // Back to Auto: the window takes the theme and the accent of Windows at once.
    FakeWindows(true, greenLight, greenDark);
    w.OnFlyoutCommand(app::kCmdThemeLight, 0);
    w.OnFlyoutCommand(app::kCmdThemeAuto, 0);
    True(L"Auto after Light: the window follows Windows again, at once (dark, its accent)", w.IsDark() && w.Palette().accent == greenDark);
    True(L"... the title bar too (DWM)", CaptionIs(h, kSpecDark));
    FakeWindows(false, greenLight, greenDark);
    settingChange();
    True(L"... and keeps following", !w.IsDark() && w.Palette().accent == greenLight);
    // A message that changes nothing changes nothing (and costs no repaint of the popups).
    settingChange();
    True(L"the same Windows theme again: nothing changes", !w.IsDark() && w.Palette().accent == greenLight);
    RealWindows();
}

// The settings file: both settings, whichever changes; read at startup; invalid values mean Auto.
static void ThemePersistence() {
    FakeWindows(false);
    std::wstring ini = TempFile(L".ini");
    {
        MainWindow w(ini.c_str());
        w.MenuFlyout();
        True(L"no settings file: the choice is Auto", w.ThemeChoice() == hc::settings::ThemeMode::Auto);
        True(L"... and none is written by creating the window and its menu", !FileExists(ini));
        w.OnFlyoutCommand(app::kCmdThemeAuto, 0);
        True(L"choosing the current theme writes nothing", !FileExists(ini));
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        Eq(L"choosing Dark writes both settings", L"DecimalPlaces=2\r\nTheme=dark", ReadTextFile(ini));
        w.SetDecimalPlaces(4);
        Eq(L"changing the places keeps the theme", L"DecimalPlaces=4\r\nTheme=dark", ReadTextFile(ini));
        w.OnFlyoutCommand(app::kCmdThemeLight, 0);
        Eq(L"changing the theme keeps the places", L"DecimalPlaces=4\r\nTheme=light", ReadTextFile(ini));
        w.OnFlyoutCommand(6, 0);
        Eq(L"... again the places (6)", L"DecimalPlaces=6\r\nTheme=light", ReadTextFile(ini));
        w.OnFlyoutCommand(app::kCmdThemeAuto, 0);
        Eq(L"choosing Auto writes theme=auto", L"DecimalPlaces=6\r\nTheme=auto", ReadTextFile(ini));
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        DeleteFileW(ini.c_str());
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        True(L"choosing the current theme (Dark) writes nothing", !FileExists(ini));
        w.SetDecimalPlaces(6);
        True(L"... and neither does the current number of places", !FileExists(ini));
    }

    // A change is on the screen before the settings file is written: the write can take a while (a busy disk) and must not hold up
    // what the user just chose.
    {
        DeleteFileW(ini.c_str());
        MainWindow w(ini.c_str());
        int shown = w.RepaintNowCount(), saves = w.SavesCount();
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        Eq(L"choosing a theme paints the window at once", I(shown + 1), I(w.RepaintNowCount()));
        Eq(L"... before the settings file is written (it was written once)", I(shown + 1) + L" " + I(saves + 1), I(w.RepaintNowCountAtLastSave()) + L" " + I(w.SavesCount()));
        shown = w.RepaintNowCount();
        saves = w.SavesCount();
        w.SetDecimalPlaces(4);
        Eq(L"changing the number of places paints the window at once", I(shown + 1), I(w.RepaintNowCount()));
        Eq(L"... before the settings file is written (it was written once)", I(shown + 1) + L" " + I(saves + 1), I(w.RepaintNowCountAtLastSave()) + L" " + I(w.SavesCount()));
        shown = w.RepaintNowCount();
        saves = w.SavesCount();
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        w.SetDecimalPlaces(4);
        Eq(L"the current theme and the current places: no repaint and no write", I(shown) + L" " + I(saves), I(w.RepaintNowCount()) + L" " + I(w.SavesCount()));
        DeleteFileW(ini.c_str());
    }

    // Two windows on one file (two HourCalc programs): changing one setting keeps what the other window saved, for the other setting.
    {
        DeleteFileW(ini.c_str());
        MainWindow a(ini.c_str()), b(ini.c_str());
        a.OnFlyoutCommand(app::kCmdThemeDark, 0);
        Eq(L"window A picks Dark", L"DecimalPlaces=2\r\nTheme=dark", ReadTextFile(ini));
        b.SetDecimalPlaces(4);
        Eq(L"window B (which started with Auto) picks 4 places: the Dark of A stays", L"DecimalPlaces=4\r\nTheme=dark", ReadTextFile(ini));
        a.OnFlyoutCommand(app::kCmdThemeLight, 0);
        Eq(L"window A (which still has 2 places) picks Light: the 4 places of B stay", L"DecimalPlaces=4\r\nTheme=light", ReadTextFile(ini));
        b.SetDecimalPlaces(6);
        Eq(L"window B picks 6 places: the Light of A stays", L"DecimalPlaces=6\r\nTheme=light", ReadTextFile(ini));
        True(L"each window keeps its own state for the session", a.DecimalPlaces() == 2 && a.ThemeChoice() == hc::settings::ThemeMode::Light &&
             b.DecimalPlaces() == 6 && b.ThemeChoice() == hc::settings::ThemeMode::Auto);
        DeleteFileW(ini.c_str());
    }

    // A settings file that is locked when the program starts (an editor, a virus scanner) gives the defaults for that session, but it
    // is not lost: a later change does not write those defaults over what is on disk.
    {
        WriteTextFile(ini, "DecimalPlaces=5\r\nTheme=dark\r\n");
        HANDLE lock = CreateFileW(ini.c_str(), GENERIC_READ, 0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        True(L"the settings file is locked", lock != INVALID_HANDLE_VALUE);
        MainWindow late(ini.c_str());
        True(L"a locked file at start-up: the defaults for this session", late.DecimalPlaces() == 2 && late.ThemeChoice() == hc::settings::ThemeMode::Auto);
        late.OnFlyoutCommand(app::kCmdThemeLight, 0);
        True(L"... a change while it is locked still applies", !late.IsDark() && late.ThemeChoice() == hc::settings::ThemeMode::Light);
        CloseHandle(lock);
        Eq(L"... and wrote nothing", L"DecimalPlaces=5\r\nTheme=dark", ReadTextFile(ini));
        late.OnFlyoutCommand(app::kCmdThemeDark, 0);
        Eq(L"the next change, when the file can be read: the 5 places of the file, not the 2 of the failed read", L"DecimalPlaces=5\r\nTheme=dark", ReadTextFile(ini));
        late.OnFlyoutCommand(app::kCmdThemeAuto, 0);
        Eq(L"... and again", L"DecimalPlaces=5\r\nTheme=auto", ReadTextFile(ini));
        DeleteFileW(ini.c_str());
    }

    // The first save makes the folder (SPEC 6c: %LOCALAPPDATA%\HourCalc is not there on a first run), one level.
    {
        std::wstring root = TempFile(L"");
        std::wstring folder = root + L"\\HourCalc", file = folder + L"\\HourCalc.ini";
        True(L"a test folder to stand for %LOCALAPPDATA%", CreateDirectoryW(root.c_str(), nullptr) != 0);
        {
            MainWindow w(file.c_str());
            True(L"a window on a folder that is not there makes nothing", !FileExists(folder));
            w.OnFlyoutCommand(app::kCmdThemeDark, 0);
            Eq(L"the first change makes the folder and the file", L"DecimalPlaces=2\r\nTheme=dark", ReadTextFile(file));
            w.SetDecimalPlaces(3);
            Eq(L"... the next change uses them", L"DecimalPlaces=3\r\nTheme=dark", ReadTextFile(file));
        }
        MainWindow next(file.c_str());
        True(L"the next window reads it from there", next.DecimalPlaces() == 3 && next.ThemeChoice() == hc::settings::ThemeMode::Dark);
        DeleteFileW(file.c_str());
        RemoveDirectoryW(folder.c_str());
        RemoveDirectoryW(root.c_str());
    }

    // A new window reads both, and a forced theme is the theme although Windows says otherwise.
    hc::settings::Save(ini, 5, hc::settings::ThemeMode::Dark);
    {
        MainWindow next(ini.c_str());
        Eq(L"the next window reads the places", L"5", I(next.DecimalPlaces()));
        Eq(L"... and the theme choice", L"2", I((int)next.ThemeChoice()));
        True(L"... it starts dark on a light Windows", next.IsDark() && next.Palette().bg == gfx::Rgb(0x20, 0x20, 0x20));
        Eq(L"... its menu shows both", L"Dark,5", CheckedRadios(next.MenuFlyout()));
        Eq(L"... and paints the dark theme", SpecColors(kSpecDark, kAccentDark2), PaintedColors(next));
        next.OnFlyoutCommand(app::kCmdThemeLight, 0);
        Eq(L"changing the theme of a window that started with a file rewrites the places it read", L"DecimalPlaces=5\r\nTheme=light", ReadTextFile(ini));
    }
    FakeWindows(true);
    {
        MainWindow next(ini.c_str());
        True(L"a file with Light: the window starts light on a dark Windows", !next.IsDark() && next.Palette().bg == gfx::Rgb(0xF0, 0xF4, 0xF7));
        Eq(L"... its menu shows it", L"Light,5", CheckedRadios(next.MenuFlyout()));
    }
    {
        MainWindow next(nullptr);
        True(L"no settings file at all: a window follows Windows (dark)", next.IsDark() && next.ThemeChoice() == hc::settings::ThemeMode::Auto);
    }
    FakeWindows(false);

    // What a hand-written or damaged file gives.
    struct Case {
        const char* text;
        int places;
        int choice;
    };
    const Case cases[] = {
        { "Theme=blue\r\n", 2, 0 },
        { "Theme=DARK\r\n", 2, 2 },
        { "theme = light", 2, 1 },
        { "THEME=Auto", 2, 0 },
        { "DecimalPlaces=5\r\n", 5, 0 },
        { "Theme=dark\r\nTheme=light\r\n", 2, 2 },
        { "Theme=blue\r\nTheme=dark\r\n", 2, 0 },
        { "Theme=\r\n", 2, 0 },
        { "Theme=darker", 2, 0 },
        { "Theme=dark light", 2, 0 },
        { "Theme", 2, 0 },
        { "; Theme=dark\r\n", 2, 0 },
        { "garbage", 2, 0 },
        { "", 2, 0 },
        { "DecimalPlaces=9\r\nTheme=light", 2, 1 },
        { "DecimalPlaces=3\r\nTheme=nonsense", 3, 0 },
        { "; settings\r\n[HourCalc]\r\nTheme=Dark\r\nDecimalPlaces=3\r\n", 3, 2 },
        { "\xEF\xBB\xBF" "Theme=dark", 2, 2 },
    };
    for (const Case& c : cases) {
        WriteTextFile(ini, c.text);
        MainWindow next(ini.c_str());
        std::wstring what = L"the file <";
        for (const char* p = c.text; *p; p++) what += *p == '\r' ? L'\\' : (*p == '\n' ? L'/' : (wchar_t)(unsigned char)*p);
        what += L">";
        Eq(what + L": places", I(c.places), I(next.DecimalPlaces()));
        Eq(what + L": theme choice", I(c.choice), I((int)next.ThemeChoice()));
        True(what + L": the window is " + (c.choice == 2 ? L"dark" : L"light") + L" (Windows is light)", next.IsDark() == (c.choice == 2));
    }
    DeleteFileW(ini.c_str());

    // A folder that cannot be written to: the choice still applies for the session.
    MainWindow nowhere((TempFile(L"") + L"\\nope\\HourCalc.ini").c_str());      // the folder cannot be made: its parent is not there either
    nowhere.OnFlyoutCommand(app::kCmdThemeDark, 0);
    True(L"an unwritable folder: the theme still applies this session", nowhere.IsDark() && nowhere.ThemeChoice() == hc::settings::ThemeMode::Dark);
    nowhere.OnFlyoutCommand(app::kCmdThemeLight, 0);
    True(L"... and changes again", !nowhere.IsDark());
    MainWindow plain(nullptr);
    plain.OnFlyoutCommand(app::kCmdThemeDark, 0);
    True(L"a window without a settings file takes the choice for the session", plain.IsDark());
    RealWindows();
}

// The menu is tall now: when it does not fit below the button on the work area of the monitor, it moves up so that it stays fully
// visible. (A work area is given to the position function: no popup is shown.)
static void ThemeMenuPosition() {
    // The rule itself.
    {
        const RECT work = { 100, 50, 1100, 850 };
        POINT p = flyout::Flyout::Fit(300, 200, 236, 441, work);
        True(L"a box that fits stays where it is", p.x == 300 && p.y == 200);
        p = flyout::Flyout::Fit(300, 500, 236, 441, work);
        True(L"a box that leaves at the bottom moves up until its bottom edge is the work area's", p.x == 300 && p.y == 850 - 441);
        p = flyout::Flyout::Fit(300, 409, 236, 441, work);
        True(L"a box that ends exactly at the bottom stays", p.y == 409);
        p = flyout::Flyout::Fit(300, 410, 236, 441, work);
        True(L"... one pixel more moves it up by one", p.y == 409);
        p = flyout::Flyout::Fit(950, 200, 236, 441, work);
        True(L"a box that leaves on the right moves left", p.x == 1100 - 236 && p.y == 200);
        p = flyout::Flyout::Fit(50, 20, 236, 441, work);
        True(L"a box that leaves on the left or at the top moves in", p.x == 100 && p.y == 50);
        const RECT tiny = { 0, 0, 800, 300 };
        p = flyout::Flyout::Fit(20, 100, 236, 441, tiny);
        True(L"a box taller than the work area starts at its top edge", p.y == 0 && p.x == 20);
    }
    MainWindow w(nullptr);
    True(L"real window created for the menu position", w.Create(g_inst, true));
    flyout::Flyout& m = w.MenuFlyout();
    const RECT huge = { -100000, -100000, 100000, 100000 };
    POINT base = w.MenuPosition(&huge);
    const app::Layout& L = w.GetLayout();
    POINT want = { (LONG)L.menuBtn.x, (LONG)L.menuBtn.bottom() };
    ClientToScreen(w.hwnd(), &want);
    want.x += (LONG)Rn(4.0f * L.d);
    want.y += (LONG)Rn(2.0f * L.d);
    True(L"the menu opens below its button, 4 px in and 2 px down", base.x == want.x && base.y == want.y);
    const int bw = m.BoxWidth(), bh = m.BoxHeight();
    True(L"the menu box is as tall as 441 px at 100 % (" + I(bh) + L" px at this scale)", L.d != 1.0f || bh == 441);
    {
        RECT roomy = { base.x - 50, base.y - 50, base.x + bw + 50, base.y + bh + 100 };
        POINT p = w.MenuPosition(&roomy);
        True(L"enough room below the button: the menu is not moved", p.x == base.x && p.y == base.y);
        RECT exact = { base.x - 50, base.y - 50, base.x + bw + 50, base.y + bh };
        p = w.MenuPosition(&exact);
        True(L"a work area that ends exactly at the bottom of the menu: not moved", p.x == base.x && p.y == base.y);
        RECT shortBy50 = { base.x - 50, base.y - 200, base.x + bw + 50, base.y + bh - 50 };
        p = w.MenuPosition(&shortBy50);
        True(L"a work area 50 px too short: the menu moves up by 50 px", p.x == base.x && p.y == base.y - 50);
        True(L"... and lies inside the work area", p.y >= shortBy50.top && p.y + bh <= shortBy50.bottom && p.x >= shortBy50.left && p.x + bw <= shortBy50.right);
        RECT narrow = { base.x - 100, base.y - 50, base.x + bw - 30, base.y + bh + 100 };
        p = w.MenuPosition(&narrow);
        True(L"a work area 30 px too narrow: the menu moves left by 30 px", p.x == base.x - 30 && p.y == base.y);
        RECT squat = { base.x - 100, base.y - 20, base.x + bw + 100, base.y + bh - 100 };
        p = w.MenuPosition(&squat);
        True(L"a work area smaller than the menu: the menu starts at its top edge", p.y == squat.top);
    }
    // The real work area of the monitor of this window (no popup is opened).
    POINT real = w.MenuPosition();
    MONITORINFO mi = { sizeof mi };
    GetMonitorInfoW(MonitorFromPoint(base, MONITOR_DEFAULTTONEAREST), &mi);
    int workH = mi.rcWork.bottom - mi.rcWork.top;
    True(L"on the real monitor the menu lies inside its work area, unless it is taller than it",
         real.x >= mi.rcWork.left && real.x + bw <= mi.rcWork.right && (bh > workH ? real.y == mi.rcWork.top : (real.y >= mi.rcWork.top && real.y + bh <= mi.rcWork.bottom)));
    True(L"... and is moved only when it had to be", (base.y + bh <= mi.rcWork.bottom && base.x + bw <= mi.rcWork.right) ? (real.x == base.x && real.y == base.y) : true);
}

// The About and license windows follow a change of the theme while they are open (a Dialog is registered while it exists).
static void ThemeDialogs() {
    FakeWindows(false);
    {
        MainWindow w(nullptr);
        True(L"real window created for the dialog theme test", w.Create(g_inst, true));
        const theme::Palette lightPal = theme::Create(false, kAccentLight1), darkPal = theme::Create(true, kAccentDark2);
        dialogs::Dialog about(dialogs::Dialog::About, lightPal, 1.0f, g_inst);
        dialogs::Dialog lic(dialogs::Dialog::License, lightPal, 1.0f, g_inst);
        True(L"About window created (not shown)", about.CreateWindowFor(nullptr));
        True(L"license window created (not shown)", lic.CreateWindowFor(nullptr));
        True(L"both start light (title bars set by DWM)", CaptionIs(about.hwnd(), kSpecLight) && CaptionIs(lic.hwnd(), kSpecLight));
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        True(L"Dark: the About window has the dark palette", about.Palette().bg == darkPal.bg && about.Palette().accent == darkPal.accent);
        True(L"Dark: the license window has the dark palette", lic.Palette().bg == darkPal.bg && lic.Palette().text == darkPal.text);
        True(L"Dark: both title bars are set to the dark theme (DWM)", CaptionIs(about.hwnd(), kSpecDark) && CaptionIs(lic.hwnd(), kSpecDark));
        gfx::Canvas c;
        about.Render(c);
        True(L"Dark: the About window paints the dark background", PixelOf(c, 2, 2) == darkPal.bg);
        gfx::Canvas c2;
        lic.Render(c2);
        True(L"Dark: the license window paints the dark background", PixelOf(c2, 2, 2) == darkPal.bg);
        w.OnFlyoutCommand(app::kCmdThemeLight, 0);
        about.Render(c);
        True(L"Light: the About window is light again", about.Palette().bg == lightPal.bg && PixelOf(c, 2, 2) == lightPal.bg && CaptionIs(about.hwnd(), kSpecLight));
        // A system theme change reaches them in Auto mode, too.
        w.OnFlyoutCommand(app::kCmdThemeAuto, 0);
        FakeWindows(true);
        SendMessageW(w.hwnd(), WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
        True(L"Auto: Windows goes dark while the About window is open: the window follows", about.Palette().bg == darkPal.bg && CaptionIs(about.hwnd(), kSpecDark));
    }
    // A dialog that is gone is not remembered: a theme change afterwards touches nothing that is not there.
    {
        MainWindow w(nullptr);
        { dialogs::Dialog gone(dialogs::Dialog::About, theme::Create(false, kAccentLight1), 1.0f, g_inst); }
        w.OnFlyoutCommand(app::kCmdThemeDark, 0);
        True(L"a theme change after a dialog was closed is harmless", w.IsDark());
    }
    RealWindows();
}

// A click inside the menu box that is not on an enabled item (the header, a label, a separator, the padding, a greyed out item, a
// right click) does nothing and the menu stays open, as the WPF popup (StaysOpen = false) does: only a click outside the box closes it.
// The popups are marked open without being shown (Flyout::OpenHiddenForTest) and get their clicks as window messages.
static void FlyoutClicks() {
    FakeWindows(false);
    MainWindow w(nullptr);
    True(L"real window created for the popup clicks", w.Create(g_inst, true));
    const RECT huge = { -100000, -100000, 100000, 100000 };
    flyout::Flyout& m = w.MenuFlyout();
    w.MenuPosition(&huge);                                     // lays the menu out
    flyout::Flyout& cp = w.CopyFlyout();
    cp.Layout(w.hwnd(), w.Scale());
    auto drain = [&]() {
        MSG msg;
        int n = 0;
        while (PeekMessageW(&msg, w.hwnd(), flyout::WM_FLYOUT_COMMAND, flyout::WM_FLYOUT_CLOSED, PM_REMOVE)) n++;
        return n;
    };
    auto at = [](flyout::Flyout& f, UINT msg, int bx, int by) {
        WPARAM keys = msg == WM_RBUTTONDOWN ? MK_RBUTTON : MK_LBUTTON;
        SendMessageW(f.hwnd(), msg, keys, MAKELPARAM(f.ShadowRoom() + bx, f.ShadowRoom() + by));
    };
    auto find = [](flyout::Flyout& f, flyout::Item::Type type, const wchar_t* text) -> const flyout::Item* {
        for (const flyout::Item& it : f.Items())
            if (it.type == type && (!text || it.text == text)) return &it;
        return nullptr;
    };
    auto middle = [](const flyout::Item& it) { return (int)(it.rect.y + it.rect.h / 2); };

    m.OpenHiddenForTest(w.hwnd(), 0, 0);
    True(L"the menu is open for the test and nothing is shown", m.IsOpen() && m.hwnd() && !IsWindowVisible(m.hwnd()));
    drain();
    const int x = m.BoxWidth() / 2;
    const flyout::Item* header = find(m, flyout::Item::Header, L"Theme");
    const flyout::Item* label = find(m, flyout::Item::Label, L"Decimal places");
    const flyout::Item* sep = find(m, flyout::Item::Separator, nullptr);
    const flyout::Item* dark = find(m, flyout::Item::Radio, L"Dark");
    True(L"the menu has a header, a label, a separator and the Dark item", header && label && sep && dark);
    if (!(header && label && sep && dark)) return;
    struct Spot {
        const wchar_t* what;
        int y;
    };
    const Spot spots[] = { { L"the Theme header", middle(*header) }, { L"the Decimal places label", middle(*label) }, { L"a separator", middle(*sep) },
                           { L"the padding at the top of the box", 2 }, { L"the padding at the bottom of the box", m.BoxHeight() - 3 } };
    for (const Spot& s : spots) {
        at(m, WM_LBUTTONDOWN, x, s.y);
        at(m, WM_LBUTTONUP, x, s.y);
        True(std::wstring(L"a click on ") + s.what + L": the menu stays open", m.IsOpen());
        at(m, WM_RBUTTONDOWN, x, s.y);
        True(std::wstring(L"a right click on ") + s.what + L": the menu stays open", m.IsOpen());
    }
    at(m, WM_RBUTTONDOWN, x, middle(*dark));
    True(L"a right click on an item: the menu stays open", m.IsOpen());
    at(m, WM_RBUTTONUP, x, middle(*dark));
    m.SetEnabled(app::kCmdThemeDark, false);
    at(m, WM_LBUTTONDOWN, x, middle(*dark));
    at(m, WM_LBUTTONUP, x, middle(*dark));
    True(L"a click on a greyed out item: the menu stays open", m.IsOpen());
    m.SetEnabled(app::kCmdThemeDark, true);
    Eq(L"none of these clicks chose anything or closed anything (no command, no closed message)", L"0", I(drain()));
    // Pressing an item and letting go elsewhere chooses nothing either.
    at(m, WM_LBUTTONDOWN, x, middle(*dark));
    at(m, WM_LBUTTONUP, x, middle(*header));
    True(L"pressed on an item, released on the header: the menu stays open", m.IsOpen());
    Eq(L"... and nothing was chosen", L"0", I(drain()));
    // A click on an item still chooses it and closes the menu.
    at(m, WM_LBUTTONDOWN, x, middle(*dark));
    at(m, WM_LBUTTONUP, x, middle(*dark));
    True(L"a click on Dark: the menu closes", !m.IsOpen());
    MSG msg;
    True(L"... and the command is sent to the window", PeekMessageW(&msg, w.hwnd(), flyout::WM_FLYOUT_COMMAND, flyout::WM_FLYOUT_COMMAND, PM_REMOVE) && (int)msg.wParam == app::kCmdThemeDark);
    drain();
    // Outside the box closes it (far away from every window, so that no click goes through to a window of the test).
    m.OpenHiddenForTest(w.hwnd(), 0, 0);
    at(m, WM_LBUTTONDOWN, 30000, 30000);
    True(L"a click outside the box: the menu closes", !m.IsOpen());
    m.OpenHiddenForTest(w.hwnd(), 0, 0);
    at(m, WM_RBUTTONDOWN, 30000, 30000);
    True(L"a right click outside the box: the menu closes", !m.IsOpen());
    drain();

    // The Copy menu has one item and the same padding.
    cp.OpenHiddenForTest(w.hwnd(), 0, 0);
    at(cp, WM_LBUTTONDOWN, cp.BoxWidth() / 2, 2);
    at(cp, WM_LBUTTONUP, cp.BoxWidth() / 2, 2);
    True(L"the Copy menu: a click on its padding leaves it open", cp.IsOpen());
    at(cp, WM_LBUTTONDOWN, cp.BoxWidth() / 2, cp.BoxHeight() / 2);
    at(cp, WM_LBUTTONUP, cp.BoxWidth() / 2, cp.BoxHeight() / 2);
    True(L"... a click on Copy closes it", !cp.IsOpen());
    drain();
    RealWindows();
}

// A change of the theme reaches a popup that is open at once (it is painted again), and costs a closed one nothing; a message from
// Windows that changes nothing (the same theme and accent) costs no repaint at all. The colourisation message (the accent) is
// handled like the others. Windows is simulated; the popups are marked open without being shown.
static void ThemeRepaints() {
    const gfx::Argb greenLight = gfx::Rgb(0x10, 0x7C, 0x10), greenDark = gfx::Rgb(0x6C, 0xCB, 0x5F);
    FakeWindows(false);
    MainWindow w(nullptr);
    True(L"real window created for the repaint test", w.Create(g_inst, true));
    HWND h = w.hwnd();
    const RECT huge = { -100000, -100000, 100000, 100000 };
    flyout::Flyout& m = w.MenuFlyout();
    w.MenuPosition(&huge);
    flyout::Flyout& cp = w.CopyFlyout();
    cp.Layout(h, w.Scale());
    m.OpenHiddenForTest(h, 0, 0);                              // open; the Copy menu stays closed
    const theme::Palette darkPal = theme::Create(true, kAccentDark2), lightPal = theme::Create(false, kAccentLight1);

    int presents = m.Presents(), copyPresents = cp.Presents(), invalidates = w.InvalidateCount();
    FakeWindows(true);
    SendMessageW(h, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
    True(L"Windows goes dark: the window follows", w.IsDark());
    True(L"... the window was invalidated", w.InvalidateCount() > invalidates);
    True(L"... the open menu was painted again at once", m.Presents() > presents);
    True(L"... with the dark palette", m.Palette().flyout == darkPal.flyout);
    True(L"... the closed Copy menu was not painted (it has the new palette for when it opens)", cp.Presents() == copyPresents && cp.Palette().flyout == darkPal.flyout);

    // Nothing changed: no repaint of the window and none of the popups, whichever message says so.
    presents = m.Presents();
    invalidates = w.InvalidateCount();
    SendMessageW(h, WM_SETTINGCHANGE, 0, (LPARAM)L"ImmersiveColorSet");
    SendMessageW(h, WM_SETTINGCHANGE, 0, 0);
    SendMessageW(h, WM_THEMECHANGED, 0, 0);
    SendMessageW(h, 0x0320 /* WM_DWMCOLORIZATIONCOLORCHANGED */, 0, 0);
    Eq(L"the same theme and accent again: the window is not invalidated", I(invalidates), I(w.InvalidateCount()));
    Eq(L"... and the open menu is not painted again", I(presents), I(m.Presents()));

    // The colourisation message: the accent changed, the theme did not.
    FakeWindows(true, greenLight, greenDark);
    SendMessageW(h, 0x0320 /* WM_DWMCOLORIZATIONCOLORCHANGED */, 0, 0);
    True(L"a new accent with the colourisation message: the window has it", w.Palette().accent == greenDark && w.IsDark());
    Eq(L"... and paints it", SpecColors(kSpecDark, greenDark), PaintedColors(w));
    True(L"... the window was invalidated and the open menu painted again", w.InvalidateCount() > invalidates && m.Presents() > presents);
    FakeWindows(false, greenLight, greenDark);
    SendMessageW(h, 0x0320, 0, 0);
    True(L"... and a new theme with it: the window follows (Auto)", !w.IsDark() && w.Palette().accent == greenLight);

    // Choosing a theme from the menu: the open popup follows as well (the menu closes on the click; the Copy menu may be open).
    cp.OpenHiddenForTest(h, 0, 0);
    copyPresents = cp.Presents();
    presents = m.Presents();
    w.OnFlyoutCommand(app::kCmdThemeDark, 0);
    True(L"choosing Dark: the open Copy menu is painted again, dark", cp.Presents() > copyPresents && cp.Palette().flyout == darkPal.flyout);
    True(L"... and so is the open menu", m.Presents() > presents);
    copyPresents = cp.Presents();
    invalidates = w.InvalidateCount();
    w.OnFlyoutCommand(app::kCmdThemeDark, 0);
    True(L"choosing Dark again changes nothing: no repaint", cp.Presents() == copyPresents && w.InvalidateCount() == invalidates);
    (void)lightPal;
    RealWindows();
}

static void AboutAndLicense() {
    theme::Palette pal = theme::Create(false, theme::SystemAccent(false));
    dialogs::Dialog about(dialogs::Dialog::About, pal, 1.0f, g_inst);
    about.Layout(440, 0);
#ifdef HC_NO_EMBEDDED_SOURCE
    // The variant without the embedded source archive: no "Save source code...", and one more line under the notice (the address).
    Eq(L"About natural height at 440 px (with the source address line)", L"311", I(about.NaturalHeight()));
#else
    Eq(L"About natural height at 440 px", L"284", I(about.NaturalHeight()));
#endif
    std::wstring buttons;
    for (auto& b : about.ButtonTexts()) buttons += (buttons.empty() ? L"" : L"|") + b;
#ifdef HC_NO_EMBEDDED_SOURCE
    Eq(L"About buttons", L"View license|Close", buttons);
#else
    Eq(L"About buttons", L"View license|Save source code...|Close", buttons);
#endif
    Eq(L"About product name", L"HourCalc", embedded::kProductName);
    Eq(L"About version", L"1.0.0", embedded::kVersion);
    Eq(L"About copyright", L"Copyright (c) 2026 Optimey CommV", embedded::kCopyright);
    std::wstring notice = embedded::kNotice;
    True(L"About shows the GPL notice (free software, version 3 or later, no warranty)",
         notice.find(L"GNU General Public License") != std::wstring::npos && notice.find(L"either version 3") != std::wstring::npos &&
             notice.find(L"ABSOLUTELY NO WARRANTY") != std::wstring::npos);
#ifdef HC_NO_EMBEDDED_SOURCE
    Eq(L"the address of the source code is the placeholder", L"https://github.com/Optimey-CommV/hourcalc", embedded::kSourceUrl);
#else
    Eq(L"embedded source file name", L"HourCalc-1.0.0-source.zip", embedded::SourceFileName());
    True(L"the build carries its source", about.SourceAvailable());
#endif

    dialogs::Dialog lic(dialogs::Dialog::License, pal, 1.0f, g_inst);
    const std::wstring& text = lic.LicenseText();
    True(L"the license text is the GPL", text.compare(0, 46, L"                    GNU GENERAL PUBLIC LICENSE") == 0 && text.find(L"Version 3, 29 June 2007") != std::wstring::npos);
    int n = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), nullptr, 0, nullptr, nullptr);
    std::string utf8((size_t)n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(), &utf8[0], n, nullptr, nullptr);
    std::string hash = Sha256Hex(utf8.data(), utf8.size());
    Eq(L"the embedded license text is the verbatim FSF text (SHA-256)", L"3972dc9744f6499f0f9b2dbf76696f2ae7ad8af9b23dde66d6af86c9dfb36986", std::wstring(hash.begin(), hash.end()));
    lic.Layout(660, 420);
    gfx::Canvas c;
    lic.Render(c);
    True(L"the license viewer renders", c.W() == 660 && c.H() == 420);
}

#ifdef HC_NO_EMBEDDED_SOURCE
// The variant without the source archive: no archive in the exe, the license comes from its own small resource.
static void EmbeddedSource() {
    const uint8_t* zip = nullptr;
    size_t size = 0;
    True(L"the exe carries no source archive", !embedded::OpenSource(&zip, &size));
    True(L"... and no archive can be saved", !embedded::SaveSource(L"NUL"));
    True(L"the exe has no RCDATA 101", FindResourceW(nullptr, MAKEINTRESOURCEW(101), RT_RCDATA) == nullptr);
    True(L"the license resource (RCDATA 102) exists", FindResourceW(nullptr, MAKEINTRESOURCEW(102), RT_RCDATA) != nullptr);
    std::wstring text;
    True(L"the license text is read from the deflated resource", embedded::ReadLicense(text) && text.size() > 30000);
    True(L"... it is the GPL", text.find(L"GNU GENERAL PUBLIC LICENSE") != std::wstring::npos && text.find(L"END OF TERMS AND CONDITIONS") != std::wstring::npos);
}
#else
static void EmbeddedSource() {
    const uint8_t* zip = nullptr;
    size_t size = 0;
    True(L"the exe carries its source archive", embedded::OpenSource(&zip, &size));
    if (!zip) return;
    std::vector<std::string> names;
    True(L"the archive's directory can be read", embedded::ZipNames(zip, size, names));
    auto has = [&](const char* n) { for (auto& s : names) if (s == n) return true; return false; };
    const char* must[] = { "native/app/app_window.cpp", "native/app/gfx.cpp", "native/app/embedded.cpp", "native/core/hc_core.h", "native/core/timemath.cpp",
                           "native/core/calcengine.cpp", "native/core/converter.cpp", "native/core/settings.cpp", "native/app/app.manifest", "native/app/app.rc",
                           "tools/make_ico.py", "tools/msvc.ps1", "tools/cflags.rsp", "assets/icon/app.ico", "assets/icon/app.svg", "assets/icon/app-16.svg",
                           "assets/icon/app-24.svg", "build.ps1", "build-core.ps1", "build-app.ps1", "README.md", "LICENSE" };
    for (const char* m : must) True(std::wstring(L"source archive contains ") + std::wstring(m, m + strlen(m)), has(m));
    bool bad = false;
    for (auto& n : names) {
        if (n.compare(0, 5, "logos") == 0 || n.compare(0, 4, "docs") == 0 || n.compare(0, 5, "tests") == 0 || n.compare(0, 4, "dist") == 0 || n.compare(0, 3, "obj") == 0 ||
            n.compare(0, 6, "legacy") == 0 || n.compare(0, 6, "oracle") == 0 || (n.size() > 4 && n.compare(n.size() - 4, 4, ".exe") == 0) || n.find('\\') != std::string::npos)
            bad = true;
    }
    True(L"source archive has no logos, documents, tests, binaries or backslash paths", !bad);
    bool cli = false;
    for (auto& n : names)
        if (n.compare(0, 11, "native/cli/") == 0) cli = true;
    True(L"source archive has no console front end (native/cli: only the differential test uses it)", !cli);
    std::vector<uint8_t> license;
    True(L"the archive's LICENSE is the full GPL text", embedded::ZipRead(zip, size, "LICENSE", license) && license.size() == 35149);
    std::vector<uint8_t> src;
    bool ok = embedded::ZipRead(zip, size, "native/app/app_window.cpp", src);
    const char* head = "// SPDX-FileCopyrightText: 2026 Optimey CommV\n// SPDX-License-Identifier: GPL-3.0-or-later";
    True(L"an archived source file starts with the SPDX header", ok && src.size() > 90 && memcmp(src.data(), head, strlen(head)) == 0);

    std::wstring tmp = TempFile(L".zip");
    True(L"the source archive can be saved", embedded::SaveSource(tmp.c_str()));
    HANDLE f = CreateFileW(tmp.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    DWORD fileSize = f == INVALID_HANDLE_VALUE ? 0 : GetFileSize(f, nullptr);
    if (f != INVALID_HANDLE_VALUE) CloseHandle(f);
    True(L"... and the saved file is the embedded archive", fileSize == size);
    True(L"... a zip with entries", names.size() >= 15);
    DeleteFileW(tmp.c_str());
}
#endif

// The icon resource (assets/icon/app.ico, PNG-compressed frames): every frame loads, through LoadImageW (what the window icon,
// the taskbar and Alt+Tab use) and through CreateIconFromResourceEx on the RT_ICON data, at the size it was made for.
static void IconFrames() {
    const int sizes[] = { 16, 20, 24, 32, 40, 48, 64, 256 };
    HRSRC grp = FindResourceW(g_inst, MAKEINTRESOURCEW(1), RT_GROUP_ICON);
    True(L"the exe has a group icon resource", grp != nullptr);
    const BYTE* dir = grp ? static_cast<const BYTE*>(LockResource(LoadResource(g_inst, grp))) : nullptr;
    HDC dc = GetDC(nullptr);
    for (int s : sizes) {
        std::wstring at = L" at " + I(s) + L" px";
        HICON ic[2] = { (HICON)LoadImageW(g_inst, MAKEINTRESOURCEW(1), IMAGE_ICON, s, s, LR_DEFAULTCOLOR), nullptr };
        if (dir) {
            int id = LookupIconIdFromDirectoryEx(const_cast<BYTE*>(dir), TRUE, s, s, LR_DEFAULTCOLOR);
            HRSRC fr = id ? FindResourceW(g_inst, MAKEINTRESOURCEW(id), RT_ICON) : nullptr;
            const BYTE* bits = fr ? static_cast<const BYTE*>(LockResource(LoadResource(g_inst, fr))) : nullptr;
            if (bits) ic[1] = CreateIconFromResourceEx(const_cast<BYTE*>(bits), SizeofResource(g_inst, fr), TRUE, 0x00030000, s, s, LR_DEFAULTCOLOR);
        }
        for (int k = 0; k < 2; k++) {
            std::wstring how = std::wstring(k == 0 ? L"LoadImageW" : L"CreateIconFromResourceEx") + at;
            True(how + L" gives an icon", ic[k] != nullptr);
            ICONINFO ii = {};
            BITMAP bm = {};
            std::vector<uint32_t> px;
            bool ok = ic[k] && GetIconInfo(ic[k], &ii) && ii.hbmColor && GetObjectW(ii.hbmColor, sizeof bm, &bm);
            if (ok) {
                BITMAPINFO bi = {};
                bi.bmiHeader.biSize = sizeof bi.bmiHeader;
                bi.bmiHeader.biWidth = bm.bmWidth;
                bi.bmiHeader.biHeight = -bm.bmHeight;
                bi.bmiHeader.biPlanes = 1;
                bi.bmiHeader.biBitCount = 32;
                bi.bmiHeader.biCompression = BI_RGB;
                px.assign((size_t)bm.bmWidth * (size_t)bm.bmHeight, 0);
                ok = GetDIBits(dc, ii.hbmColor, 0, (UINT)bm.bmHeight, px.data(), &bi, DIB_RGB_COLORS) == bm.bmHeight;
            }
            True(how + L" is " + I(s) + L" x " + I(s), ok && bm.bmWidth == s && bm.bmHeight == s);
            size_t opaque = 0;
            for (uint32_t p : px)
                if ((p >> 24) == 0xFF) opaque++;
            // The artwork is a rounded tile: opaque over most of the middle, see-through in the corner.
            True(how + L" has opaque artwork and a transparent corner", opaque > (size_t)(s * s) / 4 && !px.empty() && (px[0] >> 24) == 0);
            if (ii.hbmColor) DeleteObject(ii.hbmColor);
            if (ii.hbmMask) DeleteObject(ii.hbmMask);
            if (ic[k]) DestroyIcon(ic[k]);
        }
    }
    ReleaseDC(nullptr, dc);
}

// ------------------------------------------------------------------ the window behaviour (ReviewTests)

static void WindowTyping() {
    Win wp = New();
    MainWindow& w = *wp;
    app::Field& bl = w.FieldRef(0);
    app::Field& br = w.FieldRef(1);

    w.GotFocus(0, true);
    TypeInto(w, 0, L"8u30");
    Eq(L"left typed 8u30", L"8:30", bl.text);
    Eq(L"left typed 8u30 caret", L"4", I(bl.caret));
    Eq(L"left typed 8u30 right", S(L"8,50"), br.text);
    Eq(L"left typed 8u30 caption", L"8 h 30 min", w.ConverterCaption());
    TypeInto(w, 0, L"5");
    Eq(L"third minute digit rejected, text", L"8:30", bl.text);
    Eq(L"third minute digit rejected, caret", L"4", I(bl.caret));

    w.Press(L"RESET");
    TypeInto(w, 0, L"8.30");
    Eq(L"left typed 8.30", L"8:30", bl.text);
    w.Press(L"RESET");
    TypeInto(w, 0, L"8h30");
    Eq(L"left typed 8h30", L"8:30", bl.text);
    w.Press(L"RESET");
    TypeInto(w, 0, L"8,30");
    Eq(L"left typed 8,30: ',' is ignored", L"830", bl.text);
    Eq(L"left typed 8,30: value", S(L"8,50"), br.text);
    w.Press(L"RESET");
    TypeInto(w, 0, L"8x:3");
    Eq(L"left typed 8x:3: letters ignored", L"8:3", bl.text);
    Eq(L"left typed 8x:3: caret", L"3", I(bl.caret));
    w.Press(L"RESET");
    TypeInto(w, 0, L"12345678");
    Eq(L"left typed 8 digits", L"1234567", bl.text);

    w.Press(L"RESET");
    w.GotFocus(1, true);
    Eq(L"right made active by focus", L"1", I(w.ActiveField()));
    TypeInto(w, 1, L"8.5");
    Eq(L"right typed 8.5", S(L"8,5"), br.text);
    Eq(L"right typed 8.5 caret", L"3", I(br.caret));
    Eq(L"right typed 8.5 left", L"08:30", bl.text);
    w.Press(L"RESET");
    w.GotFocus(1, true);
    TypeInto(w, 1, L"8,5");
    Eq(L"right typed 8,5", S(L"8,5"), br.text);
    Eq(L"right typed 8,5 left", L"08:30", bl.text);
    w.Press(L"RESET");
    w.GotFocus(1, true);
    TypeInto(w, 1, L"8:30");
    Eq(L"right typed 8:30: ':' is ignored", L"830", br.text);
    w.Press(L"RESET");
    w.GotFocus(1, true);
    TypeInto(w, 1, L"1,12345678");
    Eq(L"right typed 8 decimals", S(L"1,123456"), br.text);
    w.Press(L"RESET");
    w.GotFocus(1, true);
    TypeInto(w, 1, L",5");
    Eq(L"right typed ,5", S(L",5"), br.text);
    Eq(L"right typed ,5 left", L"00:30", bl.text);

    w.Press(L"RESET");
    w.GotFocus(0, true);
    TypeInto(w, 0, L"830");
    bl.caret = bl.anchor = 1;
    TypeInto(w, 0, L":");
    Eq(L"':' typed in the middle", L"8:30", bl.text);
    Eq(L"':' typed in the middle caret", L"2", I(bl.caret));
    TypeInto(w, 0, L"5");
    Eq(L"digit in the middle pushes the rest on, 2 minute digits at most", L"8:53", bl.text);
    bl.anchor = 0;
    bl.caret = (int)bl.text.size();
    TypeInto(w, 0, L"9");
    Eq(L"typing over a selection", L"9", bl.text);
    Eq(L"typing over a selection: right", S(L"9,00"), br.text);

    bl.anchor = 0;
    bl.caret = (int)bl.text.size();
    w.TypeText(0, L"");       // deleting the selection
    Eq(L"deleting the selection clears the left", L"", bl.text);
    Eq(L"deleting the selection clears the right", L"", br.text);
    Eq(L"deleting the selection clears the caption", L"", w.ConverterCaption());

    w.Press(L"RESET");
    w.GotFocus(0, true);
    TypeInto(w, 0, L"8:30");
    Eq(L"left source gives the right", S(L"8,50"), br.text);
    w.GotFocus(1, true);
    TypeInto(w, 1, L"9");
    Eq(L"typing over the selected right value", L"9", br.text);
    Eq(L"the left follows the right", L"09:00", bl.text);
    Eq(L"caption follows the right", L"9 h 0 min", w.ConverterCaption());
}

static void WindowConverterState() {
    Win wp = New();
    MainWindow& w = *wp;
    app::Field& bl = w.FieldRef(0);
    app::Field& br = w.FieldRef(1);
    auto expectActive = [&](const wchar_t* step, int field) {
        Eq(std::wstring(step) + L": active field", I(field), I(w.ActiveField()));
        True(std::wstring(step) + L": ':' enabled only for the left field", w.KeyEnabled(L":") == (field == 0));
        True(std::wstring(step) + L": decimal key enabled only for the right field", w.KeyEnabled(L",") == (field == 1));
        True(std::wstring(step) + L": underline left", w.UnderlineVisible(0) == (field == 0));
        True(std::wstring(step) + L": underline right", w.UnderlineVisible(1) == (field == 1));
    };
    expectActive(L"start", 0);
    w.SetConverterText(1, S(L"8,5"));
    expectActive(L"after typing in the right field", 1);
    w.Press(L"CE");
    expectActive(L"after CE", 1);
    w.Press(L"5");
    w.Press(L"BACK");
    expectActive(L"after BACK", 1);
    w.Press(L"CONVERT");
    expectActive(L"after Convert", 1);
    w.SelectTab(1);
    w.Press(L"7");
    w.SelectTab(0);
    expectActive(L"after a tab round trip", 1);
    w.Press(L"RESET");
    expectActive(L"after Reset", 0);
    w.GotFocus(1, true);
    expectActive(L"after the right field got focus", 1);
    w.GotFocus(0, true);
    expectActive(L"after the left field got focus", 0);

    w.Press(L"RESET");
    w.Press(L",");
    Eq(L"',' ignored while left is active", L"", bl.text);
    w.Press(L"8");
    w.Press(L":");
    w.Press(L":");
    w.Press(L"3");
    Eq(L"keypad left", L"8:3", bl.text);
    w.Press(L"RESET");
    w.GotFocus(1, true);
    w.Press(L":");
    Eq(L"':' ignored while right is active", L"", br.text);
    w.Press(L",");
    w.Press(L"5");
    Eq(L"keypad right: lone ',' then 5", S(L",5"), br.text);
    Eq(L"keypad right: left follows", L"00:30", bl.text);
    w.Press(L",");
    Eq(L"second ',' ignored", S(L",5"), br.text);
    for (int i = 0; i < 8; i++) w.Press(L"7");
    Eq(L"keypad right stops at 6 decimals", S(L",577777"), br.text);
    w.Press(L"RESET");
    for (int i = 0; i < 9; i++) w.Press(L"9");
    Eq(L"keypad left stops at 7 hour digits", L"9999999", bl.text);

    w.Press(L"RESET");
    w.SetConverterText(0, L"8:30");
    w.GotFocus(1, true);
    Eq(L"focus selects all of the right field", I((long long)br.text.size()), I(br.selLength()));
    w.Press(L"9");
    Eq(L"keypad digit replaces the selected value", L"9", br.text);
    Eq(L"and the left follows", L"09:00", bl.text);
    w.GotFocus(0, true);
    Eq(L"focus selects all of the left field", I((long long)bl.text.size()), I(bl.selLength()));
    w.Press(L"BACK");
    Eq(L"BACK deletes a selection", L"", bl.text);
    Eq(L"BACK with the left selection clears the right", L"", br.text);

    w.SelectTab(1);
    Keys(w, L"7:30/0=");
    w.SelectTab(0);
    w.Press(L"RESET");
    True(L"converter ':' key unaffected by a calculator error", w.KeyEnabled(L":"));
    w.SelectTab(1);
    True(L"calculator '+' disabled in the error state", !w.KeyEnabled(L"+"));
    True(L"calculator ':' disabled in the error state", !w.KeyEnabled(L":"));
    True(L"calculator ',' disabled in the error state", !w.KeyEnabled(L","));
    True(L"calculator BACK disabled in the error state", !w.KeyEnabled(L"BACK"));
    True(L"calculator digit enabled in the error state", w.KeyEnabled(L"5"));
    True(L"calculator CE enabled in the error state", w.KeyEnabled(L"CE"));
    True(L"calculator C enabled in the error state", w.KeyEnabled(L"C"));
    w.Press(L"C");
    for (const wchar_t* k : { L"+", L"-", L"*", L"/", L"=", L"BACK", L":", L",", L"5", L"CE", L"C" })
        True(std::wstring(L"calculator ") + k + L" enabled after C", w.KeyEnabled(k));
}

// A field selects all its text when it gets focus by click or Tab. Focus that arrives from outside the
// window (the window was activated again after Alt+Tab, or a keypad key was clicked in an inactive window)
// must keep the caret, or the next key replaces the value.
static void WindowFocusSelection() {
    Win wp = New();
    MainWindow& w = *wp;
    app::Field& bl = w.FieldRef(0);
    app::Field& br = w.FieldRef(1);

    w.SetConverterText(0, L"8:3");
    bl.anchor = bl.caret = 3;
    w.GotFocus(0, false);
    Eq(L"activation keeps the caret (nothing selected)", L"0", I(bl.selLength()));
    w.Press(L"0");
    Eq(L"... so a keypad digit continues the typed text", L"8:30", bl.text);
    Eq(L"... and the other field follows", S(L"8,50"), br.text);

    w.GotFocus(1, false);
    Eq(L"activation of the right field keeps the caret", L"0", I(br.selLength()));

    w.GotFocus(0, true);
    Eq(L"Tab from the other field selects all", I((long long)bl.text.size()), I(bl.selLength()));
    w.Press(L"9");
    Eq(L"... so the next key replaces the value", L"9", bl.text);

    w.SetConverterText(0, L"8:30");
    bl.anchor = bl.caret = 2;
    w.MouseDownOnField(1);
    w.GotFocus(1, false);
    Eq(L"a click on the other field selects all of it", I((long long)br.text.size()), I(br.selLength()));
    w.Press(L"7");
    Eq(L"... and the next key replaces it", L"7", br.text);
    Eq(L"... the left field follows", L"07:00", bl.text);

    // The window was deactivated and activated again: the caret stays, nothing is selected.
    w.SetConverterText(0, L"8:30");
    bl.anchor = bl.caret = 4;
    w.SetWindowActive(false);
    w.SetWindowActive(true);
    Eq(L"re-activating the window keeps the selection state", L"0", I(bl.selLength()));
}

static void WindowConvertKeepsValue() {
    for (const wchar_t* input : { L"8,058", L"0,0083", L"8,0083", L"0,996", L"8,335", L"12,3456", L"1,5", L"0,125", L"9,994" }) {
        Win wp = New();
        MainWindow& w = *wp;
        w.SetConverterText(1, S(input));
        std::wstring leftLive = LT(w), capLive = w.ConverterCaption();
        w.Press(L"CONVERT");
        Eq(std::wstring(L"Convert on right <") + S(input) + L"> keeps the left (" + leftLive + L")", leftLive, LT(w));
        Eq(std::wstring(L"Convert on right <") + S(input) + L"> keeps the caption", capLive, w.ConverterCaption());
        Eq(std::wstring(L"Convert on right <") + S(input) + L"> shows two decimals", hc::converter::Normalize(hc::converter::Right, S(input)), RT(w));
    }
    for (const wchar_t* input : { L"8:3", L"830", L"0:90", L"8", L"8u30", L"899" }) {
        Win wp = New();
        MainWindow& w = *wp;
        w.SetConverterText(0, input);
        std::wstring rightLive = RT(w);
        w.Press(L"CONVERT");
        Eq(std::wstring(L"Convert on left <") + input + L"> keeps the right", rightLive, RT(w));
    }
    {
        Win wp = New();
        MainWindow& w = *wp;
        w.SetConverterText(1, S(L"9999999,999"));
        Eq(L"right 9999999,999 gives left", L"10000000:00", LT(w));
        w.Press(L"CONVERT");
        Eq(L"Convert right 9999999,999: right", S(L"10000000,00"), RT(w));
        Eq(L"Convert right 9999999,999: left", L"10000000:00", LT(w));
    }
    {
        Win wp = New();
        MainWindow& w = *wp;
        w.SetConverterText(0, L"9999999:60");
        Eq(L"left 9999999:60 gives right", S(L"10000000,00"), RT(w));
        w.Press(L"CONVERT");
        Eq(L"Convert left 9999999:60: left", L"10000000:00", LT(w));
        Eq(L"Convert left 9999999:60: right", S(L"10000000,00"), RT(w));
    }
    {
        Win wp = New();
        MainWindow& w = *wp;
        w.SetConverterText(0, L"9999999:99");
        std::wstring rightShown = RT(w);
        w.GotFocus(1, true);
        w.Press(L"CONVERT");
        Eq(L"Convert on an untouched 8 digit right value keeps it", rightShown, RT(w));
        Eq(L"and the left is that same value (10000000:39)", L"10000000:39", LT(w));
    }
    {
        Win wp = New();
        MainWindow& w = *wp;
        w.SetConverterText(1, S(L"8,5"));
        w.Press(L"CE");
        Eq(L"CE clears right", L"", RT(w));
        Eq(L"CE clears left", L"", LT(w));
        Eq(L"CE clears caption", L"", w.ConverterCaption());
        w.Press(L"CONVERT");
        Eq(L"Convert on empty fields: left", L"", LT(w));
        Eq(L"Convert on empty fields: right", L"", RT(w));
        w.SetConverterText(0, L":");
        w.Press(L"CONVERT");
        Eq(L"Convert on a lone separator clears the field", L"", LT(w));
    }
}

static const wchar_t* SpecKeyForChar(wchar_t c) {
    static wchar_t buf[2];
    if (c >= L'0' && c <= L'9') { buf[0] = c; buf[1] = 0; return buf; }
    if (c == L':' || c == L'u' || c == L'U' || c == L'h' || c == L'H') return L":";
    if (c == L',' || c == L'.') return L",";
    if (c == L'+' || c == L'-' || c == L'*' || c == L'/' || c == L'=') { buf[0] = c; buf[1] = 0; return buf; }
    return nullptr;
}

static std::wstring State(hc::CalcEngine& e) {
    return e.MainText() + L"|" + e.Expression() + L"|" + e.DecimalText();
}

static void WindowKeyboardMapping() {
    Win wp = New();
    MainWindow& w = *wp;
    w.SelectTab(1);
    std::vector<wchar_t> chars;
    for (wchar_t c = L' '; c <= L'~'; c++) chars.push_back(c);
    for (wchar_t c : { (wchar_t)0x00E9, (wchar_t)0x00E8, (wchar_t)0x00E7, (wchar_t)0x00E0, (wchar_t)0x00A7, (wchar_t)0x00B0, (wchar_t)0x00B2, (wchar_t)0x00B5, (wchar_t)0x20AC,
                       (wchar_t)0x00D7, (wchar_t)0x00F7, (wchar_t)0x2212, (wchar_t)'\t', (wchar_t)'\r', (wchar_t)0x001B })
        chars.push_back(c);
    int bad = 0;
    std::wstring first;
    for (wchar_t c : chars) {
        w.Press(L"C");
        RaiseText(w, std::wstring(L"7") + c + L"3");
        hc::CalcEngine e;
        e.Press(L"7");
        const wchar_t* k = SpecKeyForChar(c);
        if (k) e.Press(k);
        e.Press(L"3");
        std::wstring a = State(w.Engine()), x = State(e);
        if (a != x) {
            if (!bad) first = L"U+" + I(c) + L" gave " + a + L", expected " + x;
            bad++;
        }
    }
    Eq(L"calculator text mapping for " + I((long long)chars.size()) + L" characters (bad count; first: " + first + L")", L"0", I(bad));

    w.Press(L"C");
    RaiseText(w, L"7u30+7h20=");
    Eq(L"one event with a whole expression", L"14:50", w.Engine().MainText());
    Eq(L"... and its expression line", L"07:30 + 07:20 =", w.Engine().Expression());
    w.Press(L"C");
    RaiseText(w, L"7,5+7.5=");
    Eq(L"comma and dot are both the decimal separator", L"15:00", w.Engine().MainText());

    w.Press(L"C");
    RaiseText(w, L"7:30+1");
    RaiseKey(w, VK_RETURN);
    Eq(L"Enter is equals", L"08:30", w.Engine().MainText());
    RaiseKey(w, VK_RETURN);
    Eq(L"Enter again repeats", L"09:30", w.Engine().MainText());
    RaiseKey(w, VK_BACK);
    Eq(L"Backspace after a result does nothing", L"09:30", w.Engine().MainText());
    RaiseKey(w, VK_DELETE);
    Eq(L"Delete is CE (a result resets)", L"00:00", w.Engine().MainText());
    RaiseText(w, L"1:3");
    RaiseKey(w, VK_BACK);
    Eq(L"Backspace edits the entry", L"1:", w.Engine().MainText());
    RaiseKey(w, VK_DELETE);
    Eq(L"Delete clears the entry", L"0", w.Engine().MainText());
    RaiseText(w, L"5+5");
    RaiseKey(w, VK_ESCAPE);
    Eq(L"Escape is C", L"00:00", w.Engine().MainText());
    Eq(L"Escape clears the expression", L"", w.Engine().Expression());
    RaiseText(w, L"7:30/0");
    RaiseKey(w, VK_RETURN);
    Eq(L"Enter on x / 0 shows the error", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseKey(w, VK_RETURN);
    Eq(L"Enter in the error state does nothing", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseKey(w, VK_BACK);
    Eq(L"Backspace in the error state does nothing", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseText(w, L"+");
    Eq(L"'+' in the error state does nothing", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseText(w, L":");
    Eq(L"':' in the error state does nothing", hc::CalcEngine::DivideByZeroText, w.Engine().MainText());
    RaiseText(w, L"2");
    Eq(L"a digit leaves the error state", L"2", w.Engine().MainText());

    Eq(L"calculator keystrokes did not touch the converter", L"", LT(w));
    w.SelectTab(0);
    w.Press(L"C");
    std::wstring before = State(w.Engine());
    RaiseText(w, L"7:30+7:20");
    RaiseKey(w, VK_DELETE);
    RaiseKey(w, VK_BACK);
    Eq(L"converter keystrokes did not touch the calculator", before, State(w.Engine()));
    Eq(L"converter typed text (Backspace took the last digit)", L"7:3", LT(w));
    RaiseKey(w, VK_RETURN);
    Eq(L"converter Enter converts", L"07:03", LT(w));
    RaiseKey(w, VK_ESCAPE);
    Eq(L"converter Esc resets", L"", LT(w));
    Eq(L"converter Esc keeps the calculator", before, State(w.Engine()));

    Win wq = New();
    MainWindow& v = *wq;
    RaiseText(v, L"8u30");
    Eq(L"unfocused text to the left field", L"8:30", LT(v));
    v.SetConverterText(1, L"");
    RaiseText(v, L"8:30");
    Eq(L"unfocused ':' is ignored on the right", L"830", RT(v));
    v.Press(L"RESET");
    v.SetConverterText(1, L"");
    RaiseText(v, L"8.5");
    Eq(L"unfocused '.' on the right", S(L"8,5"), RT(v));
    Eq(L"unfocused '.' on the right: left", L"08:30", LT(v));

    // Editing keys of a focused field: Backspace, Delete, Home, End, arrows, Ctrl+A.
    Win wr = New();
    MainWindow& u = *wr;
    u.GotFocus(0, true);
    TypeInto(u, 0, L"830");
    u.OnKeyDown(VK_HOME, false, false);
    Eq(L"Home moves the caret to the start", L"0", I(u.FieldRef(0).caret));
    u.OnKeyDown(VK_RIGHT, false, false);
    u.OnKeyDown(VK_DELETE, false, false);
    Eq(L"Delete removes the character after the caret", L"80", LT(u));
    u.OnKeyDown(VK_END, false, true);
    Eq(L"Shift+End selects to the end", L"1", I(u.FieldRef(0).selLength()));
    u.OnKeyDown('A', true, false);
    Eq(L"Ctrl+A selects all", L"2", I(u.FieldRef(0).selLength()));
    u.OnKeyDown(VK_BACK, false, false);
    Eq(L"Backspace deletes the selection", L"", LT(u));
    u.OnKeyDown(VK_TAB, false, false);
    Eq(L"Tab moves the focus to the other field", L"1", I(u.FocusField()));
    u.OnKeyDown(VK_TAB, false, true);
    Eq(L"Shift+Tab moves it back", L"0", I(u.FocusField()));
}

static void WindowCalcWiring() {
    Win wp = New();
    MainWindow& w = *wp;
    w.SelectTab(1);
    Eq(L"idle left display", L"00:00", w.CalcMain());
    Eq(L"idle right display", S(L"0,00"), w.CalcDecimal());
    Eq(L"idle caption", L"", w.CalcCaption());
    Eq(L"idle expression", L"", w.ExpressionShown());

    const wchar_t* pool[] = { L"0", L"1", L"2", L"3", L"4", L"5", L"6", L"7", L"8", L"9", L":", L",", L"+", L"-", L"*", L"/", L"=", L"=", L"CE", L"C", L"BACK" };
    const int poolSize = (int)(sizeof pool / sizeof pool[0]);
    unsigned seed = 31;
    auto next = [&]() { seed = seed * 1103515245u + 12345u; return (seed >> 16) & 0x7FFF; };
    int bad = 0;
    std::wstring first;
    for (int n = 0; n < 500; n++) {
        w.Press(L"C");
        int len = 1 + (int)(next() % 13);
        std::wstring log;
        for (int i = 0; i < len; i++) {
            const wchar_t* k = pool[next() % (unsigned)poolSize];
            log += std::wstring(k) + L" ";
            w.Press(k);
            hc::CalcEngine& e = w.Engine();
            bool err = e.IsError();
            bool ok = w.CalcMain() == e.MainText() && w.CalcDecimal() == e.DecimalText() && w.CalcCaption() == e.Caption();
            ok = ok && (w.CalcRightVisible() == !err);
            ok = ok && (w.GetLayout().calcVal[0].w == (err ? w.GetLayout().calcDisplay.w : w.GetLayout().calcVal[0].w));
            for (const wchar_t* key : { L"0", L"1", L"5", L"9", L":", L",", L"+", L"-", L"*", L"/", L"=", L"CE", L"C", L"BACK" }) ok = ok && (w.KeyEnabled(key) == e.IsEnabled(key));
            std::wstring shown = w.ExpressionShown();
            std::wstring full = e.Expression();
            auto rep = [&](const wchar_t* a, const wchar_t* b) {
                size_t pos = 0;
                while ((pos = full.find(a, pos)) != std::wstring::npos) { full.replace(pos, wcslen(a), b); pos += wcslen(b); }
            };
            rep(L" * ", L" \x00D7 ");
            rep(L" / ", L" \x00F7 ");
            rep(L" - ", L" \x2212 ");
            ok = ok && (shown == full || (shown.compare(0, 2, L"\x2026 ") == 0 && shown.size() > 2 && full.size() >= shown.size() - 2 &&
                                          full.compare(full.size() - (shown.size() - 2), shown.size() - 2, shown.substr(2)) == 0));
            if (!ok) {
                if (!bad) first = log + L"=> UI <" + w.CalcMain() + L"|" + w.CalcDecimal() + L"|" + w.CalcCaption() + L"|" + shown + L">, engine <" + e.MainText() + L"|" + e.DecimalText() + L"|" + e.Caption() + L"|" + e.Expression() + L">";
                bad++;
            }
        }
    }
    Eq(L"window display always mirrors the engine (bad count; first: " + first + L")", L"0", I(bad));

    w.Press(L"C");
    Keys(w, L"7:30+7:20=");
    Eq(L"copy left", L"14:50", w.Engine().CopyMainText());
    Eq(L"copy right", S(L"14,83"), w.Engine().CopyDecimalText());
}

static void WindowExpressionLine() {
    Win wp = New();
    MainWindow& w = *wp;
    w.SelectTab(1);
    Keys(w, L"7:30*2/4-1=");
    Eq(L"operators are drawn as x / division sign / minus sign", L"07:30 \x00D7 2 \x00F7 4 \x2212 01:00 =", w.ExpressionShown());
    w.Press(L"C");
    Eq(L"C clears the expression line", L"", w.ExpressionShown());
    Keys(w, L"7:30*1:30=");
    Eq(L"factor shown as a decimal", S(L"07:30 \x00D7 1,5 ="), w.ExpressionShown());
    w.Press(L"C");
    Keys(w, L"7:30/0=");
    Eq(L"error keeps the failing operation", L"07:30 \x00F7 0 =", w.ExpressionShown());
    w.Press(L"C");

    const int sizes[][2] = { { 324, 521 }, { 284, 441 }, { 584, 861 } };
    for (auto& s : sizes) {
        Win vp = New();
        MainWindow& v = *vp;
        v.SelectTab(1);
        v.LayoutAt(s[0], s[1], 1.0f);
        std::wstring typed;
        for (int i = 0; i < 14; i++) {
            Keys(v, L"7:30+");
            typed += L"07:30 + ";
        }
        std::wstring full = typed.substr(0, typed.size() - 1);
        std::wstring tag = L"long chain at " + I(s[0]) + L"x" + I(s[1]);
        const std::wstring& x = v.ExpressionShown();
        True(tag + L": shortened with an ellipsis", x.compare(0, 2, L"\x2026 ") == 0);
        True(tag + L": keeps the end", full.size() >= x.size() - 2 && full.compare(full.size() - (x.size() - 2), x.size() - 2, x.substr(2)) == 0);
        True(tag + L": keeps at least the pending operator", !x.empty() && x.back() == L'+');
        float need = gfx::TextWidth(x.c_str(), (int)x.size(), ui::TextFont(14.0f * v.GetLayout().s));
        True(tag + L": fits the line (" + F2(need) + L" of " + F2(v.GetLayout().calcDisplay.w) + L")", need <= v.GetLayout().calcDisplay.w + 0.5f);
        v.Press(L"=");
        True(tag + L": still ends with = after the result", !v.ExpressionShown().empty() && v.ExpressionShown().back() == L'=');
        v.Press(L"C");
        Eq(tag + L": cleared", L"", v.ExpressionShown());
    }
}

// ------------------------------------------------------------------ DPI and theme change

static void DpiAndTheme() {
    g_culture = L"-";
    Win wp = New();
    MainWindow& w = *wp;
    w.LayoutAt(486, 782, 1.5f);
    True(L"layout at 150 % keeps the UI scale at 1", Near(w.GetLayout().s, 1.0f, 0.01f));
    True(L"layout at 150 %: the header is 72 px", w.GetLayout().headerH == 72);
    True(L"layout at 150 %: the pin is 60 px", w.GetLayout().pin.w == 60);
    w.LayoutAt(648, 1042, 2.0f);
    True(L"layout at 200 %: the header is 96 px", w.GetLayout().headerH == 96);
    // Fractional scales lay out and render without trouble (rounded to whole device pixels).
    for (float d : { 1.25f, 1.75f, 2.5f }) {
        w.LayoutAt((int)Rn(324 * d), (int)Rn(521 * d), d);
        gfx::Canvas canvas;
        w.Render(canvas);
        True(L"render at scale " + F2(d) + L" gives the client size", canvas.W() == (int)Rn(324 * d) && canvas.H() == (int)Rn(521 * d));
        const app::Layout& lay = w.GetLayout();
        bool inside = true;
        for (const RectF& k : lay.keys) inside = inside && k.x >= 0 && k.y >= 0 && k.right() <= lay.W && k.bottom() <= lay.H && k.w > 0 && k.h > 0;
        True(L"every key lies inside the client area at scale " + F2(d), inside);
    }
    w.LayoutAt(200, 300, 1.0f);
    True(L"scale never goes below 0.85", Near(w.GetLayout().s, 0.85f, 0.001f));
    w.LayoutAt(1200, 1800, 1.0f);
    True(L"scale never goes above 2.0", Near(w.GetLayout().s, 2.0f, 0.001f));
    // The theme changes the palette at once.
    w.ApplyTheme(true);
    True(L"dark palette in use", w.IsDark() && w.Palette().bg == gfx::Rgb(0x20, 0x20, 0x20));
    w.ApplyTheme(false);
    True(L"light palette in use", !w.IsDark() && w.Palette().bg == gfx::Rgb(0xF0, 0xF4, 0xF7));
}

// ------------------------------------------------------------------ the review fixes

// An in-memory clipboard for clip::Ops: the owner's own clipboard is never touched, and these tests do not depend on it being
// open-able. 'failOpens' makes OpenClipboard fail that many times, like another program that holds the clipboard.
namespace {
struct FakeClip {
    int failOpens = 0;
    int openCalls = 0;
    bool open = false;
    HGLOBAL data = nullptr;
};
FakeClip g_fc;

BOOL WINAPI FakeOpen(HWND) {
    g_fc.openCalls++;
    if (g_fc.failOpens > 0) {
        g_fc.failOpens--;
        return FALSE;
    }
    g_fc.open = true;
    return TRUE;
}
BOOL WINAPI FakeClose() { g_fc.open = false; return TRUE; }
BOOL WINAPI FakeEmpty() {
    if (g_fc.data) GlobalFree(g_fc.data);
    g_fc.data = nullptr;
    return TRUE;
}
HANDLE WINAPI FakeSet(UINT fmt, HANDLE h) {
    if (fmt != CF_UNICODETEXT || !g_fc.open) return nullptr;
    g_fc.data = static_cast<HGLOBAL>(h);
    return h;
}
HANDLE WINAPI FakeGet(UINT fmt) { return fmt == CF_UNICODETEXT && g_fc.open ? g_fc.data : nullptr; }
BOOL WINAPI FakeAvailable(UINT fmt) { return fmt == CF_UNICODETEXT && g_fc.data != nullptr; }
const clip::Ops kFakeOps = { FakeOpen, FakeClose, FakeEmpty, FakeSet, FakeGet, FakeAvailable, 1 };

struct UseFakeClipboard {
    const clip::Ops* before;
    UseFakeClipboard() {
        g_fc = FakeClip();
        before = clip::SetOps(&kFakeOps);
    }
    ~UseFakeClipboard() {
        FakeEmpty();
        clip::SetOps(before);
    }
};

std::wstring FakeText() {
    if (!g_fc.data) return L"<none>";
    const wchar_t* p = static_cast<const wchar_t*>(GlobalLock(g_fc.data));
    std::wstring s = p ? p : L"<unlockable>";
    GlobalUnlock(g_fc.data);
    return s;
}
}  // namespace

static void ClipboardModule() {
    g_culture = L"-";
    UseFakeClipboard fake;
    g_fc.failOpens = 3;
    True(L"SetText succeeds although another program holds the clipboard for a few tries", clip::SetText(nullptr, L"hello"));
    Eq(L"... after 4 tries", L"4", I(g_fc.openCalls));
    Eq(L"... and the text is on the clipboard", L"hello", FakeText());
    g_fc.openCalls = 0;
    g_fc.failOpens = 1000;
    DWORD t0 = GetTickCount();
    True(L"SetText gives up when the clipboard stays busy", !clip::SetText(nullptr, L"other"));
    Eq(L"... after the set number of tries", I(clip::kTries), I(g_fc.openCalls));
    True(L"... without waiting for long", GetTickCount() - t0 < 1000);
    g_fc.failOpens = 0;
    Eq(L"... and the clipboard still holds the old text", L"hello", FakeText());
    True(L"an empty text is not put on the clipboard", !clip::SetText(nullptr, L""));
    Eq(L"... the old text stays", L"hello", FakeText());
    g_fc.failOpens = 2;
    std::wstring got = L"before";
    True(L"GetText retries too", clip::GetText(nullptr, got));
    Eq(L"... and returns the text", L"hello", got);
    True(L"HasText sees the text", clip::HasText());
    FakeEmpty();
    got = L"before";
    True(L"GetText fails when there is no text", !clip::GetText(nullptr, got));
    Eq(L"... and leaves the text alone", L"before", got);
    True(L"HasText sees no text", !clip::HasText());
    // A block without the terminating NUL (another program's mistake): read as far as the block goes, no further.
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, 10);
    size_t n = GlobalSize(g) / sizeof(wchar_t);
    wchar_t* p = static_cast<wchar_t*>(GlobalLock(g));
    for (size_t i = 0; i < n; i++) p[i] = L'x';
    GlobalUnlock(g);
    g_fc.data = g;
    got.clear();
    True(L"GetText reads a block that has no terminating NUL", clip::GetText(nullptr, got));
    Eq(L"... exactly as far as the block goes", I((long long)n), I((long long)got.size()));
}

// Ctrl+C / Ctrl+X / Ctrl+V and the Cut / Copy / Paste menu of a converter field, and the calculator's copy, on the in-memory clipboard.
static void FieldClipboard() {
    UseFakeClipboard fake;
    Win wp = New();
    MainWindow& w = *wp;
    w.SetConverterText(0, L"08:30");
    w.GotFocus(0, true);
    w.OnKeyDown('C', true, false);
    Eq(L"Ctrl+C puts the selection on the clipboard", L"08:30", FakeText());
    w.OnKeyDown('X', true, false);
    Eq(L"Ctrl+X empties the field", L"", LT(w));
    Eq(L"... the other field follows", L"", RT(w));
    Eq(L"... and the text is on the clipboard", L"08:30", FakeText());
    w.OnKeyDown('V', true, false);
    Eq(L"Ctrl+V pastes it back", L"08:30", LT(w));
    Eq(L"... and the other field follows", S(L"8,50"), RT(w));
    // A cut that cannot reach the clipboard leaves the text where it is.
    w.GotFocus(0, true);
    g_fc.failOpens = 1000;
    w.OnKeyDown('X', true, false);
    g_fc.failOpens = 0;
    Eq(L"Ctrl+X with a clipboard that stays busy leaves the field alone", L"08:30", LT(w));
    Eq(L"... and the clipboard as it was", L"08:30", FakeText());

    // the menu of the field
    w.GotFocus(0, true);
    w.PrepareFieldMenu(0);
    flyout::Flyout& m = w.FieldMenuFlyout();
    const std::vector<flyout::Item>& it = m.Items();
    Eq(L"the field menu has three items", L"3", I((long long)it.size()));
    if (it.size() == 3) {
        Eq(L"first item", L"Cut", it[0].text);
        Eq(L"second item", L"Copy", it[1].text);
        Eq(L"third item", L"Paste", it[2].text);
        True(L"with a selection Cut and Copy are enabled", !it[0].disabled && !it[1].disabled);
        True(L"with text on the clipboard Paste is enabled", !it[2].disabled);
    }
    w.FieldRef(0).anchor = w.FieldRef(0).caret;
    w.PrepareFieldMenu(0);
    if (it.size() == 3) {
        True(L"without a selection Cut and Copy are disabled", it[0].disabled && it[1].disabled && !it[2].disabled);
        auto idAt = [&](int i) { return m.ItemIdAt(m.ShadowRoom() + (int)(it[(size_t)i].rect.x + it[(size_t)i].rect.w / 2), m.ShadowRoom() + (int)(it[(size_t)i].rect.y + it[(size_t)i].rect.h / 2)); };
        Eq(L"a greyed out item is not under the pointer for the popup (Cut)", L"-1", I(idAt(0)));
        Eq(L"... neither Copy", L"-1", I(idAt(1)));
        Eq(L"... an enabled item is (Paste)", I(app::kCmdPaste), I(idAt(2)));
    }
    FakeEmpty();
    w.PrepareFieldMenu(0);
    if (it.size() == 3) True(L"without text on the clipboard Paste is disabled", it[2].disabled);

    w.GotFocus(0, true);
    w.PrepareFieldMenu(0);
    w.OnFlyoutCommand(app::kCmdFieldCopy, 0);
    Eq(L"the Copy command copies the selection", L"08:30", FakeText());
    w.OnFlyoutCommand(app::kCmdCut, 0);
    Eq(L"the Cut command cuts it", L"", LT(w));
    w.OnFlyoutCommand(app::kCmdPaste, 0);
    Eq(L"the Paste command pastes", L"08:30", LT(w));
    // The command works on the field the menu was opened for, not on the one that has the focus now.
    w.SetConverterText(1, S(L"7,25"));
    w.GotFocus(1, true);
    w.PrepareFieldMenu(1);
    w.GotFocus(0, true);
    w.OnFlyoutCommand(app::kCmdFieldCopy, 0);
    Eq(L"the menu acts on its own field", S(L"7,25"), FakeText());

    // the calculator
    w.SelectTab(1);
    Keys(w, L"7:30+7:20=");
    w.OnKeyDown('C', true, false);
    Eq(L"Ctrl+C copies the result", L"14:50", FakeText());
    w.OnKeyDown('C', true, true);
    Eq(L"Ctrl+Shift+C copies the decimal result", S(L"14,83"), FakeText());
    w.Press(L"C");
    FakeEmpty();
    w.OnKeyDown('V', true, false);
    Eq(L"Ctrl+V with nothing on the clipboard changes nothing", L"00:00", w.Engine().MainText());
}

// The caret timer runs only while a field of an active window has the focus.
static void CaretTimerIdle() {
    MainWindow w(nullptr);
    True(L"real window created for the caret timer test", w.Create(g_inst, true));
    HWND h = w.hwnd();
    UINT blink = GetCaretBlinkTime();
    std::wstring on = (blink != 0 && blink != INFINITE) ? L"1" : L"0";     // a user can turn the blinking off
    Eq(L"a window that is not active has no caret timer", L"0", I(w.CaretTimerRunning()));
    SendMessageW(h, WM_SETFOCUS, 0, 0);
    Eq(L"the caret timer runs while a field has the keyboard focus", on, I(w.CaretTimerRunning()));
    w.SelectTab(1);
    Eq(L"... not on the calculator tab, where nothing blinks", L"0", I(w.CaretTimerRunning()));
    w.SelectTab(0);
    Eq(L"... and again when a field has the focus", on, I(w.CaretTimerRunning()));
    SendMessageW(h, WM_KILLFOCUS, 0, 0);
    Eq(L"... not when the window loses the focus", L"0", I(w.CaretTimerRunning()));
    SendMessageW(h, WM_SETFOCUS, 0, 0);
    Eq(L"... and again when it comes back", on, I(w.CaretTimerRunning()));
}

// A click right after a double click, in the same place, keeps everything selected (a triple click).
static void TripleClick() {
    MainWindow w(nullptr);
    True(L"real window created for the triple click test", w.Create(g_inst, true));
    HWND h = w.hwnd();
    w.SetWindowActive(true);
    w.SetConverterText(0, L"08:30");
    w.GotFocus(0, false);
    const RectF& b0 = w.GetLayout().box[0];
    int x = (int)(b0.x + b0.w / 2), y = (int)(b0.y + b0.h / 2);
    LPARAM lp = MAKELPARAM(x, y);
    int all = (int)LT(w).size();
    auto down = [&](UINT msg, LPARAM at) {
        SendMessageW(h, msg, MK_LBUTTON, at);
        SendMessageW(h, WM_LBUTTONUP, 0, at);
    };
    down(WM_LBUTTONDOWN, lp);
    Eq(L"a single click in the focused field places the caret", L"0", I(w.FieldRef(0).selLength()));
    down(WM_LBUTTONDBLCLK, lp);
    Eq(L"the double click selects all", I(all), I(w.FieldRef(0).selLength()));
    down(WM_LBUTTONDOWN, lp);
    Eq(L"the third click keeps everything selected", I(all), I(w.FieldRef(0).selLength()));
    down(WM_LBUTTONDOWN, lp);
    Eq(L"a fourth click places the caret again", L"0", I(w.FieldRef(0).selLength()));
    down(WM_LBUTTONDBLCLK, lp);
    down(WM_LBUTTONDOWN, MAKELPARAM(x - 40, y));
    Eq(L"a click somewhere else after a double click is a plain click", L"0", I(w.FieldRef(0).selLength()));
    down(WM_LBUTTONDBLCLK, lp);
    Sleep(GetDoubleClickTime() + 80);
    down(WM_LBUTTONDOWN, lp);
    Eq(L"a click after the double click time is a plain click", L"0", I(w.FieldRef(0).selLength()));
}

// The first erase of the background fills the window colour, later ones leave the memory bitmap alone.
static void FirstErase() {
    MainWindow w(nullptr);
    True(L"real window created for the erase test", w.Create(g_inst, true));
    HWND h = w.hwnd();
    RECT cr;
    GetClientRect(h, &cr);
    HDC screen = GetDC(nullptr);
    HDC mem = CreateCompatibleDC(screen);
    HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
    HGDIOBJ old = SelectObject(mem, bmp);
    HBRUSH sentinel = CreateSolidBrush(RGB(255, 0, 255));
    FillRect(mem, &cr, sentinel);
    True(L"WM_ERASEBKGND is handled", SendMessageW(h, WM_ERASEBKGND, (WPARAM)mem, 0) == 1);
    COLORREF px = GetPixel(mem, cr.right / 2, cr.bottom / 2);
    gfx::Argb bg = w.Palette().bg;
    True(L"the first erase fills the window colour", GetRValue(px) == gfx::R(bg) && GetGValue(px) == gfx::G(bg) && GetBValue(px) == gfx::B(bg));
    FillRect(mem, &cr, sentinel);
    SendMessageW(h, WM_ERASEBKGND, (WPARAM)mem, 0);
    px = GetPixel(mem, cr.right / 2, cr.bottom / 2);
    True(L"later erases paint nothing", GetRValue(px) == 255 && GetGValue(px) == 0 && GetBValue(px) == 255);
    DeleteObject(sentinel);
    SelectObject(mem, old);
    DeleteObject(bmp);
    DeleteDC(mem);
    ReleaseDC(nullptr, screen);
}

// Closing the menu on the click that opened it must not reopen it: a click on the menu button right after the menu closed
// is ignored (the guard of the WPF build). The click itself is covered by the on-screen popup check.
static void MenuReopenGuard() {
    MainWindow w(nullptr);
    True(L"real window created for the menu guard test", w.Create(g_inst, true));
    HWND h = w.hwnd();
    True(L"before any menu was closed, the menu button is not blocked", !w.MenuReopenBlocked());
    // The message the popup posts when the menu closes, with the time it closed at.
    SendMessageW(h, flyout::WM_FLYOUT_CLOSED, (WPARAM)flyout::Flyout::Menu, (LPARAM)GetTickCount());
    True(L"right after the menu closed, a click on its button is ignored", w.MenuReopenBlocked());
    SendMessageW(h, flyout::WM_FLYOUT_CLOSED, (WPARAM)flyout::Flyout::Menu, (LPARAM)(GetTickCount() - 400));
    True(L"... a moment later it is not", !w.MenuReopenBlocked());
    SendMessageW(h, flyout::WM_FLYOUT_CLOSED, (WPARAM)flyout::Flyout::Copy, (LPARAM)GetTickCount());
    True(L"the closing of another popup does not count", !w.MenuReopenBlocked());
}

// Milliseconds on the performance counter (the timings that HC_TRACE prints).
static double NowMs() {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return 1000.0 * (double)c.QuadPart / (double)f.QuadPart;
}

// What the first window costs (HC_TRACE=1 prints it; run before everything else, so nothing is warm yet except the drawing
// engine): creating it hidden, laying it out and painting it once, then once more.
static void FirstWindowCost() {
    if (!g_trace) return;
    g_culture = L"-";
    double t0 = NowMs();
    MainWindow w(nullptr);
    if (!w.Create(g_inst, true)) return;
    double t1 = NowMs();
    gfx::Canvas c;
    w.Render(c);
    double t2 = NowMs();
    w.Render(c);
    double t3 = NowMs();
    wprintf(L"  [timing] first window: create + layout %.1f ms, first paint %.1f ms, second paint %.1f ms\n", t1 - t0, t2 - t1, t3 - t2);
}

// What a popup costs to show again, and what it does with its items.
static void FlyoutRebuild() {
    g_culture = L"-";
    theme::Palette pal = theme::Create(false, theme::SystemAccent(false));
    flyout::Flyout f(flyout::Flyout::Menu);
    std::vector<flyout::Item> items;
    items.push_back({ flyout::Item::Header, L"Advanced", 0, false, {} });
    items.push_back({ flyout::Item::Button, L"One", 1, false, {} });
    items.push_back({ flyout::Item::Button, L"Two", 2, false, {} });
    f.SetItems(items);
    f.SetPalette(pal);
    gfx::Canvas c;
    c.Resize(400, 400);
    double t0 = NowMs();
    f.Layout(nullptr, 1.0f);
    f.ComposeOver(c, 20, 20);
    double t1 = NowMs();
    Eq(L"the outline and shadow of a popup are made for the first layout", L"1", I(f.ShapeBuilds()));
    f.Layout(nullptr, 1.0f);
    f.ComposeOver(c, 20, 20);
    double t2 = NowMs();
    if (g_trace) wprintf(L"  [timing] popup layout + pixels: first %.1f ms, again %.1f ms\n", t1 - t0, t2 - t1);
    Eq(L"laying out again at the same scale does not make them again", L"1", I(f.ShapeBuilds()));
    f.SetHot(1, false);
    f.ComposeOver(c, 20, 20);
    Eq(L"... neither does a hot item", L"1", I(f.ShapeBuilds()));
    f.Layout(nullptr, 1.5f);
    f.ComposeOver(c, 20, 20);
    Eq(L"a new scale does", L"2", I(f.ShapeBuilds()));
    f.SetEnabled(2, false);
    f.Layout(nullptr, 1.5f);
    f.ComposeOver(c, 20, 20);
    Eq(L"disabling an item does not", L"2", I(f.ShapeBuilds()));
    True(L"... and marks it", f.Items()[2].disabled && !f.Items()[1].disabled);
    f.SetHot(2, false);
    f.SetHot(1, true);
    f.ComposeOver(c, 20, 20);
}

// The license viewer: laying out the text only when its width changes, and the mouse wheel.
static void LicenseViewer() {
    g_culture = L"-";
    theme::Palette pal = theme::Create(false, theme::SystemAccent(false));
    dialogs::Dialog lic(dialogs::Dialog::License, pal, 1.0f, g_inst);
    double t0 = NowMs();
    lic.Layout(660, 420);
    double t1 = NowMs();
    Eq(L"the first layout of the license text is one pass", L"1", I(lic.LayoutPasses()));
    True(L"the license is longer than the view: scroll bar", lic.HasScrollBar());
    lic.Layout(660, 480);
    double t2 = NowMs();
    Eq(L"a change of the height alone lays nothing out again", L"1", I(lic.LayoutPasses()));
    lic.Layout(700, 480);
    double t3 = NowMs();
    if (g_trace) wprintf(L"  [timing] license layout: first %.1f ms, height only %.1f ms, width change %.1f ms\n", t1 - t0, t2 - t1, t3 - t2);
    Eq(L"a change of the width does, once", L"2", I(lic.LayoutPasses()));

    // the scroll bar decision is the one of a layout at the full width
    gfx::Font f = ui::TextFont(12.5f);
    f.fam = gfx::FamMono;
    const std::wstring& text = lic.LicenseText();
    for (int h : { 320, 420, 580, 3000, 9000, 14000, 40000 }) {
        dialogs::Dialog d(dialogs::Dialog::License, pal, 1.0f, g_inst);
        d.Layout(660, h);
        float viewW = 660.0f - 16.0f - 8.0f;      // view_.w at scale 1
        float fullH = gfx::TextHeightWrapped(text.c_str(), (int)text.size(), f, viewW - 4.0f);
        bool want = fullH > d.ViewHeight();
        Eq(L"the scroll bar is there exactly when the text is taller than the view, view height " + I((long long)d.ViewHeight()), want ? L"1" : L"0", d.HasScrollBar() ? L"1" : L"0");
    }
    {
        dialogs::Dialog d(dialogs::Dialog::License, pal, 1.0f, g_inst);
        d.Layout(660, 40000);
        Eq(L"a view taller than the text needs two passes (with and without the bar)", L"2", I(d.LayoutPasses()));
        d.Layout(660, 420);
        True(L"... and gets its scroll bar back when it shrinks", d.HasScrollBar());
        Eq(L"... with one more pass", L"3", I(d.LayoutPasses()));
    }

    // the wheel
    lic.Layout(660, 420);
    float line = Rn(gfx::LineHeight(f));
    float page = lic.ViewHeight() - line;
    lic.WheelScroll(-120, 3);
    True(L"a notch of the wheel scrolls three lines (" + F2(lic.Scroll()) + L")", Near(lic.Scroll(), 3 * line, 0.01f));
    lic.WheelScroll(120, 3);
    True(L"... and back", Near(lic.Scroll(), 0.0f, 0.01f));
    lic.WheelScroll(-120, WHEEL_PAGESCROLL);
    True(L"with 'one screen at a time' a notch scrolls a page, not to the end (" + F2(lic.Scroll()) + L")", Near(lic.Scroll(), page, 0.01f));
    lic.WheelScroll(120, WHEEL_PAGESCROLL);
    True(L"... and back a page", Near(lic.Scroll(), 0.0f, 0.01f));
    lic.WheelScroll(-120, 0);
    True(L"a setting of 0 lines scrolls nothing", Near(lic.Scroll(), 0.0f, 0.01f));
}

// A change of the scale moves the About window to the size of its content at the new scale.
static void AboutDpiChange() {
    g_culture = L"-";
    theme::Palette pal = theme::Create(false, theme::SystemAccent(false));
    dialogs::Dialog about(dialogs::Dialog::About, pal, 1.0f, g_inst);
    True(L"About window created (not shown)", about.CreateWindowFor(nullptr));
    HWND h = about.hwnd();
    RECT rc;
    GetClientRect(h, &rc);
    Eq(L"the About client height is the natural height", I(about.NaturalHeight()), I(rc.bottom));
    UINT own = GetDpiForWindow(h);
    DWORD style = (DWORD)GetWindowLongPtrW(h, GWL_STYLE), ex = (DWORD)GetWindowLongPtrW(h, GWL_EXSTYLE);
    for (UINT dpi : { 144u, 192u, own, 96u }) {
        float d = dpi / 96.0f;
        RECT suggested = { 200, 150, 200 + 777, 150 + 555 };      // not the size the content needs
        SendMessageW(h, WM_DPICHANGED, MAKEWPARAM(dpi, dpi), (LPARAM)&suggested);
        dialogs::Dialog ref(dialogs::Dialog::About, pal, d, g_inst);
        ref.Layout((int)Rn(440.0f * d), 0);
        Eq(L"after a change to " + I(dpi) + L" dpi the window reports the natural height of the content", I(ref.NaturalHeight()), I(about.NaturalHeight()));
        // The outer size is the content plus the frame of that scale, whatever size the system suggested.
        RECT want = { 0, 0, (LONG)Rn(440.0f * d), (LONG)ref.NaturalHeight() };
        AdjustWindowRectExForDpi(&want, style, FALSE, ex, dpi);
        RECT wr;
        GetWindowRect(h, &wr);
        Eq(L"... the window width", I(want.right - want.left), I(wr.right - wr.left));
        Eq(L"... the window height", I(want.bottom - want.top), I(wr.bottom - wr.top));
        Eq(L"... the window stays where the system put it", L"200,150", I(wr.left) + L"," + I(wr.top));
        if (dpi == own) {
            // (A window's frame is the one of its real scale, so the client area can only be checked at that one.)
            GetClientRect(h, &rc);
            Eq(L"... so the client width is that of the content", I((long long)Rn(440.0f * d)), I(rc.right));
            Eq(L"... and the client height is the natural height", I(ref.NaturalHeight()), I(rc.bottom));
        }
    }
}

// Failures in the drawing layer end in a skipped paint, not in a crash.
static void DrawingFailures() {
    g_culture = L"-";
    gfx::Canvas c;
    True(L"a canvas of 100 x 80 can be made", c.Resize(100, 80) && c.Valid());
    True(L"a canvas far beyond the GDI limits cannot", !c.Resize(40000, 40000));
    True(L"... and is empty", !c.Valid() && c.W() == 0 && c.H() == 0);
    c.Clear(0xFF112233);
    c.FillRect(0, 0, 10, 10, 0xFF112233);
    c.FillRoundRect({ 1, 1, 20, 20 }, 4.0f, 0xFF112233);
    c.FillEllipse({ 1, 1, 20, 20 }, 0xFF112233);
    c.BorderGradient({ 1, 1, 20, 20 }, 4.0f, 0xFF112233, 0xFF445566);
    c.BorderSolid({ 1, 1, 20, 20 }, 4.0f, 0xFF112233);
    c.Text(L"text", 4, ui::TextFont(14.0f), 0, 0, 0xFF112233);
    c.BlitTo(nullptr, 0, 0, 10, 10);
    gfx::Canvas other;
    other.Resize(10, 10);
    other.Copy(c, 0, 0);
    True(L"drawing on it does nothing and does not crash", true);
    True(L"a later Resize brings it back", c.Resize(64, 48) && c.Valid() && c.W() == 64 && c.H() == 48);
    c.Clear(0xFF112233);
    // A font DirectWrite refuses (size 0) used to send the format lookup into endless recursion.
    Eq(L"text in a font of size 0 has no width", L"0.00", F2(gfx::TextWidth(L"text", 4, gfx::Font{ gfx::FamText, 400, 0.0f })));
    c.Text(L"text", 4, gfx::Font{ gfx::FamText, 400, 0.0f }, 0, 0, 0xFF112233);
    c.Text(L"text", 4, gfx::Font{ gfx::FamText, 400, -3.0f }, 0, 0, 0xFF112233);
    Eq(L"... also not wrapped", L"0.00", F2(gfx::TextHeightWrapped(L"text", 4, gfx::Font{ gfx::FamText, 400, 0.0f }, 50.0f)));
    True(L"... and normal text still works afterwards", gfx::TextWidth(L"text", 4, ui::TextFont(14.0f)) > 5.0f);
}

// The UI scale follows the window size only from a change of 0.004 on, like the WPF window (UpdateScale).
static void ScaleHysteresis() {
    g_culture = L"-";
    Win wp = New();
    MainWindow& w = *wp;
    w.LayoutAt(324, 521, 1.0f);
    True(L"the base size has the scale 1", w.GetLayout().s == 1.0f);
    w.LayoutAt(405, 651, 1.25f);          // 324 x 520.8 DIP
    True(L"a client of 405 x 651 px at 125 % (520.8 DIP high) keeps the scale 1", w.GetLayout().s == 1.0f);
    w.LayoutAt(324, 519, 1.0f);           // 0.99616: less than 0.004 away
    True(L"a change of less than 0.004 is not followed", w.GetLayout().s == 1.0f);
    w.LayoutAt(324, 518, 1.0f);           // 0.99424: 0.0058 away
    True(L"a change of 0.004 or more is followed (" + F2(w.GetLayout().s) + L")", Near(w.GetLayout().s, 518.0f / 521.0f, 1e-5f));
    float kept = w.GetLayout().s;
    w.LayoutAt(324, 520, 1.0f);           // 0.99808: 0.0038 from the last scale
    True(L"... and the new scale is the one a small change is measured from", w.GetLayout().s == kept);
}

// Text lighter than the surface it is drawn on is drawn like the text of the dark theme, whatever the theme.
static void TextPolarity() {
    g_culture = L"-";
    theme::Palette light = theme::Create(false, theme::SystemAccent(false));
    theme::Palette dark = theme::Create(true, theme::SystemAccent(true));
    gfx::SetTextSurface(light.bg);
    True(L"light theme: the normal text is darker than its ground", !gfx::IsLightOnDark(light.text));
    True(L"light theme: so is the secondary text", !gfx::IsLightOnDark(light.textSecondary));
    True(L"light theme: so is the disabled text", !gfx::IsLightOnDark(light.textDisabled));
    True(L"light theme: the white text of the accent key is lighter than its ground", gfx::IsLightOnDark(light.accentText));
    gfx::SetTextSurface(dark.bg);
    True(L"dark theme: the normal text is lighter than its ground", gfx::IsLightOnDark(dark.text));
    True(L"dark theme: so is the secondary text", gfx::IsLightOnDark(dark.textSecondary));
    True(L"dark theme: so is the disabled text", gfx::IsLightOnDark(dark.textDisabled));
    True(L"dark theme: the black text of the accent key is darker than its ground", !gfx::IsLightOnDark(dark.accentText));
    gfx::SetTextSurface(light.bg);
}

// ------------------------------------------------------------------ the loaded modules

static void NoDirectX() {
    g_culture = L"-";
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    bool dx = false;
    std::wstring list;
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = { sizeof me };
        for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
            std::wstring n = me.szModule;
            for (auto& ch : n) ch = (wchar_t)towlower(ch);
            if (n.compare(0, 4, L"dxgi") == 0 || n.compare(0, 3, L"d3d") == 0 || n.compare(0, 4, L"d2d1") == 0) {
                dx = true;
                list += n + L" ";
            }
        }
        CloseHandle(snap);
    }
    True(L"no DirectX module is loaded (" + list + L")", !dx);
}

// ------------------------------------------------------------------ run

#define RUN(fn) do { if (g_trace) { wprintf(L"  %s\n", L#fn); fflush(stdout); } fn(); } while (0)

static void RunAll(const wchar_t* culture, const wchar_t* separator) {
    g_culture = culture;
    tm::SetDecimalSeparatorOverride(separator);
    RUN(ConverterWiring);
    RUN(ConverterEditing);
    RUN(CalculatorWiring);
    RUN(KeyboardRouting);
    RUN(ThemeAndTabs);
    RUN(RealWindow);
    RUN(RealWindowMouse);
    RUN(FieldClipboard);
    RUN(CaretTimerIdle);
    RUN(TripleClick);
    RUN(FirstErase);
    RUN(MenuReopenGuard);
    RUN(HeaderAndGeometry);
    RUN(GeometryAgainstWpf);
    RUN(KeypadStaysPut);
    RUN(ValuePairsShareASize);
    RUN(DisabledAndPressedColors);
    RUN(ExpressionLineFitsItsSlot);
    RUN(Fitting);
    RUN(AdvancedMenu);
    RUN(ThemeMenu);
    RUN(ThemeFollowsWindows);
    RUN(ThemePersistence);
    RUN(ThemeMenuPosition);
    RUN(ThemeDialogs);
    RUN(ThemeRepaints);
    RUN(FlyoutClicks);
    RUN(AboutAndLicense);
    RUN(EmbeddedSource);
    RUN(IconFrames);
    RUN(WindowTyping);
    RUN(WindowConverterState);
    RUN(WindowFocusSelection);
    RUN(WindowConvertKeepsValue);
    RUN(WindowKeyboardMapping);
    RUN(WindowCalcWiring);
    RUN(WindowExpressionLine);
    RUN(DpiAndTheme);
}

// Where the settings file lives (SPEC 6c) is found from the environment (LOCALAPPDATA) without the shell: asking the shell for the
// known folder (SHGetKnownFolderPath) loads its storage DLLs, which costs about 20 ms and a dozen modules at every start-up. (A module
// that some other part of the program loaded earlier does not count, so the check only fails when this call is what loaded it.)
static void DefaultPathCost() {
    bool environment = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0) > 0;
    bool had = GetModuleHandleW(L"windows.storage.dll") != nullptr;
    std::wstring p = hc::settings::DefaultPath();
    True(L"the default path of the settings file ends in HourCalc\\HourCalc.ini", p.size() > 21 && p.compare(p.size() - 21, 21, L"HourCalc\\HourCalc.ini") == 0);
    True(L"... it did not load the shell storage DLL (windows.storage.dll) to find it", !environment || had || GetModuleHandleW(L"windows.storage.dll") == nullptr);
}

int Run(HINSTANCE hInst) {
    g_inst = hInst;
    g_trace = GetEnvironmentVariableW(L"HC_TRACE", nullptr, 0) > 0;
    DefaultPathCost();
    if (!gfx::Init()) {
        wprintf(L"the drawing engine could not start\n");
        return 2;
    }
    FirstWindowCost();
    GridDistribution();
    ClipboardModule();
    FlyoutRebuild();
    LicenseViewer();
    AboutDpiChange();
    DrawingFailures();
    ScaleHysteresis();
    TextPolarity();
    RunAll(L"nl-BE", L",");
    RunAll(L"en-US", L".");
    tm::SetDecimalSeparatorOverride(L"");
    NoDirectX();
    wprintf(L"\nHourCalc UI tests: %d checks, %d failed.\n", g_checks, g_failures);
    gfx::Shutdown();
    return g_failures == 0 ? 0 : 1;
}

}  // namespace selftest
