// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the About window (legal notices, GPL section 0/5d) and the license viewer. Both are
// plain themed windows painted like the main window. The About window offers "Save source code..." (the
// archive the exe carries); with HC_NO_EMBEDDED_SOURCE it shows the address of the source code instead.

#pragma once

#include "gfx.h"
#include "theme.h"

#include <string>
#include <vector>

namespace dialogs {

using gfx::RectF;

class Dialog {
public:
    enum Kind { About, License };

    Dialog(Kind kind, const theme::Palette& pal, float dpiScale, HINSTANCE hInst);
    ~Dialog();

    // The colours of the window. A change of the theme reaches every dialog that exists (SetPaletteOfAll), shown or not.
    const theme::Palette& Palette() const { return pal_; }
    void SetPalette(const theme::Palette& pal);
    static void SetPaletteOfAll(const theme::Palette& pal);       // the list of live dialogs is a linked list through nextLive_

    // The window as the user sees it: modal over an owner. Returns when it is closed.
    void RunModal(HWND owner);
    // Makes the window (not shown, no message loop) at the size RunModal would use. The self test drives it with messages.
    bool CreateWindowFor(HWND owner);

    // Lays the content out at a client size (px) and paints it (the snapshot tool and the self test use these).
    // For the About window the height is the natural height of the content.
    void Layout(int clientW, int clientH);
    void Render(gfx::Canvas& c);
    int NaturalHeight() const { return naturalH_; }
    HWND hwnd() const { return hwnd_; }
    // Test access.
    const std::wstring& LicenseText() const { return licenseText_; }
    std::vector<std::wstring> ButtonTexts() const;
    void Click(int button);
#ifndef HC_NO_EMBEDDED_SOURCE
    bool SourceAvailable() const;
#endif
    void SetOnViewLicense(void (*fn)(Dialog&)) { onViewLicense_ = fn; }
    // The license viewer's scrolling, for the mouse wheel and for tests. lines: the system setting for one notch
    // (WHEEL_PAGESCROLL: a page). delta: WHEEL_DELTA (120) per notch, positive scrolls up.
    void WheelScroll(int delta, UINT lines);
    float Scroll() const { return scroll_; }
    float ViewHeight() const { return view_.h; }
    bool HasScrollBar() const { return hasBar_; }
    // How many times the license text was laid out (the costly part of a size change): a change of the height alone
    // needs none. For tests.
    int LayoutPasses() const { return licensePasses_; }

private:
    struct Button {
        std::wstring text;
        bool isDefault;
        RectF r;
    };
    void LayoutAbout(int w);
    void LayoutLicense(int w, int h);
    void SetBlock(bool withBar);
    void PaintButtons(gfx::Canvas& c);
    void PaintAbout(gfx::Canvas& c);
    void PaintLicense(gfx::Canvas& c);
    void Activate(int button);
    void ViewLicense();
#ifndef HC_NO_EMBEDDED_SOURCE
    void SaveSource();
#endif
    int ButtonAt(int x, int y) const;
    float BarWidth() const;
    float ScrollMax() const;
    void ScrollTo(float v);
    RectF ThumbRect() const;

    LRESULT Proc(UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK StaticProc(HWND, UINT, WPARAM, LPARAM);
    void Invalidate();

    Kind kind_;
    theme::Palette pal_;
    float d_;
    HINSTANCE hInst_;
    HWND hwnd_;
    HWND owner_;
    bool done_;
    std::vector<Button> buttons_;
    int hot_, pressed_, focus_;
    int W_, H_;
    int naturalH_;
    gfx::Canvas canvas_;
    gfx::Canvas viewCanvas_;

    // About
    float yName_, yVersion_, yCopy_, yNotice_, noticeH_, textX_, textW_;
    float ySource_ = 0;   // the "Source code: <address>" line of a build that does not carry its source (HC_NO_EMBEDDED_SOURCE)
    // License
    std::wstring licenseText_;
    gfx::TextBlock block_;
    RectF view_;
    float scroll_;
    int selA_, selB_;
    bool dragSel_, dragThumb_;
    float dragOffset_;
    bool hasBar_;
    float blockW_;
    void (*onViewLicense_)(Dialog&);
    // What the last layout of the text was for: the width of the view and the scale, the height of the text laid out
    // with and without room for the scroll bar (-1: not known), and which of the two the block holds now.
    float layViewW_ = -1.0f, layD_ = 0.0f, layFullH_ = -1.0f, layNarrowH_ = -1.0f;
    int blockBar_ = -1;
    size_t hardLines_ = 0;
    int licensePasses_ = 0;
    HICON iconBig_ = nullptr, iconSmall_ = nullptr;
    Dialog* nextLive_ = nullptr;
};

}  // namespace dialogs
