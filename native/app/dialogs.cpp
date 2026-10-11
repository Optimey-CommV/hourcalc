// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the About window and the license viewer, see dialogs.h.

#include "clipboard.h"
#include "dialogs.h"
#include "embedded.h"
#include "ui.h"

#include <commdlg.h>
#include <math.h>

namespace dialogs {

using gfx::Argb;
using gfx::Canvas;
using gfx::Font;
using gfx::Over;
using gfx::WithAlpha;
using ui::Rn;

static const wchar_t kClassName[] = L"HourCalcDialog";

// Every Dialog that exists (a linked list through nextLive_): the main window re-themes them all when the theme changes while the
// About or the license window is open.
static Dialog* g_live = nullptr;

Dialog::Dialog(Kind kind, const theme::Palette& pal, float dpiScale, HINSTANCE hInst)
    : kind_(kind), pal_(pal), d_(dpiScale), hInst_(hInst), hwnd_(nullptr), owner_(nullptr), done_(false), hot_(-1), pressed_(-1), focus_(-1),
      W_(0), H_(0), naturalH_(0), yName_(0), yVersion_(0), yCopy_(0), yNotice_(0), noticeH_(0), textX_(0), textW_(0), view_{}, scroll_(0),
      selA_(0), selB_(0), dragSel_(false), dragThumb_(false), dragOffset_(0), hasBar_(false), blockW_(0), onViewLicense_(nullptr) {
    if (kind == About) {
        buttons_.push_back({ L"View license", false, {} });
#ifndef HC_NO_EMBEDDED_SOURCE
        buttons_.push_back({ L"Save source code...", false, {} });
#endif
        buttons_.push_back({ L"Close", true, {} });
    } else {
        buttons_.push_back({ L"Close", true, {} });
        if (!embedded::ReadLicense(licenseText_))
            licenseText_ = L"This build does not carry the license text. See https://www.gnu.org/licenses/gpl-3.0.html";
        hardLines_ = 1;
        for (wchar_t c : licenseText_)
            if (c == L'\n') hardLines_++;
    }
    nextLive_ = g_live;
    g_live = this;
}

Dialog::~Dialog() {
    for (Dialog** link = &g_live; *link; link = &(*link)->nextLive_)
        if (*link == this) {
            *link = nextLive_;
            break;
        }
    if (hwnd_) {
        SetWindowLongPtrW(hwnd_, GWLP_USERDATA, 0);
        DestroyWindow(hwnd_);
    }
    if (iconBig_) DestroyIcon(iconBig_);
    if (iconSmall_) DestroyIcon(iconSmall_);
}

void Dialog::SetPalette(const theme::Palette& pal) {
    pal_ = pal;
    if (hwnd_) theme::ApplyCaption(hwnd_, pal_);
    Invalidate();
}

void Dialog::SetPaletteOfAll(const theme::Palette& pal) {
    for (Dialog* d = g_live; d; d = d->nextLive_) d->SetPalette(pal);
}

std::vector<std::wstring> Dialog::ButtonTexts() const {
    std::vector<std::wstring> v;
    for (auto& b : buttons_) v.push_back(b.text);
    return v;
}

#ifndef HC_NO_EMBEDDED_SOURCE
bool Dialog::SourceAvailable() const {
    const uint8_t* d;
    size_t n;
    return embedded::OpenSource(&d, &n);
}
#endif

// ------------------------------------------------------------------ layout

static float ButtonHeight(float d) {
    Font f = ui::TextFont(14.0f * d);
    return Rn(gfx::LineHeight(f)) + 2.0f * Rn(6.0f * d) + 2.0f * Rn(d);
}

static float ButtonWidth(const std::wstring& text, float d) {
    Font f = ui::TextFont(14.0f * d);
    float w = Rn(gfx::TextWidth(text.c_str(), (int)text.size(), f)) + 2.0f * Rn(16.0f * d) + 2.0f * Rn(d);
    return fmaxf(Rn(88.0f * d), w);
}

void Dialog::LayoutAbout(int w) {
    const float d = d_;
    W_ = w;
    textX_ = Rn(24.0f * d);
    textW_ = (float)w - 2.0f * Rn(24.0f * d);
    Font fName = ui::DisplayFont(28.0f * d, 600), f14 = ui::TextFont(14.0f * d), f13 = ui::TextFont(13.0f * d);
    float y = Rn(20.0f * d);
    yName_ = y;
    y += Rn(gfx::LineHeight(fName));
    y += Rn(2.0f * d);
    yVersion_ = y;
    y += Rn(gfx::LineHeight(f14));
    y += Rn(14.0f * d);
    yCopy_ = y;
    y += Rn(gfx::LineHeight(f14));
    y += Rn(12.0f * d);
    yNotice_ = y;
    const wchar_t* notice = embedded::kNotice;
    noticeH_ = Rn(gfx::TextHeightWrapped(notice, (int)wcslen(notice), f13, textW_));
    y += noticeH_;
#ifdef HC_NO_EMBEDDED_SOURCE
    y += Rn(10.0f * d);
    ySource_ = y;
    y += Rn(gfx::LineHeight(f13));
#endif
    y += Rn(22.0f * d);
    float bh = ButtonHeight(d);
    float total = 0;
    float bw[3];
    for (size_t i = 0; i < buttons_.size(); i++) {
        bw[i] = ButtonWidth(buttons_[i].text, d);
        total += bw[i];
    }
    total += 2.0f * Rn(8.0f * d);
    float x = textX_ + textW_ - total;
    for (size_t i = 0; i < buttons_.size(); i++) {
        buttons_[i].r = { x, y, bw[i], bh };
        x += bw[i] + Rn(8.0f * d);
    }
    y += bh + Rn(20.0f * d);
    naturalH_ = (int)y;
    H_ = naturalH_;
}

// The vertical scroll bar of the WPF ScrollViewer is as wide as the system's (17 px at 100 %); only its thumb is drawn.
float Dialog::BarWidth() const {
    return (float)GetSystemMetricsForDpi(SM_CXVSCROLL, (UINT)(96.0f * d_ + 0.5f));
}

float Dialog::ScrollMax() const {
    float m = block_.Height() - view_.h;
    return m > 0 ? m : 0;
}

void Dialog::ScrollTo(float v) {
    float m = ScrollMax();
    if (v < 0) v = 0;
    if (v > m) v = m;
    scroll_ = v;
    Invalidate();
}

static Font LicenseFont(float d) {
    Font f = ui::TextFont(12.5f * d);
    f.fam = gfx::FamMono;
    return f;
}

// Lays the text out for the width with (withBar) or without room for the scroll bar, and remembers its height.
void Dialog::SetBlock(bool withBar) {
    float inset = Rn(2.0f * d_);
    blockW_ = view_.w - (withBar ? BarWidth() : 0.0f) - 2.0f * inset;
    block_.Set(licenseText_.c_str(), (int)licenseText_.size(), LicenseFont(d_), blockW_);
    blockBar_ = withBar ? 1 : 0;
    (withBar ? layNarrowH_ : layFullH_) = block_.Height();
    licensePasses_++;
}

void Dialog::LayoutLicense(int w, int h) {
    const float d = d_;
    W_ = w;
    H_ = h;
    float bh = ButtonHeight(d), bw = ButtonWidth(buttons_[0].text, d);
    buttons_[0].r = { (float)w - Rn(16.0f * d) - bw, (float)h - Rn(16.0f * d) - bh, bw, bh };
    view_ = { Rn(16.0f * d), Rn(12.0f * d), (float)w - Rn(16.0f * d) - Rn(8.0f * d), (float)h - Rn(12.0f * d) - Rn(16.0f * d) - bh - Rn(12.0f * d) };
    if (view_.h < 1) view_.h = 1;
    // Laying out 35,000 characters takes tens of milliseconds, so it is done only when it has to be. The text depends on
    // the width of the view and the scale; a change of the height alone (most of a resize) only moves the end of the view.
    if (layViewW_ != view_.w || layD_ != d) {
        layViewW_ = view_.w;
        layD_ = d;
        layFullH_ = layNarrowH_ = -1.0f;
        blockBar_ = -1;
    }
    // The scroll bar takes room from the text, and it appears only when the text is longer than the view: the bar is
    // there when the text laid out at the full width is taller than the view. The text at the narrower width is never
    // shorter, so it is laid out first: when even that fits, there is no bar. When it does not fit, the bar is certain
    // if the lines that end in a line break alone are taller than the view (no wrapping makes the text shorter than
    // that); only for a view taller than that a second layout at the full width decides.
    bool bar;
    if (layFullH_ >= 0.0f) {
        bar = layFullH_ > view_.h;
    } else {
        if (layNarrowH_ < 0.0f) SetBlock(true);
        float least = 0.9f * (float)hardLines_ * gfx::LineHeight(LicenseFont(d));
        if (layNarrowH_ <= view_.h) bar = false;
        else if (least > view_.h) bar = true;
        else {
            SetBlock(false);
            bar = layFullH_ > view_.h;
        }
    }
    hasBar_ = bar;
    if (blockBar_ != (bar ? 1 : 0)) SetBlock(bar);
    ScrollTo(scroll_);
}

void Dialog::Layout(int clientW, int clientH) {
    if (kind_ == About) LayoutAbout(clientW);
    else LayoutLicense(clientW, clientH);
}

// ------------------------------------------------------------------ painting

void Dialog::PaintButtons(Canvas& c) {
    const float d = d_;
    float bt = Rn(d);
    Font f = ui::TextFont(14.0f * d);
    for (size_t i = 0; i < buttons_.size(); i++) {
        const Button& b = buttons_[i];
        bool hot = (int)i == hot_;
        bool down = (int)i == pressed_ && hot;
        float op = down ? 0.7f : (hot ? 0.85f : 1.0f);
        Argb fill = b.isDefault ? pal_.accent : pal_.func;
        Argb fg = b.isDefault ? pal_.accentText : pal_.text;
        Argb top = b.isDefault ? pal_.accent : pal_.edgeTop;
        Argb bottom = b.isDefault ? pal_.accentEdgeBottom : pal_.edgeBottom;
        bool focused = focus_ == (int)i;
        fill = Over(fill, pal_.bg, op);
        fg = Over(fg, pal_.bg, op);
        // A Border with CornerRadius 4 (gfx::Canvas::BorderBox).
        if (focused) c.BorderBox(b.r, 4.0f * d, bt, fill, Over(pal_.text, pal_.bg, op), 0, true);
        else c.BorderBox(b.r, 4.0f * d, bt, fill, Over(top, pal_.bg, op), Over(bottom, pal_.bg, op));
        float tw = Rn(gfx::TextWidth(b.text.c_str(), (int)b.text.size(), f));
        float tx = b.r.x + bt + Rn(16.0f * d) + Rn((b.r.w - 2 * bt - 2.0f * Rn(16.0f * d) - tw) / 2.0f);
        float ty = b.r.y + bt + Rn(6.0f * d);
        c.Text(b.text.c_str(), (int)b.text.size(), f, tx, ty, fg);
    }
}

void Dialog::PaintAbout(Canvas& c) {
    const float d = d_;
    Font fName = ui::DisplayFont(28.0f * d, 600), f14 = ui::TextFont(14.0f * d), f13 = ui::TextFont(13.0f * d);
    c.Text(embedded::kProductName, (int)wcslen(embedded::kProductName), fName, textX_, yName_, pal_.text);
    std::wstring ver = std::wstring(L"Version ") + embedded::kVersion;
    c.Text(ver.c_str(), (int)ver.size(), f14, textX_, yVersion_, pal_.textSecondary);
    c.Text(embedded::kCopyright, (int)wcslen(embedded::kCopyright), f14, textX_, yCopy_, pal_.text);
    c.Text(embedded::kNotice, (int)wcslen(embedded::kNotice), f13, textX_, yNotice_, pal_.textSecondary, textW_);
#ifdef HC_NO_EMBEDDED_SOURCE
    std::wstring source = std::wstring(L"Source code: ") + embedded::kSourceUrl;
    c.Text(source.c_str(), (int)source.size(), f13, textX_, ySource_, pal_.textSecondary);
#endif
    PaintButtons(c);
}

RectF Dialog::ThumbRect() const {
    const float d = d_;
    float track = view_.h;
    float ext = block_.Height();
    float len = fmaxf(Rn(10.0f * d), track * view_.h / ext);
    if (len > track) len = track;
    float m = ScrollMax();
    float y = view_.y + (m > 0 ? scroll_ / m * (track - len) : 0.0f);
    float bw = BarWidth();
    return { view_.right() - bw + Rn(3.0f * d), Rn(y), bw - 2.0f * Rn(3.0f * d), Rn(len) };
}

void Dialog::PaintLicense(Canvas& c) {
    const float d = d_;
    if (viewCanvas_.Resize((int)view_.w, (int)view_.h)) {
        gfx::SetTextSurface(pal_.bg);
        viewCanvas_.Clear(pal_.bg);
        float inset = Rn(2.0f * d);
        if (selA_ != selB_) {
            std::vector<RectF> rects;
            block_.Selection(selA_ < selB_ ? selA_ : selB_, selA_ < selB_ ? selB_ : selA_, inset, -scroll_, rects);
            for (auto& r : rects) {
                if (r.y + r.h < 0 || r.y > view_.h) continue;
                viewCanvas_.FillRect((int)Rn(r.x), (int)Rn(r.y), (int)Rn(r.w), (int)Rn(r.h), WithAlpha(pal_.accent, 0.4f));
            }
        }
        block_.Draw(viewCanvas_, inset, -scroll_, pal_.text);
        c.Copy(viewCanvas_, (int)view_.x, (int)view_.y);
    }
    if (hasBar_) {
        RectF t = ThumbRect();
        c.FillRoundRect(t, 3.0f * d, pal_.textDisabled);
    }
    PaintButtons(c);
}

void Dialog::Render(Canvas& c) {
    if (!c.Resize(W_, H_)) return;
    gfx::SetTextSurface(pal_.bg);
    c.Clear(pal_.bg);
    if (kind_ == About) PaintAbout(c);
    else PaintLicense(c);
}

void Dialog::Invalidate() {
    if (hwnd_) InvalidateRect(hwnd_, nullptr, FALSE);
}

// ------------------------------------------------------------------ actions

#ifndef HC_NO_EMBEDDED_SOURCE
void Dialog::SaveSource() {
    if (!SourceAvailable()) {
        MessageBoxW(hwnd_, L"This build does not carry its source code.", L"About HourCalc", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t file[MAX_PATH];
    std::wstring name = embedded::SourceFileName();
    wcsncpy_s(file, name.c_str(), _TRUNCATE);
    OPENFILENAMEW ofn = { sizeof ofn };
    ofn.hwndOwner = hwnd_;
    ofn.lpstrFilter = L"Zip archive (*.zip)\0*.zip\0\0";
    ofn.lpstrFile = file;
    ofn.nMaxFile = MAX_PATH;
    ofn.lpstrTitle = L"Save HourCalc source code";
    ofn.lpstrDefExt = L"zip";
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST | OFN_NOCHANGEDIR | OFN_EXPLORER;
    if (!GetSaveFileNameW(&ofn)) return;
    if (!embedded::SaveSource(file)) {
        wchar_t msg[64 + MAX_PATH];
        swprintf_s(msg, L"The source code could not be written to %s.", file);
        MessageBoxW(hwnd_, msg, L"About HourCalc", MB_OK | MB_ICONERROR);
    }
}
#endif

void Dialog::ViewLicense() {
    if (onViewLicense_) {
        onViewLicense_(*this);
        return;
    }
    Dialog lic(License, pal_, d_, hInst_);
    lic.RunModal(hwnd_);
}

void Dialog::Activate(int b) {
    if (b < 0 || b >= (int)buttons_.size()) return;
    if (kind_ == About) {
        if (b == 0) ViewLicense();
#ifndef HC_NO_EMBEDDED_SOURCE
        else if (b == 1) SaveSource();
#endif
        else done_ = true;
    } else {
        done_ = true;
    }
    if (done_ && hwnd_) PostMessageW(hwnd_, WM_NULL, 0, 0);
}

void Dialog::Click(int b) {
    Activate(b);
}

int Dialog::ButtonAt(int x, int y) const {
    for (size_t i = 0; i < buttons_.size(); i++)
        if (buttons_[i].r.Contains((float)x, (float)y)) return (int)i;
    return -1;
}

// ------------------------------------------------------------------ the window

LRESULT CALLBACK Dialog::StaticProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Dialog* self = reinterpret_cast<Dialog*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Dialog*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = h;
    }
    if (self) {
        // An exception must never unwind through the window procedure (the core throws on arithmetic overflow).
        try {
            return self->Proc(msg, wp, lp);
        } catch (...) {
            OutputDebugStringW(L"HourCalc: an exception reached the window procedure of a dialog\n");
            return DefWindowProcW(h, msg, wp, lp);
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

LRESULT Dialog::Proc(UINT msg, WPARAM wp, LPARAM lp) {
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (msg) {
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd_, &ps);
        Render(canvas_);
        canvas_.BlitTo(dc, 0, 0, canvas_.W(), canvas_.H());
        EndPaint(hwnd_, &ps);
        return 0;
    }
    case WM_SIZE:
        if (kind_ == License && wp != SIZE_MINIMIZED) {
            Layout(LOWORD(lp), HIWORD(lp));
            Invalidate();
        }
        return 0;
    case WM_GETMINMAXINFO:
        if (kind_ == License) {
            MINMAXINFO* mm = reinterpret_cast<MINMAXINFO*>(lp);
            mm->ptMinTrackSize.x = (int)Rn(420.0f * d_);
            mm->ptMinTrackSize.y = (int)Rn(320.0f * d_);
            return 0;
        }
        break;
    case WM_SETCURSOR:
        if (LOWORD(lp) == HTCLIENT && kind_ == License) {
            POINT pt;
            GetCursorPos(&pt);
            ScreenToClient(hwnd_, &pt);
            bool text = view_.Contains((float)pt.x, (float)pt.y) && !(hasBar_ && (float)pt.x >= view_.right() - BarWidth());
            SetCursor(LoadCursorW(nullptr, text ? IDC_IBEAM : IDC_ARROW));
            return TRUE;
        }
        break;
    case WM_MOUSEMOVE: {
        if (dragSel_) {
            float inset = Rn(2.0f * d_);
            int idx = block_.HitIndex((float)x - view_.x - inset, (float)y - view_.y + scroll_);
            if (idx != selB_) {
                selB_ = idx;
                Invalidate();
            }
        } else if (dragThumb_) {
            RectF t = ThumbRect();
            float track = view_.h - t.h;
            if (track > 0) ScrollTo((((float)y - dragOffset_) - view_.y) / track * ScrollMax());
        } else {
            int hot = ButtonAt(x, y);
            if (hot != hot_) {
                hot_ = hot;
                Invalidate();
            }
            TRACKMOUSEEVENT t = { sizeof t, TME_LEAVE, hwnd_, 0 };
            TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        hot_ = -1;
        Invalidate();
        return 0;
    case WM_LBUTTONDOWN: {
        int b = ButtonAt(x, y);
        if (b >= 0) {
            pressed_ = b;
            focus_ = b;
            SetCapture(hwnd_);
            Invalidate();
        } else if (kind_ == License && view_.Contains((float)x, (float)y)) {
            float bw = BarWidth();
            if (hasBar_ && (float)x >= view_.right() - bw) {
                RectF t = ThumbRect();
                if (t.Contains((float)x, (float)y)) {
                    dragThumb_ = true;
                    dragOffset_ = (float)y - t.y;
                } else {
                    ScrollTo(scroll_ + ((float)y < t.y ? -view_.h : view_.h));
                }
            } else {
                float inset = Rn(2.0f * d_);
                selA_ = selB_ = block_.HitIndex((float)x - view_.x - inset, (float)y - view_.y + scroll_);
                dragSel_ = true;
            }
            SetCapture(hwnd_);
            Invalidate();
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        int p = pressed_;
        pressed_ = -1;
        dragSel_ = dragThumb_ = false;
        if (GetCapture() == hwnd_) ReleaseCapture();
        Invalidate();
        if (p >= 0 && ButtonAt(x, y) == p) Activate(p);
        return 0;
    }
    case WM_MOUSEWHEEL:
        if (kind_ == License) {
            UINT lines = 3;
            SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
            WheelScroll(GET_WHEEL_DELTA_WPARAM(wp), lines);
        }
        return 0;
    case WM_KEYDOWN: {
        bool ctrl = GetKeyState(VK_CONTROL) < 0, shift = GetKeyState(VK_SHIFT) < 0;
        int n = (int)buttons_.size();
        if (wp == VK_ESCAPE) {
            done_ = true;
            PostMessageW(hwnd_, WM_NULL, 0, 0);
            return 0;
        }
        if (wp == VK_TAB) {
            focus_ = focus_ < 0 ? (shift ? n - 1 : 0) : (focus_ + (shift ? n - 1 : 1)) % n;
            Invalidate();
            return 0;
        }
        if (wp == VK_RETURN || wp == VK_SPACE) {
            int target = focus_ >= 0 ? focus_ : -1;
            if (target < 0 && wp == VK_RETURN)
                for (int i = 0; i < n; i++)
                    if (buttons_[(size_t)i].isDefault) target = i;
            if (target >= 0) Activate(target);
            return 0;
        }
        if (kind_ == License) {
            float line = Rn(gfx::LineHeight(LicenseFont(d_)));
            if (ctrl && wp == 'A') { selA_ = 0; selB_ = (int)licenseText_.size(); Invalidate(); return 0; }
            if (ctrl && (wp == 'C' || wp == VK_INSERT)) {
                if (selA_ != selB_) {
                    int a = selA_ < selB_ ? selA_ : selB_, b = selA_ < selB_ ? selB_ : selA_;
                    clip::SetText(hwnd_, licenseText_.substr((size_t)a, (size_t)(b - a)));
                }
                return 0;
            }
            switch (wp) {
            case VK_UP: ScrollTo(scroll_ - line); return 0;
            case VK_DOWN: ScrollTo(scroll_ + line); return 0;
            case VK_PRIOR: ScrollTo(scroll_ - view_.h + line); return 0;
            case VK_NEXT: ScrollTo(scroll_ + view_.h - line); return 0;
            case VK_HOME: if (ctrl) ScrollTo(0); return 0;
            case VK_END: if (ctrl) ScrollTo(ScrollMax()); return 0;
            }
        }
        return 0;
    }
    case WM_DPICHANGED: {
        UINT dpi = HIWORD(wp);
        d_ = dpi / 96.0f;
        const RECT* r = reinterpret_cast<const RECT*>(lp);
        if (kind_ == About) {
            // A window of a fixed size: its size follows from the content at the new scale, not from the size the system
            // suggests (that one is the old size scaled and rounded, and can leave a blank strip or cut the buttons).
            Layout((int)Rn(440.0f * d_), 0);
            RECT rc = { 0, 0, W_, H_ };
            AdjustWindowRectExForDpi(&rc, (DWORD)GetWindowLongPtrW(hwnd_, GWL_STYLE), FALSE, (DWORD)GetWindowLongPtrW(hwnd_, GWL_EXSTYLE), dpi);
            SetWindowPos(hwnd_, nullptr, r->left, r->top, rc.right - rc.left, rc.bottom - rc.top, SWP_NOZORDER | SWP_NOACTIVATE);
        } else {
            SetWindowPos(hwnd_, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top, SWP_NOZORDER | SWP_NOACTIVATE);
            RECT rc;
            GetClientRect(hwnd_, &rc);
            Layout(rc.right, rc.bottom);
        }
        Invalidate();
        return 0;
    }
    case WM_CLOSE:
        done_ = true;
        PostMessageW(hwnd_, WM_NULL, 0, 0);
        return 0;
    case WM_DESTROY:
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

void Dialog::WheelScroll(int delta, UINT lines) {
    if (kind_ != License || lines == 0) return;
    float line = Rn(gfx::LineHeight(LicenseFont(d_)));
    // "One screen at a time" (WHEEL_PAGESCROLL) is a page less a line; a number is that many lines per notch.
    float step = lines == WHEEL_PAGESCROLL ? fmaxf(line, view_.h - line) : line * (float)lines;
    ScrollTo(scroll_ - (float)delta / 120.0f * step);
}

bool Dialog::CreateWindowFor(HWND owner) {
    owner_ = owner;
    WNDCLASSEXW wc = { sizeof wc };
    if (!GetClassInfoExW(hInst_, kClassName, &wc)) {
        wc.lpfnWndProc = StaticProc;
        wc.hInstance = hInst_;
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        RegisterClassExW(&wc);
    }
    UINT dpi = owner ? GetDpiForWindow(owner) : GetDpiForSystem();
    d_ = dpi / 96.0f;
    DWORD style, ex = 0;
    int cw, ch;
    RECT rc;
    if (kind_ == About) {
        style = WS_POPUP | WS_CAPTION | WS_SYSMENU;
        Layout((int)Rn(440.0f * d_), 0);
        rc = { 0, 0, W_, H_ };
        AdjustWindowRectExForDpi(&rc, style, FALSE, ex, dpi);
        cw = rc.right - rc.left;
        ch = rc.bottom - rc.top;
    } else {
        style = WS_OVERLAPPEDWINDOW;
        cw = (int)Rn(660.0f * d_);
        ch = (int)Rn(580.0f * d_);
    }
    int x = CW_USEDEFAULT, y = CW_USEDEFAULT;
    RECT orc;
    if (owner && GetWindowRect(owner, &orc)) {
        x = orc.left + ((orc.right - orc.left) - cw) / 2;
        y = orc.top + ((orc.bottom - orc.top) - ch) / 2;
    }
    const wchar_t* title = kind_ == About ? L"About HourCalc" : L"GNU General Public License";
    hwnd_ = CreateWindowExW(ex, kClassName, title, style, x, y, cw, ch, owner, nullptr, hInst_, this);
    if (!hwnd_) return false;
    theme::ApplyCaption(hwnd_, pal_);
    iconBig_ = (HICON)LoadImageW(hInst_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetricsForDpi(SM_CXICON, dpi), GetSystemMetricsForDpi(SM_CYICON, dpi), LR_DEFAULTCOLOR);
    iconSmall_ = (HICON)LoadImageW(hInst_, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetricsForDpi(SM_CXSMICON, dpi), GetSystemMetricsForDpi(SM_CYSMICON, dpi), LR_DEFAULTCOLOR);
    if (iconBig_) SendMessageW(hwnd_, WM_SETICON, ICON_BIG, (LPARAM)iconBig_);
    if (iconSmall_) SendMessageW(hwnd_, WM_SETICON, ICON_SMALL, (LPARAM)iconSmall_);
    RECT crc;
    GetClientRect(hwnd_, &crc);
    Layout(crc.right, crc.bottom);
    return true;
}

void Dialog::RunModal(HWND owner) {
    if (!CreateWindowFor(owner)) return;
    if (owner) EnableWindow(owner, FALSE);
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    MSG m;
    while (!done_) {
        BOOL r = GetMessageW(&m, nullptr, 0, 0);
        if (r <= 0) {
            if (r == 0) PostQuitMessage((int)m.wParam);
            break;
        }
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    if (owner) EnableWindow(owner, TRUE);
    DestroyWindow(hwnd_);
    hwnd_ = nullptr;
    if (owner) SetForegroundWindow(owner);
}

}  // namespace dialogs
