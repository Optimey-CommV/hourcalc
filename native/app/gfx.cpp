// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the drawing layer, see gfx.h.

#include "gfx.h"

#include "gdip.h"
#include <dwrite.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>

namespace gfx {

using namespace Gdiplus;

static Color Col(Argb c) { return Color((ARGB)c); }

// ------------------------------------------------------------------ colours

Argb Over(Argb over, Argb under, float opacity) {
    float a = (A(over) / 255.0f) * opacity;
    if (a >= 0.9999f) return Rgb(R(over), G(over), B(over));
    float r = R(over) * a + R(under) * (1.0f - a);
    float g = G(over) * a + G(under) * (1.0f - a);
    float b = B(over) * a + B(under) * (1.0f - a);
    return Rgb((unsigned)(r + 0.5f), (unsigned)(g + 0.5f), (unsigned)(b + 0.5f));
}

Argb Mix(Argb a, Argb b, float t) {
    float r = R(a) + (float(R(b)) - R(a)) * t;
    float g = G(a) + (float(G(b)) - G(a)) * t;
    float bl = B(a) + (float(B(b)) - B(a)) * t;
    return Rgb((unsigned)(r + 0.5f), (unsigned)(g + 0.5f), (unsigned)(bl + 0.5f));
}

// ------------------------------------------------------------------ globals

static ULONG_PTR g_gdipToken = 0;
static IDWriteFactory* g_dw = nullptr;
static IDWriteGdiInterop* g_interop = nullptr;
static IDWriteFontCollection* g_sysFonts = nullptr;
static IDWriteRenderingParams* g_paramsLight = nullptr;     // dark text on a lighter ground
static IDWriteRenderingParams* g_paramsDark = nullptr;      // lighter text on a darker ground
static float g_surfaceLuma = 0.95f;                         // what the text is drawn on, see SetTextSurface

static const wchar_t* const kFamilies[FamCount][2] = {
    { L"Segoe UI Variable Text", L"Segoe UI" },
    { L"Segoe UI Variable Display", L"Segoe UI" },
    { L"Segoe Fluent Icons", L"Segoe MDL2 Assets" },
    { L"Cascadia Mono", L"Consolas" },
};
static const wchar_t* g_famName[FamCount] = {};
static float g_ratio[FamCount][2] = {};     // line height / em size, for the regular and the heavier weights

const wchar_t* FamilyName(Family fam) {
    if (!g_famName[fam]) {
        UINT32 index = 0;
        BOOL exists = FALSE;
        if (g_sysFonts) g_sysFonts->FindFamilyName(kFamilies[fam][0], &index, &exists);
        g_famName[fam] = exists ? kFamilies[fam][0] : kFamilies[fam][1];
    }
    return g_famName[fam];
}

static float Luma(Argb c) {
    return (0.2126f * (float)R(c) + 0.7152f * (float)G(c) + 0.0722f * (float)B(c)) / 255.0f;
}

void SetTextSurface(Argb surface) {
    g_surfaceLuma = Luma(surface);
}

bool IsLightOnDark(Argb text) {
    return Luma(text) > g_surfaceLuma;
}

// Text lighter than its ground is drawn with the calibration of the dark theme, darker text with the other one.
static IDWriteRenderingParams* ParamsFor(Argb text) {
    return IsLightOnDark(text) ? g_paramsDark : g_paramsLight;
}

bool Init() {
    GdiplusStartupInput in;
    if (GdiplusStartup(&g_gdipToken, &in, nullptr) != Ok) return false;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), reinterpret_cast<IUnknown**>(&g_dw)))) return false;
    if (FAILED(g_dw->GetGdiInterop(&g_interop))) return false;
    g_dw->GetSystemFontCollection(&g_sysFonts, FALSE);
    // Grayscale anti-aliasing (like the WPF build) with natural symmetric glyph positioning. The gamma and
    // contrast are calibrated against the WPF snapshots: one pair for dark text on a lighter ground, one for
    // lighter text on a darker ground (ParamsFor).
    float gl = 1.0f, cl = 0.5f, gd = 1.4f, cd = 1.0f;
    DWRITE_RENDERING_MODE mode = DWRITE_RENDERING_MODE_NATURAL_SYMMETRIC;
#ifdef HC_TESTHOOKS
    // Calibration runs: --render with HC_GAMMA, HC_CONTRAST, HC_RMODE set (both themes).
    char buf[32];
    if (GetEnvironmentVariableA("HC_GAMMA", buf, sizeof buf)) gl = gd = (float)atof(buf);
    if (GetEnvironmentVariableA("HC_CONTRAST", buf, sizeof buf)) cl = cd = (float)atof(buf);
    if (GetEnvironmentVariableA("HC_RMODE", buf, sizeof buf)) mode = (DWRITE_RENDERING_MODE)atoi(buf);
#endif
    g_dw->CreateCustomRenderingParams(gl, cl, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, mode, &g_paramsLight);
    g_dw->CreateCustomRenderingParams(gd, cd, 0.0f, DWRITE_PIXEL_GEOMETRY_FLAT, mode, &g_paramsDark);
    return true;
}

void Shutdown() {
    if (g_paramsLight) { g_paramsLight->Release(); g_paramsLight = nullptr; }
    if (g_paramsDark) { g_paramsDark->Release(); g_paramsDark = nullptr; }
    if (g_sysFonts) { g_sysFonts->Release(); g_sysFonts = nullptr; }
    if (g_interop) { g_interop->Release(); g_interop = nullptr; }
    if (g_dw) { g_dw->Release(); g_dw = nullptr; }
    if (g_gdipToken) { GdiplusShutdown(g_gdipToken); g_gdipToken = 0; }
}

// ------------------------------------------------------------------ text formats and layouts

struct FormatSlot {
    Family fam;
    int weight;
    int sizeKey;
    IDWriteTextFormat* fmt;
    IDWriteTextFormat* fmtWrap;
};
static FormatSlot g_formats[32];
static int g_formatNext = 0;

// The format of a slot, or its wrapping twin (made on first use). Null when DirectWrite refuses the font (a size of
// 0 or less, out of memory): the callers then skip the text.
static IDWriteTextFormat* Variant(FormatSlot& s, const Font& f, bool wrap) {
    if (!wrap) return s.fmt;
    if (!s.fmtWrap) {
        g_dw->CreateTextFormat(FamilyName(f.fam), nullptr, (DWRITE_FONT_WEIGHT)f.weight, DWRITE_FONT_STYLE_NORMAL,
                               DWRITE_FONT_STRETCH_NORMAL, f.size, L"en-us", &s.fmtWrap);
        if (s.fmtWrap) s.fmtWrap->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    }
    return s.fmtWrap;
}

static IDWriteTextFormat* GetFormat(const Font& f, bool wrap) {
    int key = (int)(f.size * 64.0f + 0.5f);
    for (auto& s : g_formats)
        if (s.fmt && s.fam == f.fam && s.weight == f.weight && s.sizeKey == key) return Variant(s, f, wrap);
    FormatSlot& s = g_formats[g_formatNext];
    g_formatNext = (g_formatNext + 1) % (int)(sizeof g_formats / sizeof g_formats[0]);
    if (s.fmt) s.fmt->Release();
    if (s.fmtWrap) s.fmtWrap->Release();
    s.fmt = s.fmtWrap = nullptr;
    s.fam = f.fam;
    s.weight = f.weight;
    s.sizeKey = key;
    g_dw->CreateTextFormat(FamilyName(f.fam), nullptr, (DWRITE_FONT_WEIGHT)f.weight, DWRITE_FONT_STYLE_NORMAL,
                           DWRITE_FONT_STRETCH_NORMAL, f.size, L"en-us", &s.fmt);
    if (!s.fmt) return nullptr;
    s.fmt->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return Variant(s, f, wrap);
}

static IDWriteTextLayout* MakeLayout(const wchar_t* s, int len, const Font& f, float maxWidth) {
    bool wrap = maxWidth > 0.0f;
    IDWriteTextFormat* fmt = GetFormat(f, wrap);
    if (!fmt) return nullptr;
    IDWriteTextLayout* lay = nullptr;
    static const wchar_t kEmpty[] = L"";
    g_dw->CreateTextLayout(len > 0 ? s : kEmpty, len > 0 ? (UINT32)len : 0, fmt, wrap ? maxWidth : 100000.0f, 100000.0f, &lay);
    return lay;
}

float LineHeight(const Font& f) {
    // Proportional to the em size: measured once per family and weight class at a large size.
    int cls = f.weight > 450 ? 1 : 0;
    float& r = g_ratio[f.fam][cls];
    if (r == 0.0f) {
        Font ref = { f.fam, f.weight, 200.0f };
        IDWriteTextLayout* lay = MakeLayout(L"0", 1, ref, 0.0f);
        float h = 1.3333f * 200.0f;
        if (lay) {
            DWRITE_TEXT_METRICS tm;
            if (SUCCEEDED(lay->GetMetrics(&tm))) h = tm.height;
            lay->Release();
        }
        r = h / 200.0f;
    }
    return r * f.size;
}

float Baseline(const Font& f) {
    Font ref = { f.fam, f.weight, 200.0f };
    IDWriteTextLayout* lay = MakeLayout(L"0", 1, ref, 0.0f);
    float b = 1.0f * 200.0f;
    if (lay) {
        DWRITE_LINE_METRICS lm;
        UINT32 n = 0;
        lay->GetLineMetrics(&lm, 1, &n);
        if (n) b = lm.baseline;
        lay->Release();
    }
    return b / 200.0f * f.size;
}

float TextWidth(const wchar_t* s, int len, const Font& f) {
    if (len <= 0) return 0.0f;
    IDWriteTextLayout* lay = MakeLayout(s, len, f, 0.0f);
    if (!lay) return 0.0f;
    DWRITE_TEXT_METRICS tm;
    float w = 0.0f;
    if (SUCCEEDED(lay->GetMetrics(&tm))) w = tm.widthIncludingTrailingWhitespace;
    lay->Release();
    return w;
}

float TextHeightWrapped(const wchar_t* s, int len, const Font& f, float maxWidth) {
    IDWriteTextLayout* lay = MakeLayout(s, len, f, maxWidth);
    if (!lay) return 0.0f;
    DWRITE_TEXT_METRICS tm;
    float h = 0.0f;
    if (SUCCEEDED(lay->GetMetrics(&tm))) h = tm.height;
    lay->Release();
    return h;
}

float CaretX(const wchar_t* s, int len, const Font& f, int index) {
    if (len <= 0 || index <= 0) return 0.0f;
    IDWriteTextLayout* lay = MakeLayout(s, len, f, 0.0f);
    if (!lay) return 0.0f;
    float x = 0.0f, y = 0.0f;
    DWRITE_HIT_TEST_METRICS m;
    if (index >= len) {
        lay->HitTestTextPosition((UINT32)(len - 1), TRUE, &x, &y, &m);
    } else {
        lay->HitTestTextPosition((UINT32)index, FALSE, &x, &y, &m);
    }
    lay->Release();
    return x;
}

int HitIndex(const wchar_t* s, int len, const Font& f, float x) {
    if (len <= 0) return 0;
    IDWriteTextLayout* lay = MakeLayout(s, len, f, 0.0f);
    if (!lay) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m;
    lay->HitTestPoint(x, 1.0f, &trailing, &inside, &m);
    lay->Release();
    int idx = (int)m.textPosition + (trailing ? 1 : 0);
    if (idx < 0) idx = 0;
    if (idx > len) idx = len;
    return idx;
}

// ------------------------------------------------------------------ the text renderer

struct DrawCtx {
    IDWriteBitmapRenderTarget* rt;
    IDWriteRenderingParams* params;
    COLORREF color;
    float limitY;      // glyph runs whose baseline is far outside the canvas are not rasterized
};

class TextRenderer : public IDWriteTextRenderer {
public:
    STDMETHOD(QueryInterface)(REFIID riid, void** out) override {
        if (IsEqualIID(riid, __uuidof(IUnknown)) || IsEqualIID(riid, __uuidof(IDWritePixelSnapping)) ||
            IsEqualIID(riid, __uuidof(IDWriteTextRenderer))) {
            *out = this;
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    STDMETHOD_(ULONG, AddRef)() override { return 1; }
    STDMETHOD_(ULONG, Release)() override { return 1; }
    STDMETHOD(IsPixelSnappingDisabled)(void*, BOOL* disabled) override { *disabled = TRUE; return S_OK; }
    STDMETHOD(GetCurrentTransform)(void*, DWRITE_MATRIX* m) override {
        m->m11 = 1; m->m12 = 0; m->m21 = 0; m->m22 = 1; m->dx = 0; m->dy = 0;
        return S_OK;
    }
    STDMETHOD(GetPixelsPerDip)(void*, FLOAT* p) override { *p = 1.0f; return S_OK; }
    STDMETHOD(DrawGlyphRun)(void* ctx, FLOAT x, FLOAT y, DWRITE_MEASURING_MODE mode, const DWRITE_GLYPH_RUN* run,
                            const DWRITE_GLYPH_RUN_DESCRIPTION*, IUnknown*) override {
        DrawCtx* c = static_cast<DrawCtx*>(ctx);
        if (y < -120.0f || y > c->limitY) return S_OK;
        return c->rt->DrawGlyphRun(x, floorf(y + 0.5f), mode, run, c->params, c->color, nullptr);
    }
    STDMETHOD(DrawUnderline)(void*, FLOAT, FLOAT, const DWRITE_UNDERLINE*, IUnknown*) override { return S_OK; }
    STDMETHOD(DrawStrikethrough)(void*, FLOAT, FLOAT, const DWRITE_STRIKETHROUGH*, IUnknown*) override { return S_OK; }
    STDMETHOD(DrawInlineObject)(void*, FLOAT, FLOAT, IDWriteInlineObject*, BOOL, BOOL, IUnknown*) override { return S_OK; }
};
static TextRenderer g_renderer;

// ------------------------------------------------------------------ Canvas

Canvas::Canvas() : rt_(nullptr), dc_(nullptr), bits_(nullptr), stride_(0), topDown_(true), w_(0), h_(0), bmp_(nullptr), g_(nullptr) {}

Canvas::~Canvas() {
    Release();
    if (rt_) static_cast<IDWriteBitmapRenderTarget*>(rt_)->Release();
}

void Canvas::Release() {
    delete g_;
    g_ = nullptr;
    delete bmp_;
    bmp_ = nullptr;
}

bool Canvas::Resize(int w, int h) {
    if (w < 1) w = 1;
    if (h < 1) h = 1;
    if (rt_ && g_ && w == w_ && h == h_) return true;
    Release();
    // From here on the canvas is empty until the new bitmap is ready: a failure leaves nothing stale behind.
    dc_ = nullptr;
    bits_ = nullptr;
    w_ = h_ = 0;
    IDWriteBitmapRenderTarget* rt = static_cast<IDWriteBitmapRenderTarget*>(rt_);
    if (!rt) {
        // The screen DC is only the template of the memory DC the target makes: take it for this call, a DC kept
        // for the life of the process can go stale (a display driver reset, a remote session change).
        HDC ref = GetDC(nullptr);
        HRESULT hr = ref ? g_interop->CreateBitmapRenderTarget(ref, (UINT32)w, (UINT32)h, &rt) : E_FAIL;
        if (ref) ReleaseDC(nullptr, ref);
        if (FAILED(hr)) return false;
        rt_ = rt;
        rt->SetPixelsPerDip(1.0f);
    } else if (FAILED(rt->Resize((UINT32)w, (UINT32)h))) {
        rt->Release();           // its state after a failed resize is unknown: the next call makes a new one
        rt_ = nullptr;
        return false;
    }
    HDC dc = rt->GetMemoryDC();
    HBITMAP hb = (HBITMAP)GetCurrentObject(dc, OBJ_BITMAP);
    DIBSECTION ds;
    if (!hb || GetObjectW(hb, sizeof ds, &ds) < (int)sizeof(BITMAP) || !ds.dsBm.bmBits) return false;
    w_ = w;
    h_ = h;
    dc_ = dc;
    bits_ = static_cast<uint8_t*>(ds.dsBm.bmBits);
    stride_ = ds.dsBm.bmWidthBytes;
    topDown_ = true;     // the DirectWrite target uses a top-down DIB (GetObject reports a positive height anyway)
    if (topDown_)
        bmp_ = new Bitmap(w, h, stride_, PixelFormat32bppPARGB, bits_);
    else
        bmp_ = new Bitmap(w, h, -stride_, PixelFormat32bppPARGB, bits_ + (size_t)(h - 1) * stride_);
    g_ = new Graphics(bmp_);
    g_->SetSmoothingMode(SmoothingModeAntiAlias);
    g_->SetPixelOffsetMode(PixelOffsetModeHalf);
    g_->SetCompositingMode(CompositingModeSourceOver);
    return true;
}

uint8_t* Canvas::Row(int y) const {
    return topDown_ ? bits_ + (size_t)y * stride_ : bits_ + (size_t)(h_ - 1 - y) * stride_;
}

// DirectWrite may batch its writes to the DIB through GDI: make them land before the bits are read or blitted.
void Canvas::Flush() {
    GdiFlush();
}

void Canvas::Clear(Argb c) {
    for (int y = 0; y < h_; y++) {
        uint32_t* p = reinterpret_cast<uint32_t*>(Row(y));
        for (int x = 0; x < w_; x++) p[x] = c | 0xFF000000u;
    }
}

void Canvas::FillRect(int x, int y, int w, int h, Argb c) {
    int x0 = max(x, 0), y0 = max(y, 0), x1 = min(x + w, w_), y1 = min(y + h, h_);
    unsigned a = A(c);
    for (int yy = y0; yy < y1; yy++) {
        uint32_t* p = reinterpret_cast<uint32_t*>(Row(yy));
        for (int xx = x0; xx < x1; xx++) {
            if (a >= 255) {
                p[xx] = c | 0xFF000000u;
            } else {
                uint32_t d = p[xx];
                unsigned r = (R(c) * a + R(d) * (255 - a) + 127) / 255;
                unsigned g = (G(c) * a + G(d) * (255 - a) + 127) / 255;
                unsigned b = (B(c) * a + B(d) * (255 - a) + 127) / 255;
                p[xx] = 0xFF000000u | (r << 16) | (g << 8) | b;
            }
        }
    }
}

static void AddRoundRect(GraphicsPath& p, float x, float y, float w, float h, float r) {
    if (r <= 0.0f) {
        p.AddRectangle(Gdiplus::RectF(x, y, w, h));
        return;
    }
    float d = r * 2.0f;
    if (d > w) d = w;
    if (d > h) d = h;
    p.AddArc(x, y, d, d, 180.0f, 90.0f);
    p.AddArc(x + w - d, y, d, d, 270.0f, 90.0f);
    p.AddArc(x + w - d, y + h - d, d, d, 0.0f, 90.0f);
    p.AddArc(x, y + h - d, d, d, 90.0f, 90.0f);
    p.CloseFigure();
}

void Canvas::FillRoundRect(const RectF& r, float radius, Argb c) {
    if (!g_) return;
    GraphicsPath p;
    AddRoundRect(p, r.x, r.y, r.w, r.h, radius);
    SolidBrush br(Col(c));
    g_->FillPath(&br, &p);
}

void Canvas::FillEllipse(const RectF& r, Argb c) {
    if (!g_) return;
    SolidBrush br(Col(c));
    g_->FillEllipse(&br, r.x, r.y, r.w, r.h);
}

void Canvas::BorderGradient(const RectF& r, float radius, Argb top, Argb bottom, float t) {
    if (!g_) return;
    GraphicsPath p;
    p.SetFillMode(FillModeAlternate);
    AddRoundRect(p, r.x, r.y, r.w, r.h, radius);
    AddRoundRect(p, r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t, radius > t ? radius - t : 0.0f);
    LinearGradientBrush br(Gdiplus::RectF(r.x, r.y, r.w, r.h), Col(top),
                           Col(bottom), LinearGradientModeVertical);
    Color cols[3] = { Col(top), Col(top),
                      Col(bottom) };
    REAL pos[3] = { 0.0f, 0.9f, 1.0f };
    br.SetInterpolationColors(cols, pos, 3);
    g_->FillPath(&br, &p);
}

void Canvas::BorderSolid(const RectF& r, float radius, Argb c, float t) {
    if (!g_) return;
    GraphicsPath p;
    p.SetFillMode(FillModeAlternate);
    AddRoundRect(p, r.x, r.y, r.w, r.h, radius);
    AddRoundRect(p, r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t, radius > t ? radius - t : 0.0f);
    SolidBrush br(Col(c));
    g_->FillPath(&br, &p);
}

void Canvas::BorderBox(const RectF& r, float cornerRadius, float t, Argb fill, Argb top, Argb bottom, bool solid) {
    RectF in = { r.x + t, r.y + t, r.w - 2 * t, r.h - 2 * t };
    float inner = cornerRadius - t / 2.0f;
    FillRoundRect(in, inner > 0.0f ? inner : 0.0f, fill);
    float outer = cornerRadius + t / 2.0f;
    if (solid) BorderSolid(r, outer, top, t);
    else BorderGradient(r, outer, top, bottom, t);
}

void Canvas::Text(const wchar_t* s, int len, const Font& f, float x, float y, Argb color, float maxWidth) {
    if (len <= 0 || !g_) return;
    IDWriteTextLayout* lay = MakeLayout(s, len, f, maxWidth);
    if (!lay) return;
    DrawCtx ctx = { static_cast<IDWriteBitmapRenderTarget*>(rt_), ParamsFor(color), ToColorRef(color), (float)h_ + 120.0f };
    lay->Draw(&ctx, &g_renderer, x, y);
    lay->Release();
}

// ------------------------------------------------------------------ TextBlock

TextBlock::TextBlock() : layout_(nullptr), height_(0.0f) {}

TextBlock::~TextBlock() {
    if (layout_) static_cast<IDWriteTextLayout*>(layout_)->Release();
}

bool TextBlock::Set(const wchar_t* s, int len, const Font& f, float maxWidth) {
    if (layout_) {
        static_cast<IDWriteTextLayout*>(layout_)->Release();
        layout_ = nullptr;
    }
    IDWriteTextLayout* lay = MakeLayout(s, len, f, maxWidth);
    if (!lay) return false;
    DWRITE_TEXT_METRICS tm;
    height_ = SUCCEEDED(lay->GetMetrics(&tm)) ? tm.height : 0.0f;
    layout_ = lay;
    return true;
}

void TextBlock::Draw(Canvas& c, float x, float y, Argb color) const {
    if (!layout_ || !c.g_) return;
    DrawCtx ctx = { static_cast<IDWriteBitmapRenderTarget*>(c.rt_), ParamsFor(color), ToColorRef(color), (float)c.h_ + 120.0f };
    static_cast<IDWriteTextLayout*>(layout_)->Draw(&ctx, &g_renderer, x, y);
}

int TextBlock::HitIndex(float x, float y) const {
    if (!layout_) return 0;
    BOOL trailing = FALSE, inside = FALSE;
    DWRITE_HIT_TEST_METRICS m;
    static_cast<IDWriteTextLayout*>(layout_)->HitTestPoint(x, y, &trailing, &inside, &m);
    return (int)m.textPosition + (trailing ? 1 : 0);
}

void TextBlock::Selection(int start, int end, float x, float y, std::vector<RectF>& out) const {
    out.clear();
    if (!layout_ || end <= start) return;
    IDWriteTextLayout* lay = static_cast<IDWriteTextLayout*>(layout_);
    UINT32 count = 0;
    lay->HitTestTextRange((UINT32)start, (UINT32)(end - start), 0.0f, 0.0f, nullptr, 0, &count);
    if (!count) return;
    std::vector<DWRITE_HIT_TEST_METRICS> m(count);
    if (FAILED(lay->HitTestTextRange((UINT32)start, (UINT32)(end - start), 0.0f, 0.0f, m.data(), count, &count))) return;
    for (UINT32 i = 0; i < count; i++) out.push_back({ x + m[i].left, y + m[i].top, m[i].width, m[i].height });
}

void Canvas::BlitTo(HDC dst, int x, int y, int w, int h, int srcX, int srcY) const {
    if (!dc_) return;
    GdiFlush();
    BitBlt(dst, x, y, w, h, dc_, srcX, srcY, SRCCOPY);
}

void Canvas::Copy(const Canvas& src, int x, int y) {
    for (int yy = 0; yy < src.h_; yy++) {
        int dy = y + yy;
        if (dy < 0 || dy >= h_) continue;
        int x0 = max(x, 0), x1 = min(x + src.w_, w_);
        if (x1 <= x0) continue;
        memcpy(Row(dy) + (size_t)x0 * 4, src.Row(yy) + (size_t)(x0 - x) * 4, (size_t)(x1 - x0) * 4);
    }
}

}  // namespace gfx
