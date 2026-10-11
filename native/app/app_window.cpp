// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the window: creation, the window procedure, mouse handling, popups, DPI, theme follow.

#include "app.h"
#include "clipboard.h"
#include "dialogs.h"
#include "ui.h"
#include "version.h"

#include <imm.h>
#include <stdlib.h>
#include <string.h>

namespace app {

using ui::Rn;

static const wchar_t kClassName[] = L"HourCalcWindow";
static const UINT_PTR kTimerCaret = 1, kTimerTipShow = 2, kTimerTipHide = 3;
static const DWORD kMenuReopenGuardMs = 250;     // the WPF build's guard: a click that closed the menu must not open it again

// Called after the first WM_PAINT of a window (the start-up measurement of the test build).
void (*g_firstPaintHook)() = nullptr;

const wchar_t* MainWindow::ClassName() { return kClassName; }

MainWindow::MainWindow(const wchar_t* settingsPath) {
    if (settingsPath) settingsPath_ = settingsPath;
    if (!settingsPath_.empty()) {
        hc::settings::Values v = hc::settings::Load(settingsPath_);
        decimals_ = v.decimalPlaces;
        themeMode_ = v.theme;
    }
    engine_.SetDecimalPlaces(decimals_);
    bool dark = theme::IsDarkFor(themeMode_);
    dark_ = dark;
    accent_ = theme::SystemAccent(dark);
    pal_ = theme::Create(dark_, accent_);
    BuildKeys();
    // The idle calculator display, written out instead of asked from the engine (the text the engine gives for no input),
    // so that the start-up path never runs any of its arithmetic.
    calcL_ = L"00:00";
    calcR_ = L"0" + hc::timemath::DecimalSeparator() + std::wstring((size_t)decimals_, L'0');
    SetActive(hc::converter::Left);
    focus_ = 0;
}

MainWindow::~MainWindow() {
    delete menu_;
    delete copy_;
    delete fieldMenu_;
    delete tip_;
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
    if (iconBig_) DestroyIcon(iconBig_);
    if (iconSmall_) DestroyIcon(iconSmall_);
}

bool MainWindow::Create(HINSTANCE hInst, bool hidden) {
    hInst_ = hInst;
    WNDCLASSEXW wc = { sizeof wc };
    if (!GetClassInfoExW(hInst, kClassName, &wc)) {
        wc.style = CS_DBLCLKS;
        wc.lpfnWndProc = StaticProc;
        wc.hInstance = hInst;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), LR_DEFAULTCOLOR);
        wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), LR_DEFAULTCOLOR);
        wc.lpszClassName = kClassName;
        if (!RegisterClassExW(&wc)) return false;
    }
    UINT dpi = GetDpiForSystem();
    float d = dpi / 96.0f;
    int w = (int)Rn(340.0f * d), h = (int)Rn(560.0f * d);
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    if (!hidden) {
        MONITORINFO mi = { sizeof mi };
        HMONITOR mon = MonitorFromPoint({ 0, 0 }, MONITOR_DEFAULTTOPRIMARY);
        if (GetMonitorInfoW(mon, &mi)) {
            x = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - w) / 2;
            y = mi.rcWork.top + ((mi.rcWork.bottom - mi.rcWork.top) - h) / 2;
        }
    }
    hwnd_ = CreateWindowExW(0, kClassName, L"HourCalc", WS_OVERLAPPEDWINDOW, x, y, w, h, nullptr, nullptr, hInst, this);
    if (!hwnd_) return false;
    // A window that is not shown yet has not been sent WM_SIZE: lay it out at its client size now.
    RECT cr;
    if (GetClientRect(hwnd_, &cr) && cr.right > 0 && cr.bottom > 0) {
        clientW_ = cr.right;
        clientH_ = cr.bottom;
        Relayout();
    }
    return true;
}

// ------------------------------------------------------------------ the window procedure

LRESULT CALLBACK MainWindow::StaticProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    MainWindow* self = reinterpret_cast<MainWindow*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<MainWindow*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = h;
    }
    if (self) {
        // An exception must never unwind through the window procedure (the core throws on arithmetic overflow).
        try {
            return self->Proc(msg, wp, lp);
        } catch (...) {
            OutputDebugStringW(L"HourCalc: an exception reached the window procedure of the main window\n");
            return DefWindowProcW(h, msg, wp, lp);
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void MainWindow::ApplyDpi(float dpiScale) {
    dpiScale_ = dpiScale;
}

void MainWindow::UpdateIcons() {
    if (!hwnd_ || !hInst_) return;
    UINT dpi = GetDpiForWindow(hwnd_);
    HICON big = (HICON)LoadImageW(hInst_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetricsForDpi(SM_CXICON, dpi), GetSystemMetricsForDpi(SM_CYICON, dpi), LR_DEFAULTCOLOR);
    HICON smallIcon = (HICON)LoadImageW(hInst_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi), LR_DEFAULTCOLOR);
    if (big) { SendMessageW(hwnd_, WM_SETICON, ICON_BIG, (LPARAM)big); if (iconBig_) DestroyIcon(iconBig_); iconBig_ = big; }
    if (smallIcon) { SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, (LPARAM)smallIcon); if (iconSmall_) DestroyIcon(iconSmall_); iconSmall_ = smallIcon; }
}

// The Windows theme or accent changed. Auto follows both; a forced theme keeps its light or dark and follows the accent only.
void MainWindow::OnSettingChange() {
    bool dark = theme::IsDarkFor(themeMode_);
    Argb accent = theme::SystemAccent(dark);
    if (dark != dark_ || accent != accent_) SetPalette(dark, accent);
}

LRESULT MainWindow::Proc(UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        HIMC none = nullptr;
        ImmAssociateContextEx(hwnd_, none, 0);       // no IME composition in the fields
        UINT dpi = GetDpiForWindow(hwnd_);
        dpiScale_ = dpi / 96.0f;
        theme::ApplyCaption(hwnd_, pal_);
        return 0;
    }
    case WM_ERASEBKGND:
        // The whole client is painted from the memory bitmap in WM_PAINT, so nothing is erased (no flicker while
        // resizing). The very first erase fills the window colour: the window is on the screen from the moment it
        // is shown, and whatever the system composes before the first paint must not be black.
        if (firstErase_) {
            firstErase_ = false;
            RECT rc;
            GetClientRect(hwnd_, &rc);
            HDC dc = reinterpret_cast<HDC>(wp);
            SetDCBrushColor(dc, gfx::ToColorRef(pal_.bg));
            FillRect(dc, &rc, static_cast<HBRUSH>(GetStockObject(DC_BRUSH)));
        }
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        Render(canvas_);
        int x = ps.rcPaint.left, y = ps.rcPaint.top, w = ps.rcPaint.right - x, h = ps.rcPaint.bottom - y;
        if (w > 0 && h > 0) canvas_.BlitTo(dc, x, y, w, h, x, y);
        EndPaint(hwnd_, &ps);
        if (g_firstPaintHook) {
            void (*hook)() = g_firstPaintHook;
            g_firstPaintHook = nullptr;
            hook();
        }
        return 0;
    }
    case WM_PRINTCLIENT: {
        Render(canvas_);
        canvas_.BlitTo(reinterpret_cast<HDC>(wp), 0, 0, canvas_.W(), canvas_.H());
        return 0;
    }
    case WM_SIZE:
        // A menu or tool tip that is open stays where it is on the screen; with the window moving under it, it would
        // hang in the air. (The WPF popup follows its target: this closes it, which no one will miss.)
        CloseMenus();
        HideTip();
        UpdateCaretTimer(false);
        if (wp != SIZE_MINIMIZED) {
            clientW_ = LOWORD(lp);
            clientH_ = HIWORD(lp);
            if (clientW_ > 0 && clientH_ > 0) Relayout();
            Invalidate();
        }
        return 0;
    case WM_MOVE:
        CloseMenus();
        HideTip();
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
        mm->ptMinTrackSize.x = (int)Rn(300.0f * dpiScale_);
        mm->ptMinTrackSize.y = (int)Rn(480.0f * dpiScale_);
        return 0;
    }
    case WM_DPICHANGED: {
        float d = HIWORD(wp) / 96.0f;
        dpiScale_ = d;
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
        UpdateIcons();
        if (menu_) { delete menu_; menu_ = nullptr; }
        if (copy_) { delete copy_; copy_ = nullptr; }
        if (fieldMenu_) { delete fieldMenu_; fieldMenu_ = nullptr; }
        if (tip_) { delete tip_; tip_ = nullptr; }
        Relayout();
        Invalidate();
        return 0;
    }
    case WM_SETFOCUS:
        SetWindowActive(true);
        return 0;
    case WM_KILLFOCUS:
        SetWindowActive(false);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) == WA_INACTIVE) {
            CloseMenus();
            HideTip();
        }
        break;
    case WM_ENABLE:
        if (!wp) { CloseMenus(); HideTip(); }
        break;
    case WM_SETTINGCHANGE:
    case WM_THEMECHANGED:
    case 0x0320 /* WM_DWMCOLORIZATIONCOLORCHANGED */:
        OnSettingChange();
        break;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            Hit h = HitTest(pt.x, pt.y);
            SetCursor(LoadCursorW(nullptr, h.kind == HitField ? IDC_IBEAM : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE:
        OnMouseMove((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_MOUSELEAVE:
        tracking_ = false;
        hover_ = Hit();
        HideTip();
        Invalidate();
        return 0;
    case WM_LBUTTONDOWN:
        OnLButtonDown((short)LOWORD(lp), (short)HIWORD(lp), false);
        return 0;
    case WM_LBUTTONDBLCLK:
        OnLButtonDown((short)LOWORD(lp), (short)HIWORD(lp), true);
        return 0;
    case WM_LBUTTONUP:
        OnLButtonUp((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_RBUTTONUP:
        OnRButtonUp((short)LOWORD(lp), (short)HIWORD(lp));
        return 0;
    case WM_CAPTURECHANGED:
        if (pressed_.kind != HitNone || dragField_ >= 0) {
            pressed_ = Hit();
            dragField_ = -1;
            Invalidate();
        }
        return 0;
    case WM_CHAR:
        OnChar((wchar_t)wp);
        return 0;
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        if (OnKeyDown((UINT)wp, ctrl, shift)) return 0;
        break;
    }
    case WM_TIMER:
        if (wp == kTimerCaret) {
            if (windowActive_ && focus_ >= 0) {
                caretOn_ = !caretOn_;
                RECT r = { (LONG)lay_.box[focus_].x, (LONG)lay_.box[focus_].y, (LONG)lay_.box[focus_].right() + 1, (LONG)lay_.box[focus_].bottom() };
                InvalidateRect(hwnd_, &r, FALSE);
            } else {
                UpdateCaretTimer(false);      // nothing to blink any more: stop waking up
            }
        } else if (wp == kTimerTipShow) {
            KillTimer(hwnd_, kTimerTipShow);
            if (hover_.kind == HitMenu || hover_.kind == HitPin) ShowTip(hover_);
        } else if (wp == kTimerTipHide) {
            HideTip();
        }
        return 0;
    case flyout::WM_FLYOUT_COMMAND:
        OnFlyoutCommand((int)wp, (int)lp);
        return 0;
    case flyout::WM_FLYOUT_CLOSED:
        if (wp == (WPARAM)flyout::Flyout::Menu) menuClosedTick_ = (DWORD)lp;
        return 0;
    case WM_CLOSE:
        DestroyWindow(hwnd_);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd_, kTimerCaret);
        if (isMain_) PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

// ------------------------------------------------------------------ hit testing and the mouse

Hit MainWindow::HitTest(int x, int y) const {
    float fx = (float)x + 0.001f, fy = (float)y + 0.001f;
    Hit h;
    if (lay_.menuBtn.Contains(fx, fy)) { h.kind = HitMenu; return h; }
    if (lay_.pin.Contains(fx, fy)) { h.kind = HitPin; return h; }
    if (lay_.tab[0].Contains(fx, fy)) { h.kind = HitTabConv; return h; }
    if (lay_.tab[1].Contains(fx, fy)) { h.kind = HitTabCalc; return h; }
    if (tab_ == 0) {
        for (int f = 0; f < 2; f++)
            if (lay_.box[f].Contains(fx, fy)) { h.kind = HitField; h.index = f; return h; }
    } else {
        if (lay_.calcVal[0].Contains(fx, fy)) { h.kind = HitCalcValue; h.index = 0; return h; }
        if (!error_ && lay_.calcVal[1].Contains(fx, fy)) { h.kind = HitCalcValue; h.index = 1; return h; }
    }
    for (size_t i = 0; i < lay_.keys.size(); i++)
        if (lay_.keys[i].Contains(fx, fy)) { h.kind = HitKey; h.index = (int)i; return h; }
    return h;
}

int MainWindow::FieldCaretFromX(int field, float x) const {
    const std::wstring& t = fields_[field].text;
    float x0 = FieldTextRight(field) - FieldTextWidth(field);
    return gfx::HitIndex(t.c_str(), (int)t.size(), ui::DisplayFont(convFont_), x - x0);
}

void MainWindow::UpdateHover(int x, int y) {
    Hit h = HitTest(x, y);
    // a disabled key neither highlights nor presses
    if (h.kind == HitKey && !KeyEnabled(KeyAt(h.index).key)) h = Hit();
    if (h != hover_) {
        hover_ = h;
        Invalidate();
    }
}

void MainWindow::OnMouseMove(int x, int y) {
    if (!tracking_) {
        TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd_, 0 };
        TrackMouseEvent(&t);
        tracking_ = true;
    }
    Hit before = hover_;
    UpdateHover(x, y);
    if (dragField_ >= 0) {
        Field& f = fields_[dragField_];
        int idx = FieldCaretFromX(dragField_, (float)x);
        if (idx != f.caret) {
            f.caret = idx;
            RestartCaret();
        }
    }
    if (hover_ != before) {
        KillTimer(hwnd_, kTimerTipShow);
        HideTip();
        if (hover_.kind == HitMenu || hover_.kind == HitPin) SetTimer(hwnd_, kTimerTipShow, 500, nullptr);
    }
}

void MainWindow::OnLButtonDown(int x, int y, bool dbl) {
    HideTip();
    KillTimer(hwnd_, kTimerTipShow);
    Hit h = HitTest(x, y);
    if (h.kind == HitField) {
        int f = h.index;
        bool within = windowActive_ && focus_ == f;
        // Windows reports the second click as a double click and the third as a plain click: a click right after a
        // double click, in the same place, is a triple click and keeps everything selected (a TextBox selects the line).
        bool triple = !dbl && tripleArmed_ && (DWORD)(GetTickCount() - tripleTick_) <= GetDoubleClickTime() &&
                      abs(x - triplePt_.x) <= GetSystemMetrics(SM_CXDOUBLECLK) / 2 && abs(y - triplePt_.y) <= GetSystemMetrics(SM_CYDOUBLECLK) / 2;
        tripleArmed_ = false;
        if (!within) {
            MouseDownOnField(f);
        } else if (dbl || triple) {
            fields_[f].anchor = 0;
            fields_[f].caret = (int)fields_[f].text.size();
            if (dbl) {
                tripleArmed_ = true;
                tripleTick_ = GetTickCount();
                triplePt_ = { x, y };
            }
            RestartCaret();
        } else {
            int idx = FieldCaretFromX(f, (float)x);
            Field& fld = fields_[f];
            fld.caret = idx;
            if (GetKeyState(VK_SHIFT) >= 0) fld.anchor = idx;
            dragField_ = f;
            SetCapture(hwnd_);
            RestartCaret();
        }
        return;
    }
    if (h.kind == HitKey && !KeyEnabled(KeyAt(h.index).key)) return;
    if (h.kind != HitNone && h.kind != HitCalcValue) {
        pressed_ = h;
        SetCapture(hwnd_);
        Invalidate();
    }
}

void MainWindow::OnLButtonUp(int x, int y) {
    bool hadCapture = GetCapture() == hwnd_;
    Hit pressed = pressed_;
    pressed_ = Hit();
    dragField_ = -1;
    if (hadCapture) ReleaseCapture();
    Invalidate();
    if (pressed.kind != HitNone && HitTest(x, y) == pressed) ClickHit(pressed);
}

void MainWindow::ClickHit(const Hit& h) {
    switch (h.kind) {
    case HitKey:
        Press(KeyAt(h.index).key);
        break;
    case HitTabConv:
        SelectTab(0);
        break;
    case HitTabCalc:
        SelectTab(1);
        break;
    case HitMenu:
        ToggleMenu();
        break;
    case HitPin:
        SetTopmost(!topmost_);
        break;
    default:
        break;
    }
}

void MainWindow::OnRButtonUp(int x, int y) {
    Hit h = HitTest(x, y);
    if (tab_ == 1 && h.kind == HitCalcValue) {
        POINT pt = { x, y };
        ClientToScreen(hwnd_, &pt);
        OpenCopyMenu(h.index, pt.x, pt.y);
    } else if (tab_ == 0 && h.kind == HitField) {
        // Like a TextBox: the right click gives the field the focus (a field that did not have it selects all) and
        // opens Cut / Copy / Paste at the pointer.
        if (!(windowActive_ && focus_ == h.index)) MouseDownOnField(h.index);
        POINT pt = { x, y };
        ClientToScreen(hwnd_, &pt);
        OpenFieldMenu(h.index, pt.x, pt.y);
    }
}

// ------------------------------------------------------------------ popups

flyout::Flyout& MainWindow::MenuFlyout() {
    if (!menu_) {
        menu_ = new flyout::Flyout(flyout::Flyout::Menu);
        BuildMenu();
    }
    return *menu_;
}

flyout::Flyout& MainWindow::CopyFlyout() {
    if (!copy_) {
        copy_ = new flyout::Flyout(flyout::Flyout::Copy);
        std::vector<flyout::Item> items;
        items.push_back({ flyout::Item::Button, L"Copy", kCmdCopy, false, {} });
        copy_->SetItems(items);
        copy_->SetPalette(pal_);
    }
    return *copy_;
}

flyout::Flyout& MainWindow::FieldMenuFlyout() {
    if (!fieldMenu_) {
        fieldMenu_ = new flyout::Flyout(flyout::Flyout::Copy);
        std::vector<flyout::Item> items;
        items.push_back({ flyout::Item::Button, L"Cut", kCmdCut, false, {} });
        items.push_back({ flyout::Item::Button, L"Copy", kCmdFieldCopy, false, {} });
        items.push_back({ flyout::Item::Button, L"Paste", kCmdPaste, false, {} });
        fieldMenu_->SetItems(items);
        fieldMenu_->SetPalette(pal_);
    }
    return *fieldMenu_;
}

flyout::Flyout& MainWindow::TipFlyout() {
    if (!tip_) {
        tip_ = new flyout::Flyout(flyout::Flyout::Tip);
        tip_->SetPalette(pal_);
    }
    return *tip_;
}

// The menu: the Theme section (Auto, Light, Dark), the Advanced section (decimal places), About. The radio items of the two
// sections are two groups.
void MainWindow::BuildMenu() {
    std::vector<flyout::Item> items;
    auto radio = [&](const std::wstring& text, int id, bool checked, int group) {
        flyout::Item it = { flyout::Item::Radio, text, id, checked, {} };
        it.group = group;
        items.push_back(it);
    };
    items.push_back({ flyout::Item::Header, L"Theme", 0, false, {} });
    radio(L"Auto (follow Windows)", kCmdThemeAuto, themeMode_ == hc::settings::ThemeMode::Auto, 1);
    radio(L"Light", kCmdThemeLight, themeMode_ == hc::settings::ThemeMode::Light, 1);
    radio(L"Dark", kCmdThemeDark, themeMode_ == hc::settings::ThemeMode::Dark, 1);
    items.push_back({ flyout::Item::Separator, L"", 0, false, {} });
    items.push_back({ flyout::Item::Header, L"Advanced", 0, false, {} });
    items.push_back({ flyout::Item::Label, L"Decimal places", 0, false, {} });
    for (int n = hc::timemath::MinDisplayDecimals; n <= hc::timemath::MaxDisplayDecimals; n++) {
        std::wstring text = n == hc::timemath::DefaultDecimals ? L"2 (default)" : std::to_wstring(n);
        radio(text, n, n == decimals_, 2);
    }
    items.push_back({ flyout::Item::Separator, L"", 0, false, {} });
    items.push_back({ flyout::Item::Button, L"About HourCalc", kCmdAbout, false, {} });
    menu_->SetItems(items);
    menu_->SetPalette(pal_);
}

POINT MainWindow::MenuPosition(const RECT* work) {
    flyout::Flyout& m = MenuFlyout();
    m.Layout(hwnd_, lay_.d);
    POINT pt = { (LONG)lay_.menuBtn.x, (LONG)lay_.menuBtn.bottom() };
    ClientToScreen(hwnd_, &pt);
    // The popup opens below the button: box 4 px in, 2 px down.
    pt.x += (LONG)Rn(4.0f * lay_.d);
    pt.y += (LONG)Rn(2.0f * lay_.d);
    // The menu is tall: when it does not fit below the button on the work area of the monitor, it moves up (and left) until it
    // does. (Flyout::Open does the same for every popup; knowing the place here lets the tests check it without showing it.)
    RECT area;
    if (work) {
        area = *work;
    } else {
        MONITORINFO mi = { sizeof mi };
        if (!GetMonitorInfoW(MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST), &mi)) return pt;
        area = mi.rcWork;
    }
    return flyout::Flyout::Fit(pt.x, pt.y, m.BoxWidth(), m.BoxHeight(), area);
}

void MainWindow::OpenMenu() {
    flyout::Flyout& m = MenuFlyout();
    m.SetChecked(decimals_);
    m.SetChecked(kCmdThemeAuto + (int)themeMode_);
    m.SetPalette(pal_);
    POINT pt = MenuPosition();          // lays the menu out for the scale
    if (GetCapture() == hwnd_) ReleaseCapture();
    m.Open(hwnd_, pt.x, pt.y);
}

void MainWindow::ShowAbout() {
    dialogs::Dialog dlg(dialogs::Dialog::About, pal_, lay_.d, hInst_);
    dlg.RunModal(hwnd_);
}

void MainWindow::ToggleMenu() {
    if (IsMenuOpen()) {
        CloseMenus();
        return;
    }
    // The click that closed the menu (it closes on any click outside it) also reaches the button: that must not
    // open it again.
    if (MenuReopenBlocked()) return;
    OpenMenu();
}

bool MainWindow::MenuReopenBlocked() const {
    return menuClosedTick_ != 0 && (DWORD)(GetTickCount() - menuClosedTick_) < kMenuReopenGuardMs;
}

void MainWindow::CloseMenus() {
    if (menu_) menu_->Close();
    if (copy_) copy_->Close();
    if (fieldMenu_) fieldMenu_->Close();
}

void MainWindow::OpenCopyMenu(int which, int sx, int sy) {
    copyWhich_ = which;
    flyout::Flyout& c = CopyFlyout();
    c.SetPalette(pal_);
    c.Layout(hwnd_, lay_.d);
    if (GetCapture() == hwnd_) ReleaseCapture();
    c.Open(hwnd_, sx + (int)Rn(2.0f * lay_.d), sy + (int)Rn(2.0f * lay_.d));
}

void MainWindow::PrepareFieldMenu(int field) {
    ctxField_ = field;
    flyout::Flyout& m = FieldMenuFlyout();
    const Field& f = fields_[field];
    m.SetEnabled(kCmdCut, f.selLength() > 0);
    m.SetEnabled(kCmdFieldCopy, f.selLength() > 0);
    m.SetEnabled(kCmdPaste, clip::HasText());
    m.SetPalette(pal_);
    m.Layout(hwnd_, lay_.d);
}

void MainWindow::OpenFieldMenu(int field, int sx, int sy) {
    PrepareFieldMenu(field);
    flyout::Flyout& m = FieldMenuFlyout();
    if (GetCapture() == hwnd_) ReleaseCapture();
    m.Open(hwnd_, sx + (int)Rn(2.0f * lay_.d), sy + (int)Rn(2.0f * lay_.d));
}

void MainWindow::OnFlyoutCommand(int id, int) {
    if (id >= hc::timemath::MinDisplayDecimals && id <= hc::timemath::MaxDisplayDecimals) {
        SetDecimalPlaces(id);
    } else if (id >= kCmdThemeAuto && id <= kCmdThemeDark) {
        SetThemeMode(static_cast<hc::settings::ThemeMode>(id - kCmdThemeAuto));
    } else if (id == kCmdAbout) {
        ShowAbout();
    } else if (id == kCmdCopy) {
        SetClipboard(copyWhich_ == 0 ? engine_.CopyMainText() : engine_.CopyDecimalText());
    } else if (id == kCmdCut) {
        CutField(ctxField_);
    } else if (id == kCmdFieldCopy) {
        CopyField(ctxField_);
    } else if (id == kCmdPaste) {
        PasteField(ctxField_);
    }
}

void MainWindow::ShowTip(const Hit& h) {
    const wchar_t* text = h.kind == HitMenu ? L"Menu" : L"Keep on top";
    flyout::Flyout& t = TipFlyout();
    t.SetPalette(pal_);
    t.SetText(text);
    t.Layout(hwnd_, lay_.d);
    POINT pt;
    GetCursorPos(&pt);
    t.Open(hwnd_, pt.x, pt.y + (int)Rn(21.0f * lay_.d));
    tipShown_ = true;
    SetTimer(hwnd_, kTimerTipHide, 5000, nullptr);
}

void MainWindow::HideTip() {
    if (tipShown_ && tip_) tip_->Close();
    tipShown_ = false;
    if (hwnd_) KillTimer(hwnd_, kTimerTipHide);
}

}  // namespace app
