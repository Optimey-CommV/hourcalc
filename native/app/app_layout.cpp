// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - layout and text fitting. A port of the layout math in legacy\wpf\src\MainWindow.cs
// (base client 324 x 521 DIP, scale clamp 0.85 .. 2.0, key margins, header 48, fit / shrink rules,
// fixed row heights) with WPF's layout rounding (UseLayoutRounding: every size and offset is
// rounded to whole device pixels, halves to even).

#include "app.h"
#include "ui.h"

#include <algorithm>
#include <math.h>

namespace app {

using gfx::Font;
using ui::Rn;

static const float kBaseW = 324.0f, kBaseH = 521.0f, kMinScale = 0.85f, kMaxScale = 2.0f;
static const float kValueFontMax = 40.0f, kCaptionFont = 14.0f;

static std::wstring Sep() { return hc::timemath::DecimalSeparator(); }

// ------------------------------------------------------------------ keys

void MainWindow::BuildKeys() {
    convKeys_.clear();
    calcKeys_.clear();
    auto text = [](const wchar_t* key, const wchar_t* label, KeyStyle st, gfx::Family fam, int weight, float size, int row, int col, int span,
                   const wchar_t* name) {
        KeyDef k;
        k.key = key;
        k.label = label;
        k.glyph = 0;
        k.style = st;
        k.fam = fam;
        k.weight = weight;
        k.size = size;
        k.opGlyph = false;
        k.row = row;
        k.col = col;
        k.span = span;
        k.name = name;
        return k;
    };
    auto glyph = [](const wchar_t* key, wchar_t g, KeyStyle st, float size, bool op, int row, int col, const wchar_t* name) {
        KeyDef k;
        k.key = key;
        k.glyph = g;
        k.style = st;
        k.fam = gfx::FamText;
        k.weight = 400;
        k.size = size;
        k.opGlyph = op;
        k.row = row;
        k.col = col;
        k.span = 1;
        k.name = name;
        return k;
    };
    const wchar_t* digits[9] = { L"7", L"8", L"9", L"4", L"5", L"6", L"1", L"2", L"3" };
    std::wstring sep = Sep();

    // Converter: 3 columns, 6 rows.
    convKeys_.push_back(text(L"RESET", L"Reset", KeyFunc, gfx::FamText, 400, 15, 0, 0, 1, L"Reset"));
    convKeys_.push_back(text(L"CE", L"CE", KeyFunc, gfx::FamText, 400, 15, 0, 1, 1, L"Clear entry"));
    convKeys_.push_back(glyph(L"BACK", ui::kGlyphBackspace, KeyFunc, 14, false, 0, 2, L"Backspace"));
    for (int i = 0; i < 9; i++)
        convKeys_.push_back(text(digits[i], digits[i], KeyDigit, gfx::FamDisplay, 350, 20, 1 + i / 3, i % 3, 1, L""));
    convKeys_.push_back(text(L":", L":", KeyDigit, gfx::FamDisplay, 350, 20, 4, 0, 1, L"Time separator"));
    convKeys_.push_back(text(L"0", L"0", KeyDigit, gfx::FamDisplay, 350, 20, 4, 1, 1, L""));
    convKeys_.push_back(text(L",", sep.c_str(), KeyDigit, gfx::FamDisplay, 350, 20, 4, 2, 1, L"Decimal separator"));
    convKeys_.push_back(text(L"CONVERT", L"Convert", KeyAccent, gfx::FamText, 400, 17, 5, 0, 3, L"Convert"));

    // Calculator: 4 columns, 5 rows.
    calcKeys_.push_back(text(L"CE", L"CE", KeyFunc, gfx::FamText, 400, 15, 0, 0, 1, L"Clear entry"));
    calcKeys_.push_back(text(L"C", L"C", KeyFunc, gfx::FamText, 400, 15, 0, 1, 1, L"Clear"));
    calcKeys_.push_back(glyph(L"BACK", ui::kGlyphBackspace, KeyFunc, 14, false, 0, 2, L"Backspace"));
    calcKeys_.push_back(glyph(L"/", ui::kGlyphDivide, KeyFunc, 14, true, 0, 3, L"Divide"));
    for (int i = 0; i < 9; i++)
        calcKeys_.push_back(text(digits[i], digits[i], KeyDigit, gfx::FamDisplay, 350, 20, 1 + i / 3, i % 3, 1, L""));
    const wchar_t* ops[3] = { L"*", L"-", L"+" };
    const wchar_t opGlyphs[3] = { ui::kGlyphMultiply, ui::kGlyphMinus, ui::kGlyphPlus };
    const wchar_t* opNames[3] = { L"Multiply", L"Subtract", L"Add" };
    for (int r = 0; r < 3; r++) calcKeys_.push_back(glyph(ops[r], opGlyphs[r], KeyFunc, 14, true, 1 + r, 3, opNames[r]));
    calcKeys_.push_back(text(L":", L":", KeyDigit, gfx::FamDisplay, 350, 20, 4, 0, 1, L"Time separator"));
    calcKeys_.push_back(text(L"0", L"0", KeyDigit, gfx::FamDisplay, 350, 20, 4, 1, 1, L""));
    calcKeys_.push_back(text(L",", sep.c_str(), KeyDigit, gfx::FamDisplay, 350, 20, 4, 2, 1, L"Decimal separator"));
    calcKeys_.push_back(glyph(L"=", ui::kGlyphEquals, KeyAccent, 14, false, 4, 3, L"Equals"));
    calcEnabled_.assign(calcKeys_.size(), true);
}

int MainWindow::KeyIndex(const std::wstring& key) const {
    const std::vector<KeyDef>& keys = tab_ == 0 ? convKeys_ : calcKeys_;
    for (size_t i = 0; i < keys.size(); i++)
        if (keys[i].key == key) return (int)i;
    return -1;
}

bool MainWindow::KeyEnabled(const std::wstring& key) const {
    if (tab_ == 0) {
        if (key == L":") return active_ == 0;
        if (key == L",") return active_ == 1;
        return true;
    }
    for (size_t i = 0; i < calcKeys_.size(); i++)
        if (calcKeys_[i].key == key) return calcEnabled_[i];
    return true;
}

// ------------------------------------------------------------------ layout

// The order in which the legacy WPF Grid resolves star definitions of equal weight: the result of
// .NET's old (non-introspective) QuickSort of n elements that all compare equal. The snapshots of
// the WPF build were rendered by a process that uses this "legacy" Grid (no TargetFramework
// attribute), so the native layout reproduces its star distribution.
void TieOrder(int n, std::vector<int>& idx) {
    idx.resize((size_t)n);
    for (int i = 0; i < n; i++) idx[(size_t)i] = i;
    struct Sorter {
        static void Sort(std::vector<int>& k, int left, int right) {
            do {
                int i = left, j = right;
                int mid = i + ((j - i) >> 1);
                int x = k[(size_t)mid];
                (void)x;
                // equal elements: the three SwapIfGreater calls do nothing, the partition swaps every pair
                for (;;) {
                    if (i > j) break;
                    if (i < j) std::swap(k[(size_t)i], k[(size_t)j]);
                    i++;
                    j--;
                    if (!(i <= j)) break;
                }
                if (j - left <= right - i) {
                    if (left < j) Sort(k, left, j);
                    left = i;
                } else {
                    if (i < right) Sort(k, i, right);
                    right = j;
                }
            } while (left < right);
        }
    };
    if (n > 1) Sorter::Sort(idx, 0, n - 1);
}

// Sizes (px, index order) of n star definitions of equal weight that share 'avail' px. Each one in
// turn takes round(remaining / remaining weight), so the rounding error is carried to the next.
void StarSizes(double avail, int n, std::vector<float>& sizes) {
    std::vector<int> order;
    TieOrder(n, order);
    sizes.assign((size_t)n, 0.0f);
    double taken = 0.0;
    for (int i = 0; i < n; i++) {
        double remaining = avail - taken;
        if (remaining < 0) remaining = 0;
        double s = nearbyint(remaining * (1.0 / (double)(n - i)));
        sizes[(size_t)order[(size_t)i]] = (float)s;
        taken += s;
    }
}

// Row / column boundaries of a star grid: 0, then the running sums of the sizes.
static void Boundaries(float total, int n, std::vector<float>& b) {
    std::vector<float> sizes;
    StarSizes((double)total, n, sizes);
    b.resize((size_t)n + 1);
    b[0] = 0.0f;
    for (int i = 0; i < n; i++) b[(size_t)i + 1] = b[(size_t)i] + sizes[(size_t)i];
}

void MainWindow::Relayout() {
    Layout& L = lay_;
    const float d = dpiScale_;
    L.d = d;
    L.W = clientW_;
    L.H = clientH_;
    const float Wd = clientW_ / d, Hd = clientH_ / d;
    float s = fminf(Wd / kBaseW, Hd / kBaseH);
    s = fmaxf(kMinScale, fminf(kMaxScale, s));
    // The WPF window follows its size only when the scale changes by 0.004 or more (UpdateScale): a client size in
    // device pixels at a fractional DPI is a fraction of a DIP off the base size, and the fonts must not shrink for it.
    if (fabsf(s - scaleApplied_) < 0.004f) s = scaleApplied_;
    scaleApplied_ = s;
    L.s = s;
    const float W = (float)clientW_, H = (float)clientH_;

    // ---- header: menu button, tabs, keep-on-top toggle
    float hh = Rn(48.0f * s * d);
    L.headerH = hh;
    float btn = Rn(40.0f * s * d);
    float btnY = Rn((hh - Rn(4.0f * d) - btn) / 2.0f);
    L.menuBtn = { Rn(2.0f * d), btnY, btn, btn };
    L.pin = { W - Rn(3.0f * d) - btn, btnY, btn, btn };
    Font fTab = ui::TextFont(16.0f * s * d, 600);
    const wchar_t* tabNames[2] = { L"Converter", L"Calculator" };
    float tabX = L.menuBtn.x + btn + Rn(2.0f * d);        // column 0 ends at the button's right edge; the stack panel has a 2 px margin
    float lhTab = Rn(gfx::LineHeight(fTab));
    float stackH = lhTab + Rn(3.0f * d) + Rn(3.0f * d);
    float stackTop = Rn((hh - stackH) / 2.0f);
    for (int i = 0; i < 2; i++) {
        float tw = Rn(gfx::TextWidth(tabNames[i], (int)wcslen(tabNames[i]), fTab));
        float tabW = tw + 2.0f * Rn(10.0f * d);
        L.tabTextW[i] = tw;
        L.tab[i] = { tabX, Rn(7.0f * d), tabW, hh - 2.0f * Rn(7.0f * d) };
        L.tabTextX[i] = tabX + Rn(10.0f * d);
        L.tabInd[i] = { tabX + Rn(10.0f * d), stackTop + lhTab + Rn(3.0f * d), tw, Rn(3.0f * d) };
        tabX += tabW + Rn(2.0f * d);
    }
    L.tabTextY = stackTop;
    Font fIcon = ui::IconFont(16.0f * s * d);
    float gh = Rn(gfx::LineHeight(fIcon));
    L.glyphY = btnY + Rn((btn - gh) / 2.0f);

    // ---- the display (shared geometry of the two views)
    const float vt = hh;
    float dx = Rn(14.0f * d);
    float dw = Rn(W - 28.0f * d);
    float gap = Rn(14.0f * d);
    std::vector<float> colSizes;
    StarSizes((double)(dw - gap), 2, colSizes);
    float leftW = colSizes[0], rightW = colSizes[1];
    float rightX = dx + leftW + gap;
    Font fLbl = ui::TextFont(12.0f * s * d);
    Font fVal = ui::DisplayFont(kValueFontMax * s * d);
    Font fCap = ui::TextFont(kCaptionFont * s * d);
    float lblH = Rn(gfx::LineHeight(fLbl));
    float valueRow = Rn(gfx::LineHeight(fVal));
    float capLH = Rn(gfx::LineHeight(fCap));
    L.valueRowH = valueRow;
    L.captionH = capLH;
    float inset = Rn(2.0f * d);

    // converter display: margin 14,6,14,6; rows label, value, underline, caption (margin top 4)
    {
        float top = vt + Rn(6.0f * d);
        float underH = Rn(2.0f * d);
        L.lbl[0] = { dx, top, leftW, lblH };
        L.lbl[1] = { rightX, top, rightW, lblH };
        float by = top + lblH;
        L.box[0] = { dx, by, leftW + inset, valueRow };
        L.box[1] = { rightX, by, rightW + inset, valueRow };
        L.under[0] = { dx, by + valueRow, leftW, underH };
        L.under[1] = { rightX, by + valueRow, rightW, underH };
        L.cap = { dx, by + valueRow + underH + Rn(4.0f * d), leftW, capLH };
        float displayH = Rn(6.0f * d) + lblH + valueRow + underH + Rn(4.0f * d) + capLH + Rn(6.0f * d);
        L.keypad = { 0, vt + displayH, W, H - vt - displayH };
    }
    // calculator display: margin 14,0,14,6; rows expression, labels, values, caption
    {
        float exprH = capLH;
        L.expr = { dx, vt, dw, exprH };
        float ly = vt + exprH;
        L.calcLbl[0] = { dx, ly, leftW, lblH };
        L.calcLbl[1] = { rightX, ly, rightW, lblH };
        float vy = ly + lblH;
        L.calcVal[0] = { dx, vy, error_ ? dw : leftW, valueRow };
        L.calcVal[1] = { rightX, vy, rightW, valueRow };
        L.calcCap = { dx, vy + valueRow, leftW, capLH };
        float displayH = exprH + lblH + valueRow + capLH + Rn(6.0f * d);
        L.calcDisplay = { dx, vt, dw, displayH - Rn(6.0f * d) };
        if (tab_ == 1) L.keypad = { 0, vt + displayH, W, H - vt - displayH };
    }

    // ---- keypad: margin 2.5,0,2.5,3; star rows and columns; key margin 1.5,1,1.5,1
    const std::vector<KeyDef>& keys = tab_ == 0 ? convKeys_ : calcKeys_;
    int rows = tab_ == 0 ? 6 : 5, cols = tab_ == 0 ? 3 : 4;
    float kx = Rn(2.5f * d), ky = L.keypad.y;
    float kw = Rn(W - 5.0f * d), kh = Rn(H - ky - 3.0f * d);
    std::vector<float> bx, by;
    Boundaries(kw, cols, bx);
    Boundaries(kh, rows, by);
    L.keys.resize(keys.size());
    for (size_t i = 0; i < keys.size(); i++) {
        const KeyDef& k = keys[i];
        float cx0 = bx[(size_t)k.col], cx1 = bx[(size_t)(k.col + k.span)];
        float cy0 = by[(size_t)k.row], cy1 = by[(size_t)(k.row + 1)];
        float rx = Rn(cx0 + 1.5f * d), rw = Rn(cx1 - cx0 - 3.0f * d);
        float ry = Rn(cy0 + d), rh = Rn(cy1 - cy0 - 2.0f * d);
        L.keys[i] = { kx + rx, ky + ry, rw, rh };
    }
    layoutValid_ = true;
    FitAll();
}

void MainWindow::LayoutAt(int clientW, int clientH, float dpiScale) {
    clientW_ = clientW;
    clientH_ = clientH;
    dpiScale_ = dpiScale;
    Relayout();
}

// ------------------------------------------------------------------ fitting

float MainWindow::MeasureValue(const std::wstring& s, float px) const {
    return gfx::TextWidth(s.c_str(), (int)s.size(), ui::DisplayFont(px));
}

// The font size one value needs on its own: the largest one at which its text fits.
float MainWindow::FitSize(int, float maxPx, float minPx, float padPx, float availW, const std::wstring& text, gfx::Family) {
    float size = maxPx;
    float avail = availW - padPx;
    if (!text.empty() && avail > 8.0f * lay_.d) {
        float w = MeasureValue(text, maxPx);
        if (w > avail) size = fmaxf(minPx, maxPx * avail / w);
    }
    return size;
}

void MainWindow::FitAll() {
    const float d = lay_.d, s = lay_.s;
    // converter pair: both values max 40, min 16, padding 6
    {
        float a = FitSize(0, kValueFontMax * s * d, 16.0f * s * d, 6.0f * d, lay_.box[0].w, fields_[0].text, gfx::FamDisplay);
        float b = FitSize(1, kValueFontMax * s * d, 16.0f * s * d, 6.0f * d, lay_.box[1].w, fields_[1].text, gfx::FamDisplay);
        convFont_ = fminf(a, b);
    }
    // calculator pair: the left value max 40 (26 for an error text), min 12; the right one max 40, min 16; padding 2
    {
        float a = FitSize(2, calcFontMax_ * s * d, 12.0f * s * d, 2.0f * d, lay_.calcVal[0].w, calcL_, gfx::FamDisplay);
        float b = FitSize(3, kValueFontMax * s * d, 16.0f * s * d, 2.0f * d, lay_.calcVal[1].w, calcR_, gfx::FamDisplay);
        calcFont_ = fminf(a, b);
    }
    FitExpression();
}

// The expression line shows the end of a long chain, with a leading ellipsis.
void MainWindow::FitExpression() {
    std::wstring show = exprFull_;
    float avail = lay_.calcDisplay.w;
    float size = kCaptionFont * lay_.s * lay_.d;
    Font f = ui::TextFont(size);
    if (avail > 8.0f * lay_.d && !show.empty() && gfx::TextWidth(show.c_str(), (int)show.size(), f) > avail) {
        std::vector<std::wstring> tokens;
        size_t start = 0;
        for (;;) {
            size_t p = show.find(L' ', start);
            if (p == std::wstring::npos) {
                tokens.push_back(show.substr(start));
                break;
            }
            tokens.push_back(show.substr(start, p - start));
            start = p + 1;
        }
        size_t n = tokens.size();
        for (size_t i = 1; i < n; i++) {
            std::wstring cand = L"\x2026 ";
            for (size_t j = i; j < n; j++) {
                if (j > i) cand += L' ';
                cand += tokens[j];
            }
            if (gfx::TextWidth(cand.c_str(), (int)cand.size(), f) <= avail || i == n - 1) {
                show = cand;
                break;
            }
        }
    }
    exprShown_ = show;
}

}  // namespace app
