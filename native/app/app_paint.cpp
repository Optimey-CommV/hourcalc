// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - painting. The whole client area is drawn into one memory DIB: shapes through GDI+,
// text and glyphs through DirectWrite (gfx.h). The colours, states and rules are the ones of
// legacy\wpf\src\Styles.cs and Theme.cs.

#include "app.h"
#include "ui.h"

#include <math.h>

namespace app {

using gfx::Canvas;
using gfx::Font;
using gfx::Over;
using gfx::WithAlpha;
using ui::Rn;

// ------------------------------------------------------------------ keys

Argb MainWindow::KeyFill(const KeyDef& k, bool enabled, bool hot, bool pressed) const {
    const theme::Palette& p = pal_;
    if (!enabled) return k.style == KeyDigit ? p.digit : p.func;     // disabled: the digit keys keep their fill, the others use Func
    switch (k.style) {
    case KeyDigit:
        return (hot || pressed) ? p.digitHover : p.digit;
    case KeyFunc:
        return (hot || pressed) ? p.funcHover : p.func;
    case KeyAccent:
        if (pressed) return Over(WithAlpha(p.accent, 0.8f), p.bg);
        if (hot) return Over(WithAlpha(p.accent, 0.9f), p.bg);
        return p.accent;
    }
    return p.func;
}

void MainWindow::DrawKey(Canvas& c, const KeyDef& k, const RectF& r, bool enabled, bool hot, bool pressed) {
    const theme::Palette& p = pal_;
    const float d = lay_.d, s = lay_.s;
    Argb fill = KeyFill(k, enabled, hot, pressed);
    // A key is a Border with CornerRadius 4 and BorderThickness 1 (see gfx::Canvas::BorderBox for the geometry). The border
    // is drawn 1 DIP thick, so 1.5 px at 150 %, anti-aliased: WPF does not snap it to whole pixels (the layout does, which
    // is why the text box of a key uses a whole pixel border: KeyTextBox).
    const float bt = d;
    // The background is translucent for the hovered / pressed accent key (WPF: brush opacity), so it lies over the window colour.
    Argb paint = fill;
    if (k.style == KeyAccent && enabled && (hot || pressed)) paint = WithAlpha(p.accent, pressed ? 0.8f : 0.9f);
    if (k.style == KeyAccent && enabled)
        c.BorderBox(r, 4.0f * d, bt, paint, p.accent, p.accentEdgeBottom);
    else
        c.BorderBox(r, 4.0f * d, bt, paint, p.edgeTop, p.edgeBottom);

    // text colour
    Argb fg;
    if (!enabled) fg = p.textDisabled;
    else if (k.style == KeyAccent) fg = pressed ? Over(WithAlpha(p.accentText, 0.7f), fill) : p.accentText;
    else fg = pressed ? p.textSecondary : p.text;
    if (k.opGlyph) fg = Over(WithAlpha(fg, 0.8f), fill);

    Font f = k.glyph ? ui::IconFont(k.size * s * d) : Font{ k.fam, k.weight, k.size * s * d };
    wchar_t g[2] = { k.glyph, 0 };
    const wchar_t* str = k.glyph ? g : k.label.c_str();
    int len = k.glyph ? 1 : (int)k.label.size();
    RectF tb = KeyTextBox(k, r, d, s);
    c.Text(str, len, f, tb.x, tb.y, fg);
}

// Where the label or glyph of a key sits (the TextBlock's box). The content presenter is centred in the
// key's border: its offset is the border thickness plus half the free room, rounded as one value.
RectF KeyTextBox(const KeyDef& k, const RectF& r, float d, float s) {
    float bt = Rn(d);
    Font f = k.glyph ? ui::IconFont(k.size * s * d) : Font{ k.fam, k.weight, k.size * s * d };
    wchar_t g[2] = { k.glyph, 0 };
    const wchar_t* str = k.glyph ? g : k.label.c_str();
    int len = k.glyph ? 1 : (int)k.label.size();
    float tw = Rn(gfx::TextWidth(str, len, f));
    float lh = Rn(gfx::LineHeight(f));
    float ox = Rn(bt + (r.w - 2 * bt - tw) / 2.0f);
    float oy = Rn(bt + (r.h - 2 * bt - lh) / 2.0f);
    return { r.x + ox, r.y + oy, tw, lh };
}

Argb MainWindow::KeyForeground(int index, bool pressed) const {
    const KeyDef& k = KeyAt(index);
    bool enabled = KeyEnabled(k.key);
    if (!enabled) return pal_.textDisabled;
    if (k.style == KeyAccent) return pressed ? WithAlpha(pal_.accentText, 0.7f) : pal_.accentText;
    return pressed ? pal_.textSecondary : pal_.text;
}

void MainWindow::PaintKeys(Canvas& c) {
    const std::vector<KeyDef>& keys = tab_ == 0 ? convKeys_ : calcKeys_;
    for (size_t i = 0; i < keys.size(); i++) {
        bool enabled = KeyEnabled(keys[i].key);
        bool hot = hover_.kind == HitKey && hover_.index == (int)i && enabled;
        bool pressed = pressed_.kind == HitKey && pressed_.index == (int)i && hot;
        DrawKey(c, keys[i], lay_.keys[i], enabled, hot, pressed);
    }
}

// ------------------------------------------------------------------ header

void MainWindow::PaintHeader(Canvas& c) {
    const theme::Palette& p = pal_;
    const float d = lay_.d, s = lay_.s;
    // icon buttons
    struct Btn { const RectF* r; HitKind kind; wchar_t glyph; } btns[2] = {
        { &lay_.menuBtn, HitMenu, ui::kGlyphMenu },
        { &lay_.pin, HitPin, topmost_ ? ui::kGlyphUnpin : ui::kGlyphPin },
    };
    Font fIcon = ui::IconFont(16.0f * s * d);
    for (auto& b : btns) {
        bool hot = hover_.kind == b.kind;
        bool down = pressed_.kind == b.kind && hot;
        if (down) c.FillRoundRect(*b.r, 4.0f * d, p.subtlePressed);
        else if (hot) c.FillRoundRect(*b.r, 4.0f * d, p.subtle);
        wchar_t g[2] = { b.glyph, 0 };
        float gw = Rn(gfx::TextWidth(g, 1, fIcon));
        float x = b.r->x + Rn((b.r->w - gw) / 2.0f);
        c.Text(g, 1, fIcon, x, lay_.glyphY, p.text);
    }
    // tabs
    Font fTab = ui::TextFont(16.0f * s * d, 600);
    const wchar_t* names[2] = { L"Converter", L"Calculator" };
    for (int i = 0; i < 2; i++) {
        bool selected = tab_ == i;
        bool hot = hover_.kind == (i == 0 ? HitTabConv : HitTabCalc);
        if (hot) c.FillRoundRect(lay_.tab[i], 4.0f * d, p.subtle);
        Argb fg = (selected || hot) ? p.text : p.textSecondary;
        c.Text(names[i], (int)wcslen(names[i]), fTab, lay_.tabTextX[i], lay_.tabTextY, fg);
        if (selected) c.FillRoundRect(lay_.tabInd[i], 1.5f * d, p.accent);
    }
}

// ------------------------------------------------------------------ converter

float MainWindow::FieldTextRight(int f) const {
    return lay_.box[f].right() - Rn(2.0f * lay_.d);
}

float MainWindow::FieldTextWidth(int f) const {
    const std::wstring& t = fields_[f].text;
    return gfx::TextWidth(t.c_str(), (int)t.size(), ui::DisplayFont(convFont_));
}

void MainWindow::PaintField(Canvas& c, int f) {
    const Field& fld = fields_[f];
    const float d = lay_.d;
    Font font = ui::DisplayFont(convFont_);
    float lh = gfx::LineHeight(font);
    const RectF& box = lay_.box[f];
    // The text view is not a layout element: its line sits at the exact centre, only the baseline is snapped to a pixel.
    float lineTop = box.y + (box.h - lh) / 2.0f;
    float tw = FieldTextWidth(f);
    float right = FieldTextRight(f);
    float x0 = right - tw;
    bool focused = windowActive_ && focus_ == f;
    int n = (int)fld.text.size();
    // selection highlight: the accent at 40 % under the text
    if (focused && fld.selLength() > 0) {
        float a = x0 + gfx::CaretX(fld.text.c_str(), n, font, fld.selStart());
        float b = x0 + gfx::CaretX(fld.text.c_str(), n, font, fld.selEnd());
        c.FillRect((int)Rn(a), (int)lineTop, (int)(Rn(b) - Rn(a)), (int)Rn(lh), WithAlpha(pal_.accent, 0.4f));
    }
    c.Text(fld.text.c_str(), n, font, x0, lineTop, pal_.text);
    if (focused && caretOn_ && fld.selLength() == 0) {
        float cx = x0 + gfx::CaretX(fld.text.c_str(), n, font, fld.caret);
        int cw = (int)fmaxf(1.0f, Rn(d));
        c.FillRect((int)Rn(cx), (int)lineTop, cw, (int)Rn(lh), pal_.text);
    }
}

void MainWindow::PaintConverter(Canvas& c) {
    const float d = lay_.d, s = lay_.s;
    Font fLbl = ui::TextFont(12.0f * s * d);
    const wchar_t* l0 = L"Hours : minutes";
    const wchar_t* l1 = L"Decimal hours";
    c.Text(l0, (int)wcslen(l0), fLbl, lay_.lbl[0].x, lay_.lbl[0].y, pal_.textSecondary);
    c.Text(l1, (int)wcslen(l1), fLbl, lay_.lbl[1].x, lay_.lbl[1].y, pal_.textSecondary);
    for (int f = 0; f < 2; f++) {
        PaintField(c, f);
        if (active_ == f) c.FillRoundRect(lay_.under[f], 1.0f * d, pal_.accent);
    }
    Font fCap = ui::TextFont(14.0f * s * d);
    // The text sits at the exact right edge minus its width: WPF does not snap the text origin (the width of the text
    // box is rounded, the glyphs are not).
    float cw = gfx::TextWidth(caption_.c_str(), (int)caption_.size(), fCap);
    c.Text(caption_.c_str(), (int)caption_.size(), fCap, lay_.cap.right() - cw, lay_.cap.y, pal_.textSecondary);
}

// ------------------------------------------------------------------ calculator

void MainWindow::PaintCalculator(Canvas& c) {
    const float d = lay_.d, s = lay_.s;
    Font fCap = ui::TextFont(14.0f * s * d);
    Font fLbl = ui::TextFont(12.0f * s * d);
    float ew = gfx::TextWidth(exprShown_.c_str(), (int)exprShown_.size(), fCap);
    c.Text(exprShown_.c_str(), (int)exprShown_.size(), fCap, lay_.expr.right() - ew, lay_.expr.y, pal_.textSecondary);
    const wchar_t* l0 = L"Hours : minutes";
    const wchar_t* l1 = L"Decimal hours";
    c.Text(l0, (int)wcslen(l0), fLbl, lay_.calcLbl[0].x, lay_.calcLbl[0].y, pal_.textSecondary);
    if (!error_) c.Text(l1, (int)wcslen(l1), fLbl, lay_.calcLbl[1].x, lay_.calcLbl[1].y, pal_.textSecondary);
    Font fVal = ui::DisplayFont(calcFont_);
    float lw = gfx::TextWidth(calcL_.c_str(), (int)calcL_.size(), fVal);
    c.Text(calcL_.c_str(), (int)calcL_.size(), fVal, lay_.calcVal[0].right() - lw, lay_.calcVal[0].y, pal_.text);
    if (!error_) {
        float rw = gfx::TextWidth(calcR_.c_str(), (int)calcR_.size(), fVal);
        c.Text(calcR_.c_str(), (int)calcR_.size(), fVal, lay_.calcVal[1].right() - rw, lay_.calcVal[1].y, pal_.text);
    }
    float cw = gfx::TextWidth(calcCap_.c_str(), (int)calcCap_.size(), fCap);
    c.Text(calcCap_.c_str(), (int)calcCap_.size(), fCap, lay_.calcCap.right() - cw, lay_.calcCap.y, pal_.textSecondary);
}

// ------------------------------------------------------------------ the whole window

void MainWindow::Render(Canvas& c) {
    if (!layoutValid_) Relayout();
    if (!c.Resize(clientW_, clientH_)) return;       // no bitmap to paint on: skip this paint
    gfx::SetTextSurface(pal_.bg);
    c.Clear(pal_.bg);
    PaintHeader(c);
    if (tab_ == 0) PaintConverter(c);
    else PaintCalculator(c);
    PaintKeys(c);
}

}  // namespace app
