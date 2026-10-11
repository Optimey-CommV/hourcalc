// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - the self test with real input: key presses and mouse clicks that go through the X
// server (the XTest extension of the virtual display) into the window, the way a user's input does. It checks the
// whole path the other tests call into directly: X event -> GDK -> the window's key controller / the GtkText /
// the click gestures -> the behaviour. libXtst is loaded at run time (dlopen); where it is not installed the group
// reports that it did not run.

#ifdef HC_TEST_X11
#include <dlfcn.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "testutil.h"

// The X11 headers define macros (KeyPress, Window, True, ...) that are names of the program: they come last.
#include <gdk/x11/gdkx.h>
#include <X11/XKBlib.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

// gdk_x11_surface_get_xid() and gdk_x11_display_get_xdisplay() are deprecated in GTK 4.18 (no replacement for a test)
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"

namespace hctest {

namespace {

using hcgtk::FromUtf8;
using hcgtk::ToUtf8;

typedef int (*FakeKeyFn)(Display*, unsigned int, int, unsigned long);
typedef int (*FakeButtonFn)(Display*, unsigned int, int, unsigned long);
typedef int (*FakeMotionFn)(Display*, int, int, int, unsigned long);

struct XTest {
    void* lib = nullptr;
    FakeKeyFn key = nullptr;
    FakeButtonFn button = nullptr;
    FakeMotionFn motion = nullptr;
    bool Load() {
        lib = dlopen("libXtst.so.6", RTLD_NOW);
        if (!lib) return false;
        key = reinterpret_cast<FakeKeyFn>(dlsym(lib, "XTestFakeKeyEvent"));
        button = reinterpret_cast<FakeButtonFn>(dlsym(lib, "XTestFakeButtonEvent"));
        motion = reinterpret_cast<FakeMotionFn>(dlsym(lib, "XTestFakeMotionEvent"));
        return key && button && motion;
    }
};

class Input {
public:
    Input(Display* d, const XTest& x) : d_(d), x_(x) {}

    // Lets the window process what was sent.
    void Wait(int ms = 40) {
        XFlush(d_);
        WaitUntil([] { return false; }, ms);
    }

    // One key with the modifiers it needs (Shift when the keysym is on the second level of the key).
    void Key(KeySym ks, bool ctrl = false) {
        KeyCode kc = XKeysymToKeycode(d_, ks);
        if (!kc) return;
        bool shift = XkbKeycodeToKeysym(d_, kc, 0, 0) != ks && XkbKeycodeToKeysym(d_, kc, 0, 1) == ks;
        KeyCode ctrlKc = XKeysymToKeycode(d_, XK_Control_L), shiftKc = XKeysymToKeycode(d_, XK_Shift_L);
        if (ctrl) x_.key(d_, ctrlKc, True, 0);
        if (shift) x_.key(d_, shiftKc, True, 0);
        x_.key(d_, kc, True, 0);
        x_.key(d_, kc, False, 0);
        if (shift) x_.key(d_, shiftKc, False, 0);
        if (ctrl) x_.key(d_, ctrlKc, False, 0);
        Wait();
    }

    // A key by its key code, the physical key whatever the layout makes of it (the number row of an AZERTY keyboard).
    void KeyCodeTap(unsigned kc, bool ctrl = false, bool shift = false) {
        KeyCode ctrlKc = XKeysymToKeycode(d_, XK_Control_L), shiftKc = XKeysymToKeycode(d_, XK_Shift_L);
        if (ctrl) x_.key(d_, ctrlKc, True, 0);
        if (shift) x_.key(d_, shiftKc, True, 0);
        x_.key(d_, kc, True, 0);
        x_.key(d_, kc, False, 0);
        if (shift) x_.key(d_, shiftKc, False, 0);
        if (ctrl) x_.key(d_, ctrlKc, False, 0);
        Wait();
    }

    // Text as typed on a US keyboard.
    void Type(const char* text) {
        for (const char* p = text; *p; ++p) Key(static_cast<KeySym>(static_cast<unsigned char>(*p)));
    }

    // A click at the centre of a widget (button 1 or 3). The position is the one of the widget in the window,
    // moved to the surface and to the root window of the display.
    void Click(GtkWidget* window, GtkWidget* widget, unsigned button = 1, int times = 1) {
        graphene_rect_t r;
        if (!gtk_widget_compute_bounds(widget, window, &r)) return;
        double sx = 0, sy = 0;
        gtk_native_get_surface_transform(GTK_NATIVE(window), &sx, &sy);
        GdkSurface* s = gtk_native_get_surface(GTK_NATIVE(window));
        ::Window xid = gdk_x11_surface_get_xid(s);
        int rx = 0, ry = 0;
        ::Window child = 0;
        XTranslateCoordinates(d_, xid, DefaultRootWindow(d_), (int)std::lround(r.origin.x + r.size.width / 2 + sx), (int)std::lround(r.origin.y + r.size.height / 2 + sy),
                              &rx, &ry, &child);
        x_.motion(d_, -1, rx, ry, 0);
        Wait(30);
        for (int i = 0; i < times; i++) {
            x_.button(d_, button, True, 0);
            x_.button(d_, button, False, 0);
            Wait(30);
        }
        Wait(60);
    }

private:
    Display* d_;
    XTest x_;
};

}  // namespace

bool RealInputTests(const std::function<void(const std::wstring&, bool)>& check) {
    GdkDisplay* gd = gdk_display_get_default();
    if (!GDK_IS_X11_DISPLAY(gd)) return false;
    XTest xt;
    if (!xt.Load()) return false;
    Display* dpy = gdk_x11_display_get_xdisplay(gd);
    Input in(dpy, xt);
    std::wstring sep = hc::timemath::DecimalSeparator();
    auto until = [&](const std::function<bool()>& f) {
        WaitUntil(f, 1500);
        return f();
    };

    Win wp(true);
    HcWindow& w = *wp;
    GtkWidget* win = GTK_WIDGET(w.Window());
    check(L"real input: the window is the active window", until([&] { return gtk_window_is_active(w.Window()) != FALSE; }));
    check(L"real input: the left field has the keyboard focus", w.FocusField() == 0);

    // ---- the keyboard in the converter (the GtkText gets the characters)
    in.Type("830");
    check(L"real input: typed 830 -> left", until([&] { return w.ConverterText(0) == L"830"; }));
    check(L"real input: ... the right value follows", w.ConverterText(1) == L"8" + sep + L"50");
    in.Key(XK_Return);
    check(L"real input: Enter converts (left reads 08:30)", until([&] { return w.ConverterText(0) == L"08:30"; }));
    in.Key(XK_Escape);
    check(L"real input: Esc resets both fields", until([&] { return w.ConverterText(0).empty() && w.ConverterText(1).empty(); }));
    in.Type("8u30");
    check(L"real input: 8u30 is typed as 8:30", until([&] { return w.ConverterText(0) == L"8:30"; }));
    in.Key(XK_Tab);
    check(L"real input: Tab moves to the right field", until([&] { return w.FocusField() == 1; }));
    check(L"real input: ... and selects its value", w.GetField(1).selLength() == (int)w.ConverterText(1).size());
    in.Type("9");
    check(L"real input: a digit replaces the selected value", until([&] { return w.ConverterText(1) == L"9"; }));
    check(L"real input: ... and the left follows", w.ConverterText(0) == L"09:00");
    in.Key(XK_Escape);
    until([&] { return w.ConverterText(0).empty(); });

    // ---- the mouse on the fields and the keypad
    in.Click(win, w.FieldWidget(1));
    check(L"real input: a click on the right field focuses it", until([&] { return w.FocusField() == 1; }));
    in.Type("8,5");
    check(L"real input: 8,5 typed in the right field", until([&] { return w.ConverterText(1) == L"8" + sep + L"5"; }));
    check(L"real input: ... the left shows 08:30", w.ConverterText(0) == L"08:30");
    in.Click(win, w.FieldWidget(0));
    check(L"real input: a click on the left field selects all of it", until([&] { return w.FocusField() == 0 && w.GetField(0).selLength() == 5; }));
    in.Type("7");
    check(L"real input: ... so a digit replaces it", until([&] { return w.ConverterText(0) == L"7"; }));
    in.Click(win, w.KeyAt(w.KeyIndex(L"3")).button);
    check(L"real input: a click on the 3 key types a 3", until([&] { return w.ConverterText(0) == L"73"; }));
    in.Click(win, w.KeyAt(w.KeyIndex(L"BACK")).button);
    check(L"real input: a click on the backspace key deletes it", until([&] { return w.ConverterText(0) == L"7"; }));
    in.Click(win, w.KeyAt(w.KeyIndex(L"CONVERT")).button);
    check(L"real input: a click on Convert converts", until([&] { return w.ConverterText(0) == L"07:00"; }));
    check(L"real input: ... and the keypad did not take the focus", w.FocusField() == 0);
    in.Click(win, w.KeyAt(w.KeyIndex(L"RESET")).button);
    check(L"real input: a click on Reset clears", until([&] { return w.ConverterText(0).empty(); }));
    in.Type("12");
    in.Click(win, w.FieldWidget(0), 1, 2);
    check(L"real input: a double click selects all", until([&] { return w.GetField(0).selLength() == 2; }));

    // ---- the context menu of a field
    in.Click(win, w.FieldWidget(0), 3);
    check(L"real input: a right click opens the field menu", until([&] { return w.Overlay(); }));
    in.Key(XK_Escape);
    check(L"real input: Esc closes the menu", until([&] { return !w.Overlay(); }));
    check(L"real input: ... and does not reset the field behind it", w.ConverterText(0) == L"12");
    in.Key(XK_Escape);

    // ---- the primary menu and the tabs
    in.Key(XK_F10);
    check(L"real input: F10 opens the primary menu", until([&] { return w.IsMenuOpen(); }));
    in.Key(XK_Escape);
    check(L"real input: Esc closes it", until([&] { return !w.IsMenuOpen(); }));
    // the caret and the selection of the field stay as they were
    in.Type("830");
    until([&] { return w.ConverterText(0) == L"830"; });
    in.Key(XK_Left);
    in.Key(XK_Left);
    int caretBefore = w.GetField(0).caret;
    in.Key(XK_F10);
    until([&] { return w.IsMenuOpen(); });
    in.Key(XK_Escape);
    until([&] { return !w.IsMenuOpen() && w.FocusField() == 0; });
    check(L"real input: F10 and Esc leave the caret of the field where it was", w.GetField(0).caret == caretBefore && w.GetField(0).selLength() == 0);
    w.SetCaret(0, 2, 0);
    in.Key(XK_F10);
    until([&] { return w.IsMenuOpen(); });
    in.Key(XK_Escape);
    until([&] { return !w.IsMenuOpen() && w.FocusField() == 0; });
    check(L"real input: ... and its selection", w.GetField(0).selLength() == 2);
    in.Click(win, w.MenuButton());
    until([&] { return w.IsMenuOpen(); });
    in.Key(XK_Escape);
    until([&] { return !w.IsMenuOpen() && w.FocusField() == 0; });
    check(L"real input: a click on the menu button and Esc leave the selection as well", w.GetField(0).selLength() == 2);
    in.Key(XK_Escape);
    until([&] { return w.ConverterText(0).empty(); });
    in.Type("12");
    in.Key(XK_2, true);
    check(L"real input: Ctrl+2 selects the calculator", until([&] { return w.CurrentTab() == 1; }));
    in.Type("7:30+7:20");
    check(L"real input: the calculator takes typed text", until([&] { return w.Engine().Expression() == L"07:30 +" && w.CalcMain() == L"7:20"; }));
    in.Key(XK_Return);
    check(L"real input: Enter is equals", until([&] { return w.CalcMain() == L"14:50"; }));
    check(L"real input: ... right value", w.CalcDecimal() == L"14" + sep + L"83");
    in.Key(XK_Escape);
    check(L"real input: Esc clears the calculator", until([&] { return w.CalcMain() == L"00:00"; }));
    in.Click(win, w.KeyAt(w.KeyIndex(L"9")).button);
    in.Click(win, w.KeyAt(w.KeyIndex(L"+")).button);
    in.Click(win, w.KeyAt(w.KeyIndex(L"1")).button);
    in.Click(win, w.KeyAt(w.KeyIndex(L"=")).button);
    check(L"real input: clicks 9 + 1 = give 10:00", until([&] { return w.CalcMain() == L"10:00"; }));
    in.Key(XK_Delete);
    check(L"real input: Delete is CE", until([&] { return w.CalcMain() == L"00:00"; }));
    in.Type("7.5+1");
    in.Key(XK_KP_Enter);
    check(L"real input: the number block Enter is equals (7.5 + 1)", until([&] { return w.CalcMain() == L"08:30"; }));
    in.Key(XK_c, true);
    in.Key(XK_1, true);
    check(L"real input: Ctrl+1 selects the converter", until([&] { return w.CurrentTab() == 0; }));
    in.Key(XK_Tab, true);
    check(L"real input: Ctrl+Tab selects the calculator", until([&] { return w.CurrentTab() == 1; }));
    in.Key(XK_Tab, true);
    check(L"real input: ... and back", until([&] { return w.CurrentTab() == 0; }));

    // ---- a Belgian AZERTY keyboard: & and e-acute are the keys 10 and 11, the digits are one level up. Ctrl + the key
    // alone is Ctrl+1 / Ctrl+2 as on Windows (the shortcut is the key, not the character).
    if (std::system("setxkbmap be >/dev/null 2>&1") == 0) {
        WaitUntil([] { return false; }, 500);          // GDK learns of the new keyboard map from the server
        KeySym sym10 = XkbKeycodeToKeysym(dpy, 10, 0, 0), sym10s = XkbKeycodeToKeysym(dpy, 10, 0, 1);
        if (sym10 == XK_ampersand && sym10s == XK_1) {
            w.SelectTab(0);
            until([&] { return w.CurrentTab() == 0; });
            in.KeyCodeTap(11, true);
            check(L"real input, AZERTY: Ctrl + the key of 2 (unshifted) selects the calculator", until([&] { return w.CurrentTab() == 1; }));
            in.KeyCodeTap(10, true);
            check(L"real input, AZERTY: Ctrl + the key of 1 (unshifted) selects the converter", until([&] { return w.CurrentTab() == 0; }));
            in.KeyCodeTap(11, true, true);
            check(L"real input, AZERTY: Ctrl+Shift + the key of 2 selects the calculator too", until([&] { return w.CurrentTab() == 1; }));
            in.KeyCodeTap(10, true, true);
            check(L"real input, AZERTY: Ctrl+Shift + the key of 1 selects the converter too", until([&] { return w.CurrentTab() == 0; }));
            std::wstring before = w.ConverterText(0);
            in.KeyCodeTap(10, true);
            check(L"real input, AZERTY: Ctrl + the key types nothing into the field", w.ConverterText(0) == before);
            in.Key(XK_Escape);
            until([&] { return w.ConverterText(0).empty(); });
            in.KeyCodeTap(10, false, true);
            in.KeyCodeTap(11, false, true);
            check(L"real input, AZERTY: Shift + the keys types 1 and 2", until([&] { return w.ConverterText(0) == L"12"; }));
        } else {
            std::printf("   (the Belgian layout could not be set here: the AZERTY checks did not run)\n");
        }
        const int restored = std::system("setxkbmap us >/dev/null 2>&1");
        if (restored != 0) std::printf("   (could not set the US layout again)\n");
        WaitUntil([] { return false; }, 300);
    } else {
        std::printf("   (setxkbmap is not available here: the AZERTY checks did not run)\n");
    }
    return true;
}

}  // namespace hctest

#else  // no X11 client library: the group does not run

#include "testutil.h"

namespace hctest {

bool RealInputTests(const std::function<void(const std::wstring&, bool)>&) { return false; }

}  // namespace hctest

#endif
