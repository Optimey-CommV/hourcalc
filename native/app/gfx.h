// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the drawing layer. One memory DIB per window (the DIB of a DirectWrite GDI
// interop bitmap render target), shapes through GDI+ on that DIB, all text and glyphs through
// DirectWrite. No Direct2D, no DXGI, no Direct3D, no WIC.

#pragma once

#include <windows.h>
#include <stdint.h>
#include <vector>

namespace Gdiplus { class Graphics; class Bitmap; }

namespace gfx {

typedef uint32_t Argb;   // 0xAARRGGBB, straight (not premultiplied) alpha

inline Argb Rgb(unsigned r, unsigned g, unsigned b) { return 0xFF000000u | (r << 16) | (g << 8) | b; }
inline Argb RgbA(unsigned a, unsigned r, unsigned g, unsigned b) { return (a << 24) | (r << 16) | (g << 8) | b; }
inline unsigned A(Argb c) { return c >> 24; }
inline unsigned R(Argb c) { return (c >> 16) & 255; }
inline unsigned G(Argb c) { return (c >> 8) & 255; }
inline unsigned B(Argb c) { return c & 255; }
inline Argb WithAlpha(Argb c, float alpha) { return (c & 0x00FFFFFFu) | ((unsigned)(alpha * 255.0f + 0.5f) << 24); }
inline COLORREF ToColorRef(Argb c) { return RGB(R(c), G(c), B(c)); }

// The colour of 'over' (with its own alpha and an extra opacity factor) composited on 'under'. The result is opaque.
Argb Over(Argb over, Argb under, float opacity = 1.0f);
// Linear blend of two colours (t = 0 gives a, t = 1 gives b), opaque.
Argb Mix(Argb a, Argb b, float t);

struct RectF {
    float x, y, w, h;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
    bool Contains(float px, float py) const { return px >= x && px < x + w && py >= y && py < y + h; }
};

enum Family { FamText, FamDisplay, FamIcons, FamMono, FamCount };

// A font: family, OpenType weight (100..900, 350 is Semilight) and the em size in device pixels.
struct Font {
    Family fam;
    int weight;
    float size;
};

bool Init();
void Shutdown();
// Tells the text renderer what the next text is drawn on (the window colour of the theme, the colour of a menu). Each text
// gets the rendering calibration of its polarity: dark text on a lighter ground (gamma 1.0, contrast 0.5), or lighter
// text on a darker ground (gamma 1.4, contrast 1.0), whatever the theme: white text on the accent key of the light
// theme is light on dark like all text of the dark theme.
void SetTextSurface(Argb surface);
// Whether a text of this colour is drawn with the calibration of lighter text on a darker ground (relative to the surface
// set with SetTextSurface).
bool IsLightOnDark(Argb text);

// Line height of one line of text in px (ascent + descent + line gap of the font that is really used).
float LineHeight(const Font& f);
// Baseline offset from the top of a line, in px.
float Baseline(const Font& f);
// Advance width of the text including trailing whitespace, in px (no wrapping).
float TextWidth(const wchar_t* s, int len, const Font& f);
// Height of the text wrapped to maxWidth (px).
float TextHeightWrapped(const wchar_t* s, int len, const Font& f, float maxWidth);
// x offsets of a caret before character 'index' (0..len) from the left edge of the text, in px.
float CaretX(const wchar_t* s, int len, const Font& f, int index);
// The character index nearest to x (px from the left edge of the text), 0..len.
int HitIndex(const wchar_t* s, int len, const Font& f, float x);
// The family name that is really used for a family (after the fallback was resolved).
const wchar_t* FamilyName(Family fam);

class Canvas;

// A wrapped block of text that is laid out once and drawn many times (the license viewer): hit testing and
// selection rectangles included.
class TextBlock {
public:
    TextBlock();
    ~TextBlock();
    bool Set(const wchar_t* s, int len, const Font& f, float maxWidth);
    float Height() const { return height_; }
    void Draw(Canvas& c, float x, float y, Argb color) const;
    int HitIndex(float x, float y) const;
    void Selection(int start, int end, float x, float y, std::vector<RectF>& out) const;
private:
    TextBlock(const TextBlock&);
    TextBlock& operator=(const TextBlock&);
    void* layout_;
    float height_;
};

class Canvas {
public:
    Canvas();
    ~Canvas();
    // Makes the canvas w x h pixels. Returns false when the bitmap could not be made (a size far beyond the
    // GDI limits, out of memory); the canvas is then empty (0 x 0, Valid() is false) and every drawing call
    // does nothing until a later Resize succeeds.
    bool Resize(int w, int h);
    bool Valid() const { return g_ != nullptr && dc_ != nullptr; }
    int W() const { return w_; }
    int H() const { return h_; }
    HDC Dc() const { return dc_; }

    void Clear(Argb c);
    // Pixel-exact rectangle (integer pixels), alpha blended when the colour is not opaque.
    void FillRect(int x, int y, int w, int h, Argb c);
    void FillRoundRect(const RectF& r, float radius, Argb c);
    void FillEllipse(const RectF& r, Argb c);
    // The 1 px border of a rounded rect: top colour down to 90 %, then a gradient to the bottom colour.
    void BorderGradient(const RectF& r, float radius, Argb top, Argb bottom, float thickness = 1.0f);
    // A 1 px border in one colour.
    void BorderSolid(const RectF& r, float radius, Argb c, float thickness = 1.0f);
    // A WPF Border with a background, a border of 'thickness' px and CornerRadius 'cornerRadius' (px): the border is
    // stroked along the centre line of that radius, so its outer edge has the radius cornerRadius + thickness / 2 and its
    // inner edge cornerRadius - thickness / 2, and the background lies inside the border. (A background under the border
    // with the outer radius shows at the anti-aliased corner pixels of the border: WPF does not do that.)
    // The border runs from 'top' to 'bottom' like BorderGradient, or is one colour 'top' when 'solid'.
    void BorderBox(const RectF& r, float cornerRadius, float thickness, Argb fill, Argb top, Argb bottom, bool solid = false);

    // Text with its line box starting at (x, y). maxWidth > 0 wraps at that width.
    void Text(const wchar_t* s, int len, const Font& f, float x, float y, Argb color, float maxWidth = 0.0f);

    // Copies the canvas to a device context.
    void BlitTo(HDC dc, int x, int y, int w, int h, int srcX = 0, int srcY = 0) const;
    // Row access for post-processing (top-down order, 32 bit BGRA).
    uint8_t* Row(int y) const;
    // Copies another canvas (same size or smaller) into this one at (x, y).
    void Copy(const Canvas& src, int x, int y);

    void Flush();
    Gdiplus::Graphics* Gp() { return g_; }

private:
    friend class TextBlock;
    void Release();
    void* rt_;              // IDWriteBitmapRenderTarget*
    HDC dc_;
    uint8_t* bits_;
    int stride_;
    bool topDown_;
    int w_, h_;
    Gdiplus::Bitmap* bmp_;
    Gdiplus::Graphics* g_;
};

}  // namespace gfx
