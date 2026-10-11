// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// Test support of the Qt front end (test build only): what the real-input test needs from the X server of the virtual
// display (Xvfb) - key presses through the XTest extension, the keyboard layout, a minimal window manager (the display
// has none, and without one a window never gets the keyboard focus or a state like "above"), and the window state
// property. It lives in a file of its own because the X11 headers define macros (KeyPress, None, Bool, ...) that
// are names in the Qt headers; nothing here includes Qt, and the header names no X type.

#pragma once

#include <string>
#include <vector>

namespace hcqt {
namespace testing {

class X11Env {
public:
    X11Env();
    ~X11Env();
    X11Env(const X11Env&) = delete;
    X11Env& operator=(const X11Env&) = delete;

    // Connects to $DISPLAY. False (and the reason) when there is no X server.
    bool open(std::string* why);
    // libXtst (XTest) was found: keys can be sent.
    bool hasXTest() const;

    // Becomes the window manager of the display: maps windows, gives the one that was mapped the keyboard focus, keeps
    // the _NET_WM_STATE property of a window up to date when a client asks for a change (keep on top), answers
    // _NET_ACTIVE_WINDOW. False when another window manager is running.
    bool startWindowManager();

    // Handles what has arrived (window manager requests, keyboard map changes). Call it often.
    void pump();
    void flush();

    // The key code of a key symbol ("Control_L", "q", ...), 0 when the layout has none.
    int keycodeForKeysym(const char* name) const;
    // The name of the key symbol of a key in the first group: level 0 is the key alone, 1 with Shift.
    std::string keysymAt(int keycode, int level) const;
    void key(int keycode, bool press);

    void focusWindow(unsigned long xid);
    // The names of the atoms in the _NET_WM_STATE property of a window.
    std::vector<std::string> netWmState(unsigned long xid) const;

private:
    struct Impl;
    Impl* d_;
};

}  // namespace testing
}  // namespace hcqt
