// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - fitting the values into their columns. A long value shrinks its font (a Pango scale
// on the 30 pt of the style) so it is never clipped; the two values side by side use one size (the smaller of the
// two that fit). The rows that hold them keep their height (min-height in the style), so a shrinking font never
// moves the keypad. The expression line shows the end of a long chain, with a leading ellipsis.

#include <algorithm>
#include <cmath>
#include <vector>

#include "hcwindow.h"
#include "util.h"

namespace hcgtk {

// The width of a display (the content of the page): what the display box was allocated last; before the first
// allocation, the width of the window less the margins.
double HcWindow::DisplayWidth(int page) const {
    if (displayW_[page] > 0) return displayW_[page];
    int w = gtk_widget_get_width(win_);
    if (w <= 0) gtk_window_get_default_size(GTK_WINDOW(win_), &w, nullptr);
    if (w <= 0) w = kDefaultWidth;
    return w - 2 * kPageMargin;
}

void HcWindow::OnDisplayAllocated(int page, int width) {
    if (!alive_) return;
    displayW_[page] = width;
    if (page == 0) FitConverter();
    else {
        FitCalculator();
        FitExpression();
    }
}

// Pango attributes of a value: the scale on the font size of the style, and tabular digits (so the width of a
// value does not jump with the digits). The same list is used to measure, so the fit is exact.
static PangoAttrList* NumericAttrs(double scale) {
    PangoAttrList* l = pango_attr_list_new();
    pango_attr_list_insert(l, pango_attr_scale_new(scale));
    pango_attr_list_insert(l, pango_attr_font_features_new("tnum"));
    return l;
}

// The width of a text at the font of a widget at scale 1.
double HcWindow::TextWidth(GtkWidget* w, const std::wstring& text) const {
    if (text.empty()) return 0;
    PangoLayout* l = gtk_widget_create_pango_layout(w, ToUtf8(text).c_str());
    PangoAttrList* attrs = NumericAttrs(1.0);
    pango_layout_set_attributes(l, attrs);
    pango_attr_list_unref(attrs);
    int pw = 0, ph = 0;
    pango_layout_get_pixel_size(l, &pw, &ph);
    g_object_unref(l);
    return pw;
}

// The scale one value needs on its own: the largest one (up to maxScale) at which its text fits.
double HcWindow::FitScale(GtkWidget* w, const std::wstring& text, double avail, double maxScale, double minScale) const {
    double scale = maxScale;
    if (!text.empty() && avail > 8.0) {
        double w1 = TextWidth(w, text);
        if (w1 * maxScale > avail) scale = std::max(minScale, avail / w1);
    }
    return scale;
}

void HcWindow::ApplyScale(GtkWidget* w, double scale, double* last) {
    if (std::fabs(*last - scale) < 0.001) return;
    *last = scale;
    PangoAttrList* l = NumericAttrs(scale);
    if (GTK_IS_TEXT(w)) gtk_text_set_attributes(GTK_TEXT(w), l);
    else gtk_label_set_attributes(GTK_LABEL(w), l);
    pango_attr_list_unref(l);
}

void HcWindow::FitAll() {
    FitConverter();
    FitCalculator();
    FitExpression();
}

void HcWindow::FitConverter() {
    if (!alive_) return;
    double colW = (DisplayWidth(0) - kColumnGap) / 2.0;
    double a = FitScale(text_[0], GetField(0).text, colW - 6.0, 1.0, kConvMinScale);
    double b = FitScale(text_[1], GetField(1).text, colW - 6.0, 1.0, kConvMinScale);
    convScale_ = std::min(a, b);
    ApplyScale(text_[0], convScale_, &textScale_[0]);
    ApplyScale(text_[1], convScale_, &textScale_[1]);
}

void HcWindow::FitCalculator() {
    if (!alive_) return;
    double dw = DisplayWidth(1);
    double colW = (dw - kColumnGap) / 2.0;
    double a = FitScale(calcVal_[0], calcL_, (error_ ? dw : colW) - 2.0, calcFontMax_, kCalcLeftMinScale);
    double b = error_ ? 1.0 : FitScale(calcVal_[1], calcR_, colW - 2.0, 1.0, kCalcRightMinScale);
    calcScale_ = std::min(a, b);
    ApplyScale(calcVal_[0], calcScale_, &calcValScale_[0]);
    ApplyScale(calcVal_[1], calcScale_, &calcValScale_[1]);
}

// The expression line shows the end of a long chain, with a leading ellipsis: whole tokens are dropped from
// the front until the rest fits.
void HcWindow::FitExpression() {
    if (!alive_) return;
    std::wstring show = exprFull_;
    double avail = DisplayWidth(1);
    if (avail > 8.0 && !show.empty() && TextWidth(exprLabel_, show) > avail) {
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
            if (TextWidth(exprLabel_, cand) <= avail || i == n - 1) {
                show = cand;
                break;
            }
        }
    }
    exprShown_ = show;
    gtk_label_set_text(GTK_LABEL(exprLabel_), ToUtf8(show).c_str());
}

}  // namespace hcgtk
