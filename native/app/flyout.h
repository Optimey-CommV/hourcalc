// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - the popups: the Advanced menu, the Copy menu of the calculator values and the tool
// tips. Each is an own layered window painted like the main window (rounded box, soft drop
// shadow, themed colours). The window never takes the focus.

#pragma once

#include "gfx.h"
#include "theme.h"

#include <string>
#include <vector>

namespace flyout {

using gfx::RectF;

// Messages the popup sends to its owner (the main window).
const UINT WM_FLYOUT_COMMAND = WM_APP + 21;      // wParam = item id
const UINT WM_FLYOUT_CLOSED = WM_APP + 22;       // wParam = the popup's kind

struct Item {
    enum Type { Header, Label, Radio, Button, Separator };
    Type type;
    std::wstring text;
    int id;
    bool checked;
    // layout (px, relative to the popup box)
    RectF rect;
    bool disabled = false;      // greyed out: no hover, no click
    int group = 0;              // radio items: the items of one group exclude each other (see SetChecked)
};

class Flyout {
public:
    enum Kind { Menu, Copy, Tip };

    explicit Flyout(Kind kind);
    ~Flyout();

    Kind kind() const { return kind_; }
    HWND hwnd() const { return hwnd_; }
    bool IsOpen() const { return open_; }

    // Content. Menu and Copy take items, a Tip takes text.
    void SetItems(const std::vector<Item>& items);
    const std::vector<Item>& Items() const { return items_; }
    const std::wstring& Text() const { return tip_; }
    void SetText(const wchar_t* text);
    // Checks the radio item with this id and unchecks the others of its group (a menu can hold several groups). An id that is
    // not a radio item changes nothing.
    void SetChecked(int id);
    void SetEnabled(int id, bool enabled);
    // Changes the colours. A popup that is open is painted again at once.
    void SetPalette(const theme::Palette& p);
    const theme::Palette& Palette() const { return pal_; }
    // Computes the size for a device scale (dpi / 96). Returns the size of the box, without the shadow room.
    void Layout(HWND owner, float scale);
    int BoxWidth() const { return boxW_; }
    int BoxHeight() const { return boxH_; }
    // The room around the box that the shadow needs (px); 0 for a tip.
    int ShadowRoom() const { return room_; }
    // The id of the enabled item at a point of the popup window (client coordinates), -1 when there is none. For tests.
    int ItemIdAt(int x, int y) const { int i = ItemAt(x, y); return i < 0 ? -1 : items_[(size_t)i].id; }
    // How many times the outline and the soft shadow were computed (the expensive part of showing a popup): Layout
    // with an unchanged size and scale does not make it necessary again. For tests.
    int ShapeBuilds() const { return shapeBuilds_; }

    // Where a box of w x h px that is meant to open with its top-left corner at (x, y) really goes so that it stays inside a work
    // area: shifted left and up when it would leave on the right or at the bottom (and back in when that pushes it out on the
    // left or at the top, for a box that is larger than the work area).
    static POINT Fit(int x, int y, int w, int h, const RECT& work);

    // Shows the popup with the top-left corner of its box at a screen position (moved with Fit to stay inside the work area of
    // the monitor it is on), without activating anything.
    void Open(HWND owner, int screenX, int screenY);
    void Close();
    // The hot item (under the mouse) and its pressed state, for the snapshot tool.
    void SetHot(int id, bool pressed);

    // Draws the popup over a canvas with the box's top-left at (boxX, boxY) (the snapshot tool).
    void ComposeOver(gfx::Canvas& dst, int boxX, int boxY);

#ifdef HC_TESTHOOKS
    // Test build only: marks the popup as open without showing it and without taking the mouse capture, so that the self test can
    // send its window synthetic clicks and see what an open popup does when the theme changes, without anything on the screen. Layout
    // must have been called. Presents() counts how often the popup was painted to its window (a closed popup is not).
    void OpenHiddenForTest(HWND owner, int screenX, int screenY);
    int Presents() const { return presents_; }
#endif

private:
    void Paint(gfx::Canvas& c);
    void BuildPixels();
    void Present(int screenX, int screenY);
    int ItemAt(int x, int y, bool disabledToo = false) const;
    bool BoxContains(int x, int y) const;
    void ForwardClick(UINT msg, int x, int y);
    void Track(int x, int y);
    LRESULT Proc(UINT msg, WPARAM wp, LPARAM lp);
    static LRESULT CALLBACK StaticProc(HWND, UINT, WPARAM, LPARAM);
    void Ensure(HWND owner);

    Kind kind_;
    HWND hwnd_;
    HWND owner_;
    bool open_;
    bool dirty_;
    std::vector<Item> items_;
    std::wstring tip_;
    theme::Palette pal_;
    float scale_;
    int boxW_, boxH_, room_, winW_, winH_;
    int hot_, pressed_;       // item index or -1
    bool tracking_;
    int posX_, posY_;
    gfx::Canvas canvas_;
    std::vector<uint32_t> pixels_;     // premultiplied BGRA, winW_ x winH_
    std::vector<uint8_t> mask_;        // coverage of the box, winW_ x winH_
    std::vector<uint8_t> shadow_;      // shadow alpha (0..255), winW_ x winH_
    bool shapeDirty_;
    int shapeBuilds_ = 0;
#ifdef HC_TESTHOOKS
    int presents_ = 0;
#endif
    HBITMAP dib_;
    uint32_t* dibBits_;
    HDC dibDc_;
};

}  // namespace flyout
