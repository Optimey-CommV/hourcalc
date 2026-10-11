// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the hooks of the test build (HC_TESTHOOKS), see testhooks.h:
//   --render <dir>        renders every state of the WPF snapshot list into <dir> (no window is shown), and the states of the
//                         theme choice (SPEC 6b: the menu, a theme forced against the theme of Windows)
//   --renderdpi <dir>     renders a few states at 100, 125, 150, 175 and 200 % with a device pixel layout (the states
//                         of a WPF reference made with VisualTreeHelper.SetRootDpi)
//   --selftest            UI-level behaviour tests (selftest.cpp)
//   --measure-startup     shows the window without activating it, prints the start-up time and the loaded modules

#include "testhooks.h"
#include "app.h"
#include "clipboard.h"
#include "dialogs.h"
#include "gdip.h"
#include "ui.h"
#include "selftest.h"

#include <stdio.h>
#include <tlhelp32.h>

namespace app {
extern void (*g_firstPaintHook)();
}

namespace testhooks {

using app::MainWindow;
using gfx::Canvas;
using ui::Rn;

static std::wstring g_dir;
static int g_written = 0;

static bool Clsid(const wchar_t* mime, CLSID* out) {
    UINT n = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&n, &size);
    if (!size) return false;
    Gdiplus::ImageCodecInfo* info = static_cast<Gdiplus::ImageCodecInfo*>(malloc(size));
    Gdiplus::GetImageEncoders(n, size, info);
    bool ok = false;
    for (UINT i = 0; i < n; i++)
        if (wcscmp(info[i].MimeType, mime) == 0) {
            *out = info[i].Clsid;
            ok = true;
            break;
        }
    free(info);
    return ok;
}

bool SavePng(const Canvas& c, const wchar_t* path) {
    GdiFlush();
    CLSID clsid;
    if (!Clsid(L"image/png", &clsid)) return false;
    // The canvas is an opaque top-down BGRA DIB (alpha is ignored by the RGB pixel format).
    Gdiplus::Bitmap bmp(c.W(), c.H(), (INT)(c.Row(1) - c.Row(0)), PixelFormat32bppRGB, c.Row(0));
    return bmp.Save(path, &clsid, nullptr) == Gdiplus::Ok;
}

static void Save(const Canvas& c, const wchar_t* name) {
    std::wstring path = g_dir + L"\\" + name + L".png";
    if (SavePng(c, path.c_str())) g_written++;
    else fwprintf(stderr, L"could not write %s\n", path.c_str());
}

// ------------------------------------------------------------------ --render

static const int DefaultW = 324, DefaultH = 521, MinW = 284, MinH = 441, LargeW = 584, LargeH = 861;

static MainWindow* NewWin(bool dark) {
    MainWindow* w = new MainWindow(nullptr);       // no settings file: always 2 decimals
    w->ApplyTheme(dark);
    return w;
}

static MainWindow* NewCalc(bool dark) {
    MainWindow* w = NewWin(dark);
    w->SelectTab(1);
    return w;
}

// The accents of this machine, read once before Windows is simulated (the simulation answers with these).
static gfx::Argb g_accentLight = 0, g_accentDark = 0;

// A window with a forced theme choice while Windows itself is in the other theme: Windows is simulated (theme::SetSystemForTest)
// for the moment the window is made and the choice is set; nothing of the machine's settings is touched and no settings file is
// involved. Free the window with DeleteForced.
static MainWindow* NewForced(hc::settings::ThemeMode mode, bool windowsDark) {
    theme::SetSystemForTest(true, windowsDark, g_accentLight, g_accentDark);
    MainWindow* w = new MainWindow(nullptr);
    w->SetThemeMode(mode);
    return w;
}

static void DeleteForced(MainWindow* w) {
    delete w;
    theme::SetSystemForTest(false, false, 0, 0);
}

static void Keys(MainWindow* w, const wchar_t* keys) {
    for (const wchar_t* p = keys; *p; p++) w->Press(std::wstring(1, *p));
}

static void TypeLive(MainWindow* w, const wchar_t* keys, int cw, int ch) {
    w->LayoutAt(cw, ch, 1.0f);
    for (const wchar_t* p = keys; *p; p++) {
        w->Press(std::wstring(1, *p));
        w->LayoutAt(cw, ch, 1.0f);
    }
}

static void SaveWin(MainWindow* w, int wDip, int hDip, float dpi, const std::wstring& name) {
    int pw = (int)Rn(wDip * dpi), ph = (int)Rn(hDip * dpi);
    w->LayoutAt(pw, ph, dpi);
    Canvas c;
    w->Render(c);
    Save(c, name.c_str());
}

static void Report(const std::wstring& what, const std::wstring& a, const std::wstring& b, const std::wstring& c) {
    wprintf(L"%s: <%s> <%s> <%s>\n", what.c_str(), a.c_str(), b.c_str(), c.c_str());
}

static int RenderAll(HINSTANCE hInst, const std::wstring& dir) {
    g_dir = dir;
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!gfx::Init()) return 1;
    g_accentLight = theme::SystemAccent(false);
    g_accentDark = theme::SystemAccent(true);
    for (int pass = 0; pass < 2; pass++) {
        bool dark = pass == 1;
        std::wstring th = dark ? L"dark" : L"light";
        MainWindow* w = NewWin(dark);
        SaveWin(w, DefaultW, DefaultH, 1.0f, th + L"-converter-idle");
        delete w;

        w = NewWin(dark);
        w->SetConverterText(0, L"08:30");
        SaveWin(w, DefaultW, DefaultH, 1.0f, th + L"-converter-left");
        Report(th + L" converter left", w->GetConverterText(0), w->GetConverterText(1), w->ConverterCaption());
        delete w;

        w = NewWin(dark);
        w->SetConverterText(1, L"");
        Keys(w, L"8,5");
        SaveWin(w, DefaultW, DefaultH, 1.0f, th + L"-converter-right");
        Report(th + L" converter right", w->GetConverterText(0), w->GetConverterText(1), w->ConverterCaption());
        delete w;

        w = NewWin(dark);
        w->SetConverterText(1, L"1234567,123456");
        SaveWin(w, DefaultW, DefaultH, 1.0f, th + L"-converter-long");
        delete w;

        w = NewCalc(dark);
        Keys(w, L"7:30+7:20=");
        SaveWin(w, DefaultW, DefaultH, 1.0f, th + L"-calc-result");
        Report(th + L" calc result", w->Engine().MainText(), w->Engine().DecimalText(), w->Engine().Expression());
        delete w;

        w = NewCalc(dark);
        Keys(w, L"7:30+7:2");
        SaveWin(w, DefaultW, DefaultH, 1.0f, th + L"-calc-entry");
        Report(th + L" calc entry", w->Engine().MainText(), w->Engine().DecimalText(), w->Engine().Expression());
        delete w;

        w = NewCalc(dark);
        Keys(w, L"7:30/0=");
        SaveWin(w, DefaultW, DefaultH, 1.0f, th + L"-calc-divzero");
        Report(th + L" calc divzero", w->Engine().MainText(), w->Engine().DecimalText(), w->Engine().Expression());
        delete w;

        w = NewCalc(dark);
        TypeLive(w, L"7:30+7:20+8:00+1:15+2:45+3:10+4:00+5:00+6:00+", DefaultW, DefaultH);
        SaveWin(w, DefaultW, DefaultH, 1.0f, th + L"-calc-chain");
        w->LayoutAt(LargeW, LargeH, 1.0f);
        SaveWin(w, DefaultW, DefaultH, 1.0f, th + L"-calc-chain-after-resize");
        delete w;

        // The Advanced menu, the Copy menu, a tool tip, the dialogs.
        w = NewWin(dark);
        {
            w->LayoutAt(DefaultW, DefaultH, 1.0f);
            Canvas c;
            w->Render(c);
            flyout::Flyout& m = w->MenuFlyout();
            m.SetPalette(w->Palette());
            m.Layout(nullptr, 1.0f);
            const app::Layout& L = w->GetLayout();
            m.ComposeOver(c, (int)Rn(L.menuBtn.x + 4.0f), (int)Rn(L.menuBtn.bottom() + 2.0f));
            Save(c, (th + L"-menu").c_str());
        }
        delete w;

        w = NewCalc(dark);
        Keys(w, L"7:30+7:20=");
        {
            w->LayoutAt(DefaultW, DefaultH, 1.0f);
            Canvas c;
            w->Render(c);
            flyout::Flyout& m = w->CopyFlyout();
            m.SetPalette(w->Palette());
            m.Layout(nullptr, 1.0f);
            m.ComposeOver(c, 112, 102);
            Save(c, (th + L"-calc-copy").c_str());
        }
        delete w;

        w = NewWin(dark);
        {
            w->LayoutAt(DefaultW, DefaultH, 1.0f);
            Canvas c;
            w->Render(c);
            flyout::Flyout& t = w->TipFlyout();
            t.SetPalette(w->Palette());
            t.SetText(L"Keep on top");
            t.Layout(nullptr, 1.0f);
            const app::Layout& L = w->GetLayout();
            t.ComposeOver(c, (int)Rn(L.pin.x - 62.0f), (int)Rn(L.pin.bottom() + 4.0f));
            Save(c, (th + L"-tooltip").c_str());
        }
        delete w;

        {
            theme::Palette pal = theme::Create(dark, theme::SystemAccent(dark));
            dialogs::Dialog about(dialogs::Dialog::About, pal, 1.0f, hInst);
            about.Layout(440, 0);
            Canvas c;
            about.Render(c);
            Save(c, (th + L"-about").c_str());
            dialogs::Dialog lic(dialogs::Dialog::License, pal, 1.0f, hInst);
            lic.Layout(660, 420);
            Canvas c2;
            lic.Render(c2);
            Save(c2, (th + L"-license").c_str());
        }
    }

    // Light only: other sizes and 150 % DPI.
    MainWindow* s = NewCalc(false);
    Keys(s, L"7:30+7:20=");
    SaveWin(s, MinW, MinH, 1.0f, L"light-calc-result-min");
    delete s;
    s = NewCalc(false);
    Keys(s, L"7:30+7:20=");
    SaveWin(s, LargeW, LargeH, 1.0f, L"light-calc-result-large");
    delete s;
    s = NewWin(false);
    s->SetConverterText(0, L"08:30");
    SaveWin(s, DefaultW, DefaultH, 1.5f, L"light-converter-150dpi");
    delete s;
    s = NewCalc(false);
    TypeLive(s, L"7:30+7:20+8:00+1:15+2:45+3:10+4:00+5:00+6:00+", MinW, MinH);
    SaveWin(s, MinW, MinH, 1.0f, L"light-calc-chain-min");
    delete s;

    // Decimal places set in the Advanced menu.
    s = NewWin(false);
    s->SetDecimalPlaces(4);
    s->SetConverterText(0, L"08:20");
    SaveWin(s, DefaultW, DefaultH, 1.0f, L"light-converter-4-places");
    Report(L"converter at 4 places", s->GetConverterText(0), s->GetConverterText(1), s->ConverterCaption());
    delete s;
    s = NewCalc(false);
    s->SetDecimalPlaces(3);
    Keys(s, L"7:30/4=");
    SaveWin(s, DefaultW, DefaultH, 1.0f, L"light-calc-3-places");
    Report(L"calc at 3 places", s->Engine().MainText(), s->Engine().DecimalText(), s->Engine().Expression());
    delete s;

    // The theme choice (SPEC 6b): a theme forced against the theme of Windows, the converter and the calculator, and the menu with
    // that choice checked. Dark on a light Windows, Light on a dark Windows.
    struct Forced {
        const wchar_t* name;
        hc::settings::ThemeMode mode;
        bool windowsDark;
    };
    for (const Forced& f : { Forced{ L"forced-dark", hc::settings::ThemeMode::Dark, false }, Forced{ L"forced-light", hc::settings::ThemeMode::Light, true } }) {
        std::wstring name = f.name;
        MainWindow* w = NewForced(f.mode, f.windowsDark);
        w->SetConverterText(0, L"08:30");
        SaveWin(w, DefaultW, DefaultH, 1.0f, name + L"-converter");
        Report(name + L" converter (dark window: " + (w->IsDark() ? L"yes" : L"no") + L")", w->GetConverterText(0), w->GetConverterText(1), w->ConverterCaption());
        DeleteForced(w);

        w = NewForced(f.mode, f.windowsDark);
        w->SelectTab(1);
        Keys(w, L"7:30+7:20=");
        SaveWin(w, DefaultW, DefaultH, 1.0f, name + L"-calc");
        DeleteForced(w);

        w = NewForced(f.mode, f.windowsDark);
        {
            w->LayoutAt(DefaultW, DefaultH, 1.0f);
            Canvas c;
            w->Render(c);
            flyout::Flyout& m = w->MenuFlyout();
            m.SetPalette(w->Palette());
            m.Layout(nullptr, 1.0f);
            const app::Layout& L = w->GetLayout();
            m.ComposeOver(c, (int)Rn(L.menuBtn.x + 4.0f), (int)Rn(L.menuBtn.bottom() + 2.0f));
            Save(c, (name + L"-menu").c_str());
        }
        DeleteForced(w);
    }

    wprintf(L"Wrote %d snapshots to %s\n", g_written, dir.c_str());
    gfx::Shutdown();
    return 0;
}

// ------------------------------------------------------------------ --render2 (extra states for calibration)

static int Render2(const std::wstring& dir) {
    g_dir = dir;
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!gfx::Init()) return 1;
    const int sizes[][2] = { { 324, 521 }, { 284, 441 }, { 584, 861 }, { 400, 600 } };
    const wchar_t* rights[] = { L"1234567,123456", L"123456,12345", L"12345,1234", L"8,5" };
    const wchar_t* lefts[] = { L"1234567:59", L"123456:5", L"12345:07", L"8:30" };
    const wchar_t* calcs[] = { L"9999999:59+9999999=", L"7:30/0=", L"1234567:5", L"12:5+1:2=" };
    for (auto& s : sizes) {
        std::wstring tag = std::to_wstring(s[0]) + L"x" + std::to_wstring(s[1]);
        for (int i = 0; i < 4; i++) {
            MainWindow* w = NewWin(false);
            w->SetConverterText(1, rights[i]);
            SaveWin(w, s[0], s[1], 1.0f, L"r" + std::to_wstring(i) + L"-" + tag);
            wprintf(L"r%d-%s font %.4f box.h %g\n", i, tag.c_str(), w->ConverterFontPx(), w->GetLayout().box[1].h);
            delete w;
            w = NewWin(false);
            w->SetConverterText(0, lefts[i]);
            SaveWin(w, s[0], s[1], 1.0f, L"l" + std::to_wstring(i) + L"-" + tag);
            wprintf(L"l%d-%s font %.4f box.h %g\n", i, tag.c_str(), w->ConverterFontPx(), w->GetLayout().box[0].h);
            delete w;
            w = NewCalc(false);
            Keys(w, calcs[i]);
            SaveWin(w, s[0], s[1], 1.0f, L"c" + std::to_wstring(wcslen(calcs[i])) + L"-" + tag);
            delete w;
        }
    }
    // Focus visuals (no reference in the WPF snapshots): a selected value and a caret.
    {
        MainWindow* w = NewWin(false);
        w->SetWindowActive(true);
        w->SetConverterText(0, L"08:30");
        w->GotFocus(0, true);
        SaveWin(w, 324, 521, 1.0f, L"x-sel");
        w->FieldRef(0).anchor = w->FieldRef(0).caret = 3;
        SaveWin(w, 324, 521, 1.0f, L"x-caret");
        delete w;
        w = NewWin(true);
        w->SetWindowActive(true);
        w->SetConverterText(1, L"8,5");
        w->GotFocus(1, true);
        SaveWin(w, 324, 521, 1.0f, L"x-sel-dark");
        delete w;
        // The Cut / Copy / Paste menu of a converter field, with Paste greyed out (no reference in the WPF snapshots).
        for (int pass = 0; pass < 2; pass++) {
            w = NewWin(pass == 1);
            w->SetWindowActive(true);
            w->SetConverterText(0, L"08:30");
            w->GotFocus(0, true);
            w->LayoutAt(324, 521, 1.0f);
            Canvas c;
            w->Render(c);
            w->PrepareFieldMenu(0);
            flyout::Flyout& m = w->FieldMenuFlyout();
            m.SetEnabled(app::kCmdPaste, false);
            m.SetHot(app::kCmdFieldCopy, false);
            m.ComposeOver(c, 150, 130);
            Save(c, pass == 1 ? L"x-fieldmenu-dark" : L"x-fieldmenu");
            delete w;
        }
    }
    gfx::Shutdown();
    return 0;
}

// ------------------------------------------------------------------ --renderdpi

static int RenderDpi(const std::wstring& dir) {
    g_dir = dir;
    CreateDirectoryW(dir.c_str(), nullptr);
    if (!gfx::Init()) return 1;
    for (float d : { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f }) {
        std::wstring tag = L"d" + std::to_wstring((int)Rn(d * 100.0f));
        MainWindow* w = NewWin(false);
        SaveWin(w, DefaultW, DefaultH, d, tag + L"-conv-idle");
        delete w;
        w = NewWin(false);
        w->SetConverterText(0, L"08:30");
        SaveWin(w, DefaultW, DefaultH, d, tag + L"-conv-left");
        SaveWin(w, MinW, MinH, d, tag + L"-conv-left-min");
        SaveWin(w, LargeW, LargeH, d, tag + L"-conv-left-large");
        delete w;
        w = NewWin(true);
        w->SetConverterText(1, L"8,5");
        SaveWin(w, DefaultW, DefaultH, d, tag + L"-conv-dark-right");
        delete w;
        w = NewCalc(false);
        Keys(w, L"7:30+7:20=");
        SaveWin(w, DefaultW, DefaultH, d, tag + L"-calc-result");
        delete w;
        w = NewCalc(false);
        Keys(w, L"7:30/0=");
        SaveWin(w, DefaultW, DefaultH, d, tag + L"-calc-divzero");
        delete w;
    }
    gfx::Shutdown();
    return 0;
}

// ------------------------------------------------------------------ --geometry (layout dump for comparison with the WPF build)

static void GeoLine(FILE* f, const char* kind, const std::wstring& name, float x, float y, float w, float h) {
    fprintf(f, "%s\t%ls\t%g\t%g\t%g\t%g\n", kind, name.c_str(), x, y, w, h);
}

static void GeoDump(FILE* f, MainWindow* w, const char* tag, int cw, int ch, float dpi = 1.0f) {
    w->LayoutAt(cw, ch, dpi);
    const app::Layout& L = w->GetLayout();
    fprintf(f, "## %s %dx%d\n", tag, cw, ch);
    GeoLine(f, "Button", L"Menu", L.menuBtn.x, L.menuBtn.y, L.menuBtn.w, L.menuBtn.h);
    GeoLine(f, "Radio", L"Converter", L.tab[0].x, 0, L.tab[0].w, L.headerH);
    GeoLine(f, "TextBlock", L"Converter", L.tabTextX[0], L.tabTextY, L.tabTextW[0], 0);
    GeoLine(f, "Radio", L"Calculator", L.tab[1].x, 0, L.tab[1].w, L.headerH);
    GeoLine(f, "TextBlock", L"Calculator", L.tabTextX[1], L.tabTextY, L.tabTextW[1], 0);
    GeoLine(f, "Button", L"Keep on top", L.pin.x, L.pin.y, L.pin.w, L.pin.h);
    float d = L.d, s = L.s;
    gfx::Font fLbl = ui::TextFont(12.0f * s * d), fCap = ui::TextFont(14.0f * s * d);
    float w0 = Rn(gfx::TextWidth(L"Hours : minutes", 15, fLbl)), w1 = Rn(gfx::TextWidth(L"Decimal hours", 13, fLbl));
    if (w->CurrentTab() == 0) {
        GeoLine(f, "TextBlock", L"Hours : minutes", L.lbl[0].x, L.lbl[0].y, w0, L.lbl[0].h);
        GeoLine(f, "TextBlock", L"Decimal hours", L.lbl[1].x, L.lbl[1].y, w1, L.lbl[1].h);
        GeoLine(f, "TextBox", w->GetConverterText(0), L.box[0].x, L.box[0].y, L.box[0].w, L.box[0].h);
        GeoLine(f, "TextBox", w->GetConverterText(1), L.box[1].x, L.box[1].y, L.box[1].w, L.box[1].h);
        GeoLine(f, "Underline", L"", L.under[0].x, L.under[0].y, L.under[0].w, L.under[0].h);
        const std::wstring& cap = w->ConverterCaption();
        float cwid = Rn(gfx::TextWidth(cap.c_str(), (int)cap.size(), fCap));
        GeoLine(f, "TextBlock", cap, L.cap.right() - cwid, L.cap.y, cwid, L.cap.h);
    } else {
        std::wstring expr = L"";
        GeoLine(f, "TextBlock", w->ExpressionShown(), L.expr.x, L.expr.y, L.expr.w, L.expr.h);
        GeoLine(f, "TextBlock", L"Hours : minutes", L.calcLbl[0].x, L.calcLbl[0].y, w0, L.calcLbl[0].h);
        if (w->CalcRightVisible()) GeoLine(f, "TextBlock", L"Decimal hours", L.calcLbl[1].x, L.calcLbl[1].y, w1, L.calcLbl[1].h);
        GeoLine(f, "TextBlock", w->CalcMain(), L.calcVal[0].x, L.calcVal[0].y, L.calcVal[0].w, L.calcVal[0].h);
        if (w->CalcRightVisible()) GeoLine(f, "TextBlock", w->CalcDecimal(), L.calcVal[1].x, L.calcVal[1].y, L.calcVal[1].w, L.calcVal[1].h);
        const std::wstring& cap = w->CalcCaption();
        float cwid = Rn(gfx::TextWidth(cap.c_str(), (int)cap.size(), fCap));
        GeoLine(f, "TextBlock", cap, L.calcCap.right() - cwid, L.calcCap.y, cwid, L.calcCap.h);
    }
    GeoLine(f, "FontSize", L"conv", w->ConverterFontPx(), 0, 0, 0);
    GeoLine(f, "FontSize", L"calc", w->CalcFontPx(), 0, 0, 0);
    for (size_t i = 0; i < L.keys.size(); i++) {
        const app::KeyDef& k = w->KeyAt((int)i);
        GeoLine(f, "Button", k.name, L.keys[i].x, L.keys[i].y, L.keys[i].w, L.keys[i].h);
        gfx::RectF tb = app::KeyTextBox(k, L.keys[i], d, s);
        GeoLine(f, "KeyText", k.name, tb.x, tb.y, tb.w, tb.h);
    }
}

// The layout of a few states at 100 to 200 %, in device pixels (to compare with a WPF layout made with SetRootDpi).
static int GeometryDpi(const std::wstring& file) {
    if (!gfx::Init()) return 1;
    FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"wb") != 0 || !f) return 1;
    for (float d : { 1.0f, 1.25f, 1.5f, 1.75f, 2.0f }) {
        char tag[64];
        sprintf_s(tag, "d%d-conv-left", (int)Rn(d * 100.0f));
        MainWindow* w = NewWin(false);
        w->SetConverterText(0, L"08:30");
        GeoDump(f, w, tag, (int)Rn(324 * d), (int)Rn(521 * d), d);
        delete w;
        sprintf_s(tag, "d%d-calc-result", (int)Rn(d * 100.0f));
        w = NewCalc(false);
        Keys(w, L"7:30+7:20=");
        GeoDump(f, w, tag, (int)Rn(324 * d), (int)Rn(521 * d), d);
        delete w;
        sprintf_s(tag, "d%d-conv-left-min", (int)Rn(d * 100.0f));
        w = NewWin(false);
        w->SetConverterText(0, L"08:30");
        GeoDump(f, w, tag, (int)Rn(284 * d), (int)Rn(441 * d), d);
        delete w;
        sprintf_s(tag, "d%d-conv-left-large", (int)Rn(d * 100.0f));
        w = NewWin(false);
        w->SetConverterText(0, L"08:30");
        GeoDump(f, w, tag, (int)Rn(584 * d), (int)Rn(861 * d), d);
        delete w;
    }
    fclose(f);
    gfx::Shutdown();
    return 0;
}

static int Geometry(const std::wstring& file) {
    if (!gfx::Init()) return 1;
    FILE* f = nullptr;
    if (_wfopen_s(&f, file.c_str(), L"wb") != 0 || !f) return 1;
    const int sizes[][2] = { { 324, 521 }, { 284, 441 }, { 584, 861 }, { 400, 600 }, { 500, 521 }, { 324, 700 }, { 300, 480 }, { 700, 500 }, { 330, 530 }, { 341, 563 } };
    for (auto& s : sizes) {
        MainWindow* w = NewWin(false);
        w->SetConverterText(0, L"08:30");
        GeoDump(f, w, "conv", s[0], s[1]);
        delete w;
        w = NewCalc(false);
        Keys(w, L"7:30+7:20=");
        GeoDump(f, w, "calc", s[0], s[1]);
        delete w;
    }
    for (auto& s : sizes) {
        if (s[0] != 324 && s[0] != 284 && s[0] != 584) continue;
        MainWindow* w = NewWin(false);
        w->SetConverterText(1, L"1234567,123456");
        GeoDump(f, w, "convlong", s[0], s[1]);
        delete w;
        w = NewCalc(false);
        Keys(w, L"9999999:59+9999999=");
        GeoDump(f, w, "calclong", s[0], s[1]);
        delete w;
        w = NewCalc(false);
        Keys(w, L"7:30/0=");
        GeoDump(f, w, "calcerr", s[0], s[1]);
        delete w;
    }
    fclose(f);
    gfx::Shutdown();
    return 0;
}

// ------------------------------------------------------------------ --popup-check
// Opens the real popups on the screen for a moment (shown without activating anything) and drives them with
// messages: the menu button, a menu item, a click outside, Esc, the Copy menu, the tool tip.

static void Pump(int ms) {
    DWORD end = GetTickCount() + (DWORD)ms;
    MSG m;
    while ((int)(end - GetTickCount()) > 0) {
        while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
        Sleep(5);
    }
}

static int g_pcChecks = 0, g_pcFailed = 0;

static void PcCheck(const wchar_t* what, bool ok) {
    g_pcChecks++;
    if (!ok) g_pcFailed++;
    wprintf(L"  %s: %s\n", ok ? L"ok  " : L"FAIL", what);
}

static void MouseTo(HWND h, UINT msg, int x, int y) {
    SendMessageW(h, msg, msg == WM_LBUTTONDOWN ? MK_LBUTTON : 0, MAKELPARAM(x, y));
}

static void ClickAt(HWND h, int x, int y) {
    MouseTo(h, WM_MOUSEMOVE, x, y);
    MouseTo(h, WM_LBUTTONDOWN, x, y);
    MouseTo(h, WM_LBUTTONUP, x, y);
}

// A click on the main window while a popup is open reaches the popup first (it has the mouse capture), as a real click does:
// the point is a client point of the main window.
static void ClickThroughPopup(flyout::Flyout& popup, HWND main, int clientX, int clientY) {
    POINT sp = { clientX, clientY };
    ClientToScreen(main, &sp);
    RECT fr = {};
    GetWindowRect(popup.hwnd(), &fr);
    MouseTo(popup.hwnd(), WM_LBUTTONDOWN, sp.x - fr.left, sp.y - fr.top);
}

// Safety net of the on-screen check: whatever goes wrong (a modal loop that waits for a real mouse), the window of this
// test never stays on the screen for more than a short while.
static DWORD WINAPI Watchdog(LPVOID) {
    Sleep(60000);
    ExitProcess(3);
}

static int PopupCheck(HINSTANCE hInst) {
    CreateThread(nullptr, 0, Watchdog, nullptr, 0, nullptr);
    if (!gfx::Init()) return 1;
    {
        MainWindow w(nullptr);
        w.SetMain();
        if (!w.Create(hInst, false)) return 1;
        HWND h = w.hwnd();
        ShowWindow(h, SW_SHOWNOACTIVATE);
        Pump(400);
        const app::Layout& L = w.GetLayout();
        // 1. the menu button opens the menu
        ClickAt(h, (int)(L.menuBtn.x + L.menuBtn.w / 2), (int)(L.menuBtn.y + L.menuBtn.h / 2));
        Pump(150);
        PcCheck(L"the menu button opens the Advanced menu", w.IsMenuOpen());
        flyout::Flyout& menu = w.MenuFlyout();
        PcCheck(L"the menu window is visible", menu.hwnd() && IsWindowVisible(menu.hwnd()));
        RECT mr = {};
        if (menu.hwnd()) GetWindowRect(menu.hwnd(), &mr);
        POINT bt = { (LONG)L.menuBtn.x, (LONG)L.menuBtn.bottom() };
        ClientToScreen(h, &bt);
        // (4 px right of and 2 px below the button, or moved up / left to stay inside the work area: the menu is tall)
        POINT where = w.MenuPosition();
        PcCheck(L"the menu box opens at its position: 4 px right of and 2 px below the button, moved only to stay on the monitor",
                mr.left + menu.ShadowRoom() == where.x && mr.top + menu.ShadowRoom() == where.y && where.x >= bt.x + 4 - menu.BoxWidth() && where.y <= bt.y + 2);
        // 2. clicking the item "3" sets 3 decimal places and closes the menu
        int idx = -1;
        for (size_t i = 0; i < menu.Items().size(); i++)
            if (menu.Items()[i].type == flyout::Item::Radio && menu.Items()[i].id == 3) idx = (int)i;
        if (idx >= 0 && menu.hwnd()) {
            const flyout::Item& it = menu.Items()[(size_t)idx];
            ClickAt(menu.hwnd(), menu.ShadowRoom() + (int)(it.rect.x + it.rect.w / 2), menu.ShadowRoom() + (int)(it.rect.y + it.rect.h / 2));
            Pump(150);
        }
        PcCheck(L"clicking the item 3 sets 3 decimal places", w.DecimalPlaces() == 3);
        PcCheck(L"... and closes the menu", !w.IsMenuOpen());
        w.SetDecimalPlaces(2);
        // 3. a click outside the box closes the menu without a command
        Pump(280);       // a click on the button right after the menu closed is ignored (the guard of the WPF build)
        ClickAt(h, (int)(L.menuBtn.x + L.menuBtn.w / 2), (int)(L.menuBtn.y + L.menuBtn.h / 2));
        Pump(100);
        PcCheck(L"the menu opens again", w.IsMenuOpen());
        // (a spot of the display where a click does nothing)
        if (menu.hwnd()) ClickThroughPopup(menu, h, L.W - 8, (int)L.headerH + 10);
        Pump(100);
        PcCheck(L"a click outside the box closes the menu", !w.IsMenuOpen());
        PcCheck(L"... and changes nothing", w.DecimalPlaces() == 2);
        // 4. Esc closes it
        Pump(280);
        ClickAt(h, (int)(L.menuBtn.x + L.menuBtn.w / 2), (int)(L.menuBtn.y + L.menuBtn.h / 2));
        Pump(100);
        SendMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
        Pump(100);
        PcCheck(L"Esc closes the menu", !w.IsMenuOpen());
        PcCheck(L"... and does not reset the converter", w.CurrentTab() == 0);
        // 4b. a menu that is open does not stay behind when the window moves or changes size
        auto openMenu = [&]() {
            Pump(280);       // a click on the button right after the menu closed is ignored (the guard of the WPF build)
            ClickAt(h, (int)(L.menuBtn.x + L.menuBtn.w / 2), (int)(L.menuBtn.y + L.menuBtn.h / 2));
            Pump(150);
            return w.IsMenuOpen();
        };
        RECT wr;
        GetWindowRect(h, &wr);
        PcCheck(L"the menu opens (for the move test)", openMenu());
        SetWindowPos(h, nullptr, wr.left + 12, wr.top + 4, 0, 0, SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
        Pump(100);
        PcCheck(L"moving the window closes the menu", !w.IsMenuOpen());
        PcCheck(L"the menu opens again (for the size test)", openMenu());
        SetWindowPos(h, nullptr, 0, 0, wr.right - wr.left + 20, wr.bottom - wr.top + 20, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        Pump(100);
        PcCheck(L"resizing the window closes the menu", !w.IsMenuOpen());
        SetWindowPos(h, nullptr, 0, 0, wr.right - wr.left, wr.bottom - wr.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        Pump(100);
        // 4c. a click outside the menu closes it and goes on to the key under the pointer, like the WPF popup
        w.Press(L"RESET");
        PcCheck(L"the menu opens (for the click-through test)", openMenu());
        {
            const app::Layout& LL = w.GetLayout();
            // (a key outside the menu: the menu covers most of the keypad, so the key of the right hand column that the menu
            // leaves free is the one to press)
            RECT menuRect = {};
            GetWindowRect(menu.hwnd(), &menuRect);
            POINT origin = { 0, 0 };
            ClientToScreen(h, &origin);
            int kx = 0, ky = 0;
            wchar_t digit = 0;
            for (const wchar_t* d : { L"9", L"6", L"3", L"0" }) {
                const gfx::RectF& k = LL.keys[(size_t)w.KeyIndex(d)];
                int cx = (int)(k.x + k.w / 2), cy = (int)(k.y + k.h / 2);
                if (origin.x + cx > menuRect.right - menu.ShadowRoom() || origin.y + cy > menuRect.bottom - menu.ShadowRoom() ||
                    origin.x + cx < menuRect.left + menu.ShadowRoom() || origin.y + cy < menuRect.top + menu.ShadowRoom()) {
                    kx = cx;
                    ky = cy;
                    digit = d[0];
                    break;
                }
            }
            PcCheck(L"a key of the keypad lies outside the menu", digit != 0);
            ClickThroughPopup(menu, h, kx, ky);
            Pump(60);
            PcCheck(L"a click on a key outside the menu closes the menu", !w.IsMenuOpen());
            MouseTo(h, WM_LBUTTONUP, kx, ky);
            Pump(60);
            PcCheck(L"... and presses the key (the first click is not lost)", digit != 0 && w.GetConverterText(0) == std::wstring(1, digit));
        }
        w.Press(L"RESET");
        // 4d. the click that closes the menu on its own button does not open it again
        PcCheck(L"the menu opens (for the button test)", openMenu());
        {
            const app::Layout& LL = w.GetLayout();
            int bx = (int)(LL.menuBtn.x + LL.menuBtn.w / 2), by = (int)(LL.menuBtn.y + LL.menuBtn.h / 2);
            ClickThroughPopup(menu, h, bx, by);
            Pump(60);
            PcCheck(L"a click on the menu button closes the open menu", !w.IsMenuOpen());
            MouseTo(h, WM_LBUTTONUP, bx, by);
            Pump(100);
            PcCheck(L"... and does not open it again", !w.IsMenuOpen());
        }
        // 4e. the Cut / Copy / Paste menu of a converter field (nothing is clicked: that would write the real clipboard)
        w.Press(L"RESET");
        w.SetConverterText(0, L"08:30");
        {
            const app::Layout& LL = w.GetLayout();
            int fx = (int)(LL.box[1].x + LL.box[1].w / 2), fy = (int)(LL.box[1].y + LL.box[1].h / 2);
            SendMessageW(h, WM_RBUTTONUP, 0, MAKELPARAM(fx, fy));
            Pump(150);
            flyout::Flyout& fm = w.FieldMenuFlyout();
            PcCheck(L"a right click on a converter field opens the Cut / Copy / Paste menu", w.IsFieldMenuOpen() && fm.hwnd() && IsWindowVisible(fm.hwnd()));
            PcCheck(L"... the field it was opened on has the focus and is selected", w.FocusField() == 1 && w.FieldRef(1).selLength() == (int)w.GetConverterText(1).size());
            const std::vector<flyout::Item>& it = fm.Items();
            PcCheck(L"... Cut and Copy are enabled (there is a selection)", it.size() == 3 && !it[0].disabled && !it[1].disabled);
            PcCheck(L"... Paste is enabled when the clipboard holds text", it.size() == 3 && it[2].disabled == !clip::HasText());
            if (it.size() == 3 && it[2].disabled) {
                MouseTo(fm.hwnd(), WM_LBUTTONDOWN, fm.ShadowRoom() + (int)(it[2].rect.x + it[2].rect.w / 2), fm.ShadowRoom() + (int)(it[2].rect.y + it[2].rect.h / 2));
                Pump(60);
                PcCheck(L"a click on a greyed out item does nothing: the menu stays", w.IsFieldMenuOpen());
            }
            SendMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
            Pump(100);
            PcCheck(L"Esc closes the field menu", !w.IsFieldMenuOpen());
            PcCheck(L"... and does not reset the converter", w.GetConverterText(0) == L"08:30");
        }
        w.Press(L"RESET");
        // 5. the Copy menu of the calculator values
        w.SelectTab(1);
        for (wchar_t c : std::wstring(L"7:30+7:20=")) SendMessageW(h, WM_CHAR, c, 0);
        Pump(100);
        const app::Layout& L2 = w.GetLayout();
        int vx = (int)(L2.calcVal[0].x + L2.calcVal[0].w / 2), vy = (int)(L2.calcVal[0].y + L2.calcVal[0].h / 2);
        SendMessageW(h, WM_RBUTTONUP, 0, MAKELPARAM(vx, vy));
        Pump(150);
        flyout::Flyout& cp = w.CopyFlyout();
        PcCheck(L"a right click on a result value opens the Copy menu", cp.IsOpen() && cp.hwnd() && IsWindowVisible(cp.hwnd()));
        SendMessageW(h, WM_KEYDOWN, VK_ESCAPE, 0);
        Pump(100);
        PcCheck(L"Esc closes the Copy menu", !cp.IsOpen());
        PcCheck(L"... and does not clear the calculator", w.Engine().MainText() == L"14:50");
        // 6. the tool tip of the pin button appears after a moment and goes away
        const app::Layout& L3 = w.GetLayout();
        int px = (int)(L3.pin.x + L3.pin.w / 2), py = (int)(L3.pin.y + L3.pin.h / 2);
        // Messages to our own window; the real pointer is not moved (if the real pointer is over the window it
        // sends its own mouse moves, so the hover is repeated).
        MouseTo(h, WM_MOUSEMOVE, px, py);
        SendMessageW(h, WM_TIMER, 2 /* the tool tip delay timer of the window */, 0);
        flyout::Flyout& tip = w.TipFlyout();
        PcCheck(L"hovering the pin shows its tool tip", tip.IsOpen() && tip.Text() == L"Keep on top");
        SendMessageW(h, WM_MOUSELEAVE, 0, 0);
        Pump(100);
        PcCheck(L"leaving the pin hides the tool tip", !tip.IsOpen());
        // modules after the popups were shown
        HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
        bool dx = false;
        if (snap != INVALID_HANDLE_VALUE) {
            MODULEENTRY32W me = { sizeof me };
            for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
                std::wstring n = me.szModule;
                for (auto& ch : n) ch = (wchar_t)towlower(ch);
                if (n.compare(0, 4, L"dxgi") == 0 || n.compare(0, 3, L"d3d") == 0 || n.compare(0, 4, L"d2d1") == 0) dx = true;
            }
            CloseHandle(snap);
        }
        PcCheck(L"no DirectX module is loaded after all popups were shown", !dx);
    }
    gfx::Shutdown();
    wprintf(L"popup check: %d checks, %d failed\n", g_pcChecks, g_pcFailed);
    return g_pcFailed == 0 ? 0 : 1;
}

// ------------------------------------------------------------------ --measure-startup

static ULONGLONG FileTimeToU64(const FILETIME& f) {
    return ((ULONGLONG)f.dwHighDateTime << 32) | f.dwLowDateTime;
}

static double g_phase[4];     // ms since the process was created: wWinMain entered, drawing engine up, window created, first paint

static double SinceStart() {
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimePreciseAsFileTime(&now);
    return (double)(FileTimeToU64(now) - FileTimeToU64(created)) / 10000.0;
}

static bool g_firstPaintDone = false;

static void PrintStartup() {
    if (!g_firstPaintDone) g_phase[3] = SinceStart();
    wprintf(L"phases_ms: main=%.0f engine=%.0f window=%.0f paint=%.0f\n", g_phase[0], g_phase[1], g_phase[2], g_phase[3]);
    FILETIME created, exited, kernel, user, now;
    GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user);
    GetSystemTimePreciseAsFileTime(&now);
    double ms = (double)(FileTimeToU64(now) - FileTimeToU64(created)) / 10000.0;
    wprintf(L"startup_ms=%.0f\n", ms);
    wprintf(L"cpu_ms=%.0f\n", (double)(FileTimeToU64(kernel) + FileTimeToU64(user)) / 10000.0);
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, GetCurrentProcessId());
    std::wstring list;
    bool dx = false;
    if (snap != INVALID_HANDLE_VALUE) {
        MODULEENTRY32W me = { sizeof me };
        for (BOOL ok = Module32FirstW(snap, &me); ok; ok = Module32NextW(snap, &me)) {
            std::wstring n = me.szModule;
            if (!list.empty()) list += L", ";
            list += n;
            std::wstring l = n;
            for (auto& ch : l) ch = (wchar_t)towlower(ch);
            if (l.find(L"dxgi") == 0 || l.find(L"d3d") == 0 || l.find(L"d2d1") == 0 || l.find(L"d3d10warp") == 0) dx = true;
        }
        CloseHandle(snap);
    }
    wprintf(L"modules: %s\n", list.c_str());
    wprintf(L"directx_modules=%s\n", dx ? L"YES" : L"none");
    fflush(stdout);
    PostQuitMessage(0);
}

static int MeasureStartup(HINSTANCE hInst, bool hidden) {
    g_phase[0] = SinceStart();
    if (!gfx::Init()) return 1;
    g_phase[1] = SinceStart();
    {
        MainWindow w(nullptr);
        w.SetMain();
        if (!w.Create(hInst, hidden)) return 1;
        g_phase[2] = SinceStart();
        app::g_firstPaintHook = PrintStartup;
        if (hidden) {
            // Nothing is shown: the first paint is requested into a memory device context.
            HDC screen = GetDC(nullptr);
            HDC mem = CreateCompatibleDC(screen);
            RECT cr;
            GetClientRect(w.hwnd(), &cr);
            HBITMAP bmp = CreateCompatibleBitmap(screen, cr.right, cr.bottom);
            HGDIOBJ old = SelectObject(mem, bmp);
            SendMessageW(w.hwnd(), WM_PRINTCLIENT, (WPARAM)mem, PRF_CLIENT);
            g_phase[3] = SinceStart();
            SelectObject(mem, old);
            DeleteObject(bmp);
            DeleteDC(mem);
            ReleaseDC(nullptr, screen);
            g_firstPaintDone = true;
            PrintStartup();
            return 0;
        }
        ShowWindow(w.hwnd(), SW_SHOWNOACTIVATE);
        MSG m;
        while (GetMessageW(&m, nullptr, 0, 0) > 0) {
            TranslateMessage(&m);
            DispatchMessageW(&m);
        }
    }
    gfx::Shutdown();
    return 0;
}

bool Handle(HINSTANCE hInst, PWSTR cmdLine, int, int* rc) {
    std::wstring cmd = cmdLine ? cmdLine : L"";
    if (cmd.find(L"--render") != std::wstring::npos && cmd.find(L"--render2") == std::wstring::npos && cmd.find(L"--renderdpi") == std::wstring::npos) {
        size_t p = cmd.find(L"--render") + 8;
        while (p < cmd.size() && cmd[p] == L' ') p++;
        std::wstring dir = cmd.substr(p);
        if (!dir.empty() && dir[0] == L'"') {
            size_t e = dir.find(L'"', 1);
            dir = dir.substr(1, e == std::wstring::npos ? std::wstring::npos : e - 1);
        }
        *rc = RenderAll(hInst, dir);
        return true;
    }
    if (cmd.find(L"--renderdpi") != std::wstring::npos) {
        size_t p = cmd.find(L"--renderdpi") + 11;
        while (p < cmd.size() && cmd[p] == L' ') p++;
        *rc = RenderDpi(cmd.substr(p));
        return true;
    }
    if (cmd.find(L"--render2") != std::wstring::npos) {
        size_t p = cmd.find(L"--render2") + 9;
        while (p < cmd.size() && cmd[p] == L' ') p++;
        *rc = Render2(cmd.substr(p));
        return true;
    }
    if (cmd.find(L"--geometrydpi") != std::wstring::npos) {
        size_t p = cmd.find(L"--geometrydpi") + 13;
        while (p < cmd.size() && cmd[p] == L' ') p++;
        *rc = GeometryDpi(cmd.substr(p));
        return true;
    }
    if (cmd.find(L"--geometry") != std::wstring::npos) {
        size_t p = cmd.find(L"--geometry") + 10;
        while (p < cmd.size() && cmd[p] == L' ') p++;
        *rc = Geometry(cmd.substr(p));
        return true;
    }
    if (cmd.find(L"--measure-startup") != std::wstring::npos && cmd.find(L"--popups") != std::wstring::npos) {
        // Part of the on-screen measurement mode: the window and its popups are shown without being activated.
        *rc = PopupCheck(hInst);
        return true;
    }
    if (cmd.find(L"--measure-startup") != std::wstring::npos) {
        *rc = MeasureStartup(hInst, cmd.find(L"--hidden") != std::wstring::npos);
        return true;
    }
    if (cmd.find(L"--selftest") != std::wstring::npos) {
        *rc = selftest::Run(hInst);
        return true;
    }
    // The test build never starts the real window by accident (a stale or mistyped option must not show one).
    fwprintf(stderr, L"HourCalc-test: use --render <dir>, --renderdpi <dir>, --selftest, --geometry <file> or --measure-startup\n");
    *rc = 2;
    return true;
}

}  // namespace testhooks
