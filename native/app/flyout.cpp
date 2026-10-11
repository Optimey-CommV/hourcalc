// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the popups, see flyout.h.

#include "flyout.h"
#include "ui.h"

#include <math.h>
#include <string.h>

namespace flyout {

using gfx::Argb;
using gfx::Canvas;
using gfx::Font;
using gfx::Over;
using ui::Rn;

// Soft shadow of the WPF build: DropShadowEffect BlurRadius 16, ShadowDepth 3 (down), opacity 0.28.
static const float kShadowSigma = 5.2f;       // DIP
static const float kShadowDepth = 3.0f;       // DIP
static const float kShadowOpacity = 0.28f;
static const float kShadowRoomDip = 14.0f;
static const wchar_t kClassName[] = L"HourCalcFlyout";
static ATOM g_class = 0;

Flyout::Flyout(Kind kind)
    : kind_(kind), hwnd_(nullptr), owner_(nullptr), open_(false), dirty_(true), scale_(1.0f), boxW_(0), boxH_(0), room_(0),
      winW_(0), winH_(0), hot_(-1), pressed_(-1), tracking_(false), posX_(0), posY_(0), shapeDirty_(true),
      dib_(nullptr), dibBits_(nullptr), dibDc_(nullptr) {
    memset(&pal_, 0, sizeof pal_);
}

Flyout::~Flyout() {
    if (hwnd_) DestroyWindow(hwnd_);
    if (dib_) DeleteObject(dib_);
    if (dibDc_) DeleteDC(dibDc_);
}

void Flyout::SetItems(const std::vector<Item>& items) {
    items_ = items;
    dirty_ = true;
    shapeDirty_ = true;
}

void Flyout::SetText(const wchar_t* text) {
    tip_ = text;
    dirty_ = true;
    shapeDirty_ = true;
}

void Flyout::SetChecked(int id) {
    int group = -1;
    for (auto& it : items_)
        if (it.type == Item::Radio && it.id == id) group = it.group;
    for (auto& it : items_)
        if (it.type == Item::Radio && it.group == group) it.checked = it.id == id;
    dirty_ = true;
    if (open_) Present(posX_, posY_);
}

void Flyout::SetEnabled(int id, bool enabled) {
    for (auto& it : items_)
        if (it.id == id) it.disabled = !enabled;
    dirty_ = true;
}

void Flyout::SetPalette(const theme::Palette& p) {
    pal_ = p;
    dirty_ = true;
    if (open_) Present(posX_, posY_);       // colours only: the size, the outline and the shadow stay
}

void Flyout::SetHot(int id, bool pressed) {
    hot_ = -1;
    for (size_t i = 0; i < items_.size(); i++)
        if (items_[i].id == id && !items_[i].disabled && (items_[i].type == Item::Radio || items_[i].type == Item::Button)) hot_ = (int)i;
    pressed_ = pressed ? hot_ : -1;
    dirty_ = true;
}

// ------------------------------------------------------------------ layout

void Flyout::Layout(HWND, float scale) {
    const int oldRoom = room_, oldBoxW = boxW_, oldBoxH = boxH_;
    const float oldScale = scale_;
    scale_ = scale;
    const float d = scale;
    room_ = kind_ == Tip ? 0 : (int)Rn(kShadowRoomDip * d);
    int bt = (int)fmaxf(1.0f, Rn(d));
    if (kind_ == Tip) {
        Font f = ui::TextFont(12.0f * d);
        float tw = Rn(gfx::TextWidth(tip_.c_str(), (int)tip_.size(), f));
        float th = Rn(gfx::LineHeight(f));
        boxW_ = (int)(tw + Rn(16.0f * d) + 2 * bt);
        boxH_ = (int)(th + Rn(10.0f * d) + 2 * bt);
    } else {
        Font fHeader = ui::TextFont(12.0f * d, 600);
        Font fText = ui::TextFont(14.0f * d);
        float minW = kind_ == Menu ? Rn(236.0f * d) : 0.0f;
        float w = minW;
        float rowH = Rn(34.0f * d) + 2.0f * Rn(d);
        float y = 0.0f;
        float padTop = Rn(6.0f * d);
        for (auto& it : items_) {
            switch (it.type) {
            case Item::Header: {
                float h = Rn(6.0f * d) + Rn(gfx::LineHeight(fHeader)) + Rn(2.0f * d);
                it.rect = { 0, y, 0, h };
                y += h;
                break;
            }
            case Item::Label: {
                float h = Rn(6.0f * d) + Rn(gfx::LineHeight(fText)) + Rn(4.0f * d);
                it.rect = { 0, y, 0, h };
                y += h;
                break;
            }
            case Item::Radio:
            case Item::Button: {
                float tw = Rn(gfx::TextWidth(it.text.c_str(), (int)it.text.size(), fText));
                float iw = Rn(4.0f * d) + Rn(34.0f * d) + tw + Rn(14.0f * d) + Rn(4.0f * d);
                if (iw + 2 * bt > w) w = iw + 2 * bt;
                it.rect = { 0, y, 0, rowH };
                y += rowH;
                break;
            }
            case Item::Separator: {
                float h = Rn(6.0f * d) + Rn(d) + Rn(6.0f * d);
                it.rect = { 0, y, 0, h };
                y += h;
                break;
            }
            }
        }
        boxW_ = (int)w;
        boxH_ = (int)(y + 2 * padTop + 2 * bt);
        // Final rectangles, relative to the box.
        float cur = (float)bt + padTop;
        for (auto& it : items_) {
            float h = it.rect.h;
            it.rect = { 0, cur, (float)boxW_, h };
            cur += h;
        }
    }
    winW_ = boxW_ + 2 * room_;
    winH_ = boxH_ + 2 * room_;
    dirty_ = true;
    // The outline and the shadow depend on the size and the scale only: opening the same menu again does not need them
    // again (the blur is most of the time it takes to show a menu).
    if (oldRoom != room_ || oldBoxW != boxW_ || oldBoxH != boxH_ || oldScale != scale_) shapeDirty_ = true;
}

// ------------------------------------------------------------------ painting

void Flyout::Paint(Canvas& c) {
    const float d = scale_;
    int bt = (int)fmaxf(1.0f, Rn(d));
    // A Border with CornerRadius 4 (tip) or 8 and a 1 px border: its outer edge has the radius + bt / 2, its inner edge the
    // radius - bt / 2 (see gfx::Canvas::BorderBox).
    float radius = (kind_ == Tip ? 4.0f * d : 8.0f * d) + (float)bt / 2.0f;
    if (!c.Resize(winW_, winH_)) return;
    gfx::SetTextSurface(pal_.flyout);
    c.Clear(pal_.flyoutEdge);
    RectF inner = { (float)(room_ + bt), (float)(room_ + bt), (float)(boxW_ - 2 * bt), (float)(boxH_ - 2 * bt) };
    c.FillRoundRect(inner, radius - bt, pal_.flyout);
    if (kind_ == Tip) {
        Font f = ui::TextFont(12.0f * d);
        c.Text(tip_.c_str(), (int)tip_.size(), f, (float)(room_ + bt) + Rn(8.0f * d), (float)(room_ + bt) + Rn(5.0f * d), pal_.text);
        return;
    }
    Font fHeader = ui::TextFont(12.0f * d, 600);
    Font fText = ui::TextFont(14.0f * d);
    float ox = (float)room_, oy = (float)room_;
    for (size_t i = 0; i < items_.size(); i++) {
        const Item& it = items_[i];
        switch (it.type) {
        case Item::Header:
            c.Text(it.text.c_str(), (int)it.text.size(), fHeader, ox + bt + Rn(16.0f * d), oy + it.rect.y + Rn(6.0f * d), pal_.textSecondary);
            break;
        case Item::Label:
            c.Text(it.text.c_str(), (int)it.text.size(), fText, ox + bt + Rn(16.0f * d), oy + it.rect.y + Rn(6.0f * d), pal_.text);
            break;
        case Item::Separator: {
            float y = oy + it.rect.y + Rn(6.0f * d);
            c.FillRect((int)(ox + bt + Rn(12.0f * d)), (int)y, boxW_ - 2 * bt - 2 * (int)Rn(12.0f * d), (int)Rn(d), pal_.flyoutEdge);
            break;
        }
        case Item::Radio:
        case Item::Button: {
            RectF r = { ox + bt + Rn(4.0f * d), oy + it.rect.y + Rn(d), (float)(boxW_ - 2 * bt) - 2 * Rn(4.0f * d), Rn(34.0f * d) };
            bool hot = (int)i == hot_ && !it.disabled;
            if (hot) c.FillRoundRect(r, 4.0f * d, Over((int)i == pressed_ ? pal_.subtlePressed : pal_.subtle, pal_.flyout));
            if (it.type == Item::Radio && it.checked) {
                float cx = r.x + Rn(17.0f * d), cy = r.y + r.h / 2.0f, rad = 3.0f * d;
                c.FillEllipse({ cx - rad, cy - rad, rad * 2, rad * 2 }, pal_.text);
            }
            float lh = Rn(gfx::LineHeight(fText));
            float ty = r.y + Rn((r.h - lh) / 2.0f);
            c.Text(it.text.c_str(), (int)it.text.size(), fText, r.x + Rn(34.0f * d), ty, it.disabled ? pal_.textDisabled : pal_.text);
            break;
        }
        }
    }
}

// Signed coverage of a rounded rectangle (anti-aliased, one pixel wide edge).
static float RoundRectCoverage(float px, float py, float cx, float cy, float hx, float hy, float r) {
    float qx = fabsf(px - cx) - (hx - r);
    float qy = fabsf(py - cy) - (hy - r);
    float ox = qx > 0 ? qx : 0, oy = qy > 0 ? qy : 0;
    float sd = sqrtf(ox * ox + oy * oy) + fminf(fmaxf(qx, qy), 0.0f) - r;
    float cov = 0.5f - sd;
    return cov < 0 ? 0 : (cov > 1 ? 1 : cov);
}

static void GaussBlur(std::vector<float>& a, int w, int h, float sigma) {
    int rad = (int)ceilf(sigma * 3.0f);
    if (rad < 1) return;
    std::vector<float> k((size_t)(rad * 2 + 1));
    float sum = 0;
    for (int i = -rad; i <= rad; i++) {
        float v = expf(-(float)(i * i) / (2.0f * sigma * sigma));
        k[(size_t)(i + rad)] = v;
        sum += v;
    }
    for (auto& v : k) v /= sum;
    std::vector<float> t(a.size());
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float acc = 0;
            for (int i = -rad; i <= rad; i++) {
                int xx = x + i;
                if (xx >= 0 && xx < w) acc += a[(size_t)y * w + xx] * k[(size_t)(i + rad)];
            }
            t[(size_t)y * w + x] = acc;
        }
    for (int y = 0; y < h; y++)
        for (int x = 0; x < w; x++) {
            float acc = 0;
            for (int i = -rad; i <= rad; i++) {
                int yy = y + i;
                if (yy >= 0 && yy < h) acc += t[(size_t)yy * w + x] * k[(size_t)(i + rad)];
            }
            a[(size_t)y * w + x] = acc;
        }
}

void Flyout::BuildPixels() {
    const float d = scale_;
    if (shapeDirty_) {
        shapeBuilds_++;
        float radius = (kind_ == Tip ? 4.0f * d : 8.0f * d) + (float)(int)fmaxf(1.0f, Rn(d)) / 2.0f;
        mask_.assign((size_t)winW_ * winH_, 0);
        shadow_.assign((size_t)winW_ * winH_, 0);
        float cx = room_ + boxW_ / 2.0f, cy = room_ + boxH_ / 2.0f, hx = boxW_ / 2.0f, hy = boxH_ / 2.0f;
        for (int y = 0; y < winH_; y++)
            for (int x = 0; x < winW_; x++)
                mask_[(size_t)y * winW_ + x] = (uint8_t)(RoundRectCoverage(x + 0.5f, y + 0.5f, cx, cy, hx, hy, radius) * 255.0f + 0.5f);
        if (room_ > 0) {
            std::vector<float> a((size_t)winW_ * winH_);
            float shift = kShadowDepth * d;
            for (int y = 0; y < winH_; y++)
                for (int x = 0; x < winW_; x++)
                    a[(size_t)y * winW_ + x] = RoundRectCoverage(x + 0.5f, y + 0.5f - shift, cx, cy, hx, hy, radius);
            GaussBlur(a, winW_, winH_, kShadowSigma * d);
            for (size_t i = 0; i < a.size(); i++) shadow_[i] = (uint8_t)(a[i] * kShadowOpacity * 255.0f + 0.5f);
        }
        shapeDirty_ = false;
    }
    Paint(canvas_);
    canvas_.Flush();
    if (!canvas_.Valid()) {             // no bitmap to paint on: show nothing instead of garbage
        pixels_.assign((size_t)winW_ * winH_, 0);
        dirty_ = false;
        return;
    }
    pixels_.resize((size_t)winW_ * winH_);
    for (int y = 0; y < winH_; y++) {
        const uint32_t* src = reinterpret_cast<const uint32_t*>(canvas_.Row(y));
        for (int x = 0; x < winW_; x++) {
            size_t i = (size_t)y * winW_ + x;
            unsigned cov = mask_[i], sh = shadow_[i];
            unsigned a = cov + (sh * (255 - cov) + 127) / 255;
            uint32_t s = src[x];
            unsigned r = ((s >> 16) & 255) * cov / 255, g = ((s >> 8) & 255) * cov / 255, b = (s & 255) * cov / 255;
            pixels_[i] = (a << 24) | (r << 16) | (g << 8) | b;
        }
    }
    dirty_ = false;
}

void Flyout::ComposeOver(Canvas& dst, int boxX, int boxY) {
    if (dirty_ || pixels_.empty()) BuildPixels();
    int ox = boxX - room_, oy = boxY - room_;
    for (int y = 0; y < winH_; y++) {
        int dy = oy + y;
        if (dy < 0 || dy >= dst.H()) continue;
        uint32_t* row = reinterpret_cast<uint32_t*>(dst.Row(dy));
        for (int x = 0; x < winW_; x++) {
            int dx = ox + x;
            if (dx < 0 || dx >= dst.W()) continue;
            uint32_t p = pixels_[(size_t)y * winW_ + x];
            unsigned a = p >> 24;
            if (a == 0) continue;
            uint32_t b = row[dx];
            unsigned inv = 255 - a;
            unsigned r = ((p >> 16) & 255) + (((b >> 16) & 255) * inv + 127) / 255;
            unsigned g = ((p >> 8) & 255) + (((b >> 8) & 255) * inv + 127) / 255;
            unsigned bl = (p & 255) + ((b & 255) * inv + 127) / 255;
            row[dx] = 0xFF000000u | ((r > 255 ? 255 : r) << 16) | ((g > 255 ? 255 : g) << 8) | (bl > 255 ? 255 : bl);
        }
    }
}

// ------------------------------------------------------------------ the window

void Flyout::Ensure(HWND owner) {
    owner_ = owner;
    if (hwnd_) return;
    if (!g_class) {
        WNDCLASSEXW wc = { sizeof wc };
        wc.lpfnWndProc = StaticProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        wc.lpszClassName = kClassName;
        g_class = RegisterClassExW(&wc);
    }
    DWORD ex = WS_EX_LAYERED | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | (kind_ == Tip ? WS_EX_TRANSPARENT : 0);
    hwnd_ = CreateWindowExW(ex, kClassName, L"", WS_POPUP, 0, 0, 1, 1, owner, nullptr, GetModuleHandleW(nullptr), this);
}

LRESULT CALLBACK Flyout::StaticProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    Flyout* self = reinterpret_cast<Flyout*>(GetWindowLongPtrW(h, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        self = static_cast<Flyout*>(reinterpret_cast<CREATESTRUCTW*>(lp)->lpCreateParams);
        SetWindowLongPtrW(h, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
        if (self) self->hwnd_ = h;
    }
    if (self) {
        // An exception must never unwind through the window procedure (the core throws on arithmetic overflow).
        try {
            return self->Proc(msg, wp, lp);
        } catch (...) {
            OutputDebugStringW(L"HourCalc: an exception reached the window procedure of a popup\n");
            return DefWindowProcW(h, msg, wp, lp);
        }
    }
    return DefWindowProcW(h, msg, wp, lp);
}

void Flyout::Present(int sx, int sy) {
#ifdef HC_TESTHOOKS
    ++presents_;
#endif
    if (dirty_ || pixels_.empty()) BuildPixels();
    if (!dibDc_) dibDc_ = CreateCompatibleDC(nullptr);
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof bi.bmiHeader;
    bi.bmiHeader.biWidth = winW_;
    bi.bmiHeader.biHeight = -winH_;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    bi.bmiHeader.biCompression = BI_RGB;
    if (dib_) { DeleteObject(dib_); dib_ = nullptr; }
    void* bits = nullptr;
    dib_ = CreateDIBSection(nullptr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    if (!dib_) return;
    dibBits_ = static_cast<uint32_t*>(bits);
    memcpy(dibBits_, pixels_.data(), pixels_.size() * 4);
    HGDIOBJ old = SelectObject(dibDc_, dib_);
    POINT dst = { sx - room_, sy - room_ };
    SIZE sz = { winW_, winH_ };
    POINT src = { 0, 0 };
    BLENDFUNCTION bf = { AC_SRC_OVER, 0, 255, AC_SRC_ALPHA };
    HDC screen = GetDC(nullptr);
    UpdateLayeredWindow(hwnd_, screen, &dst, &sz, dibDc_, &src, 0, &bf, ULW_ALPHA);
    ReleaseDC(nullptr, screen);
    SelectObject(dibDc_, old);
}

POINT Flyout::Fit(int x, int y, int w, int h, const RECT& work) {
    if (x + w > work.right) x = work.right - w;
    if (y + h > work.bottom) y = work.bottom - h;
    if (x < work.left) x = work.left;
    if (y < work.top) y = work.top;
    return { x, y };
}

void Flyout::Open(HWND owner, int sx, int sy) {
    Ensure(owner);
    if (!hwnd_) return;
    // Keep the box on the screen.
    POINT pt = { sx, sy };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof mi };
    if (GetMonitorInfoW(mon, &mi)) {
        POINT fit = Fit(sx, sy, boxW_, boxH_, mi.rcWork);
        sx = fit.x;
        sy = fit.y;
    }
    posX_ = sx;
    posY_ = sy;
    hot_ = -1;
    pressed_ = -1;
    dirty_ = true;
    open_ = true;
    Present(sx, sy);
    SetWindowPos(hwnd_, HWND_TOP, sx - room_, sy - room_, winW_, winH_, SWP_NOACTIVATE | SWP_SHOWWINDOW);
    Present(sx, sy);
    if (kind_ != Tip) SetCapture(hwnd_);
}

#ifdef HC_TESTHOOKS
void Flyout::OpenHiddenForTest(HWND owner, int sx, int sy) {
    Ensure(owner);
    if (!hwnd_) return;
    posX_ = sx;
    posY_ = sy;
    hot_ = -1;
    pressed_ = -1;
    dirty_ = true;
    open_ = true;
    Present(sx, sy);
}
#endif

void Flyout::Close() {
    if (!open_) return;
    open_ = false;
    tracking_ = false;
    if (GetCapture() == hwnd_) ReleaseCapture();
    ShowWindow(hwnd_, SW_HIDE);
    if (owner_) PostMessageW(owner_, WM_FLYOUT_CLOSED, (WPARAM)kind_, (LPARAM)GetTickCount());
}

bool Flyout::BoxContains(int x, int y) const {
    return x >= room_ && y >= room_ && x - room_ < boxW_ && y - room_ < boxH_;
}

int Flyout::ItemAt(int x, int y, bool disabledToo) const {
    float bx = (float)(x - room_), by = (float)(y - room_);
    if (bx < 0 || by < 0 || bx >= boxW_ || by >= boxH_) return -1;
    for (size_t i = 0; i < items_.size(); i++) {
        const Item& it = items_[i];
        if ((it.type == Item::Radio || it.type == Item::Button) && (disabledToo || !it.disabled) && by >= it.rect.y && by < it.rect.y + it.rect.h)
            return (int)i;
    }
    return -1;
}

// A click outside the popup closes it and then goes on to the window it was meant for, as the WPF popup (StaysOpen = false)
// does: the first click on a key while the menu is open also presses the key. Only clicks on the owner go on; a click
// on another program takes the mouse capture away from the popup by itself. x, y: popup client coordinates.
void Flyout::ForwardClick(UINT msg, int x, int y) {
    if (!owner_ || !IsWindow(owner_)) return;
    POINT pt = { x, y };
    ClientToScreen(hwnd_, &pt);
    RECT wr;
    if (!GetWindowRect(owner_, &wr) || !PtInRect(&wr, pt)) return;
    bool right = msg == WM_RBUTTONDOWN;
    LRESULT ht = SendMessageW(owner_, WM_NCHITTEST, 0, MAKELPARAM(pt.x, pt.y));
    if (ht == HTCLIENT) {
        ScreenToClient(owner_, &pt);
        SendMessageW(owner_, right ? WM_RBUTTONDOWN : WM_LBUTTONDOWN, right ? MK_RBUTTON : MK_LBUTTON, MAKELPARAM(pt.x, pt.y));
    } else if (ht != HTNOWHERE && ht != HTTRANSPARENT) {
        PostMessageW(owner_, right ? WM_NCRBUTTONDOWN : WM_NCLBUTTONDOWN, (WPARAM)ht, MAKELPARAM(pt.x, pt.y));
    }
}

void Flyout::Track(int x, int y) {
    int hot = ItemAt(x, y);
    if (hot != hot_) {
        hot_ = hot;
        dirty_ = true;
        Present(posX_, posY_);
    }
}

LRESULT Flyout::Proc(UINT msg, WPARAM wp, LPARAM lp) {
    int x = (short)LOWORD(lp), y = (short)HIWORD(lp);
    switch (msg) {
    case WM_MOUSEACTIVATE:
        return MA_NOACTIVATE;
    case WM_NCHITTEST:
        return kind_ == Tip ? HTTRANSPARENT : HTCLIENT;
    case WM_MOUSEMOVE:
        if (open_ && kind_ != Tip) Track(x, y);
        return 0;
    case WM_LBUTTONDOWN:
    case WM_RBUTTONDOWN:
    case WM_NCLBUTTONDOWN: {
        if (!open_ || kind_ == Tip) return 0;
        if (msg == WM_NCLBUTTONDOWN) {
            Close();
            return 0;
        }
        int it = ItemAt(x, y);
        if (it >= 0 && msg == WM_LBUTTONDOWN) {
            pressed_ = it;
            dirty_ = true;
            Present(posX_, posY_);
            return 0;
        }
        // A click inside the box that is not the press of an enabled item (the header, a label, a separator, the padding, a greyed
        // out item, a right click) does nothing: the menu stays open, as the WPF popup (StaysOpen = false) does, which only closes
        // for a click outside its content.
        if (BoxContains(x, y)) return 0;
        Close();
        ForwardClick(msg, x, y);
        return 0;
    }
    case WM_LBUTTONUP: {
        if (!open_ || kind_ == Tip) return 0;
        int it = ItemAt(x, y);
        int p = pressed_;
        pressed_ = -1;
        if (p >= 0 && it == p) {
            int id = items_[(size_t)it].id;
            Close();
            PostMessageW(owner_, WM_FLYOUT_COMMAND, (WPARAM)id, (LPARAM)kind_);
        } else {
            dirty_ = true;
            Present(posX_, posY_);
        }
        return 0;
    }
    case WM_CAPTURECHANGED:
        if (open_ && kind_ != Tip && (HWND)lp != hwnd_) Close();
        return 0;
    case WM_DESTROY:
        hwnd_ = nullptr;
        return 0;
    }
    return DefWindowProcW(hwnd_, msg, wp, lp);
}

}  // namespace flyout
