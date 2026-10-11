// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// See x11helper.h. Plain Xlib; libXtst is loaded at run time (dlopen) so the test build does not need its headers or
// its library, and the real-input test reports that it did not run where it is not installed.

#include "x11helper.h"

#include <X11/XKBlib.h>
#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/keysym.h>

#include <dlfcn.h>

#include <algorithm>
#include <cstring>

namespace hcqt {
namespace testing {

namespace {

bool g_badAccess = false;

// Errors of a window that is gone already are normal for a window manager; a refused SubstructureRedirect is the
// sign that there is another one.
int errorHandler(Display*, XErrorEvent* e) {
    if (e->error_code == BadAccess) g_badAccess = true;
    return 0;
}

typedef int (*FakeKeyFn)(Display*, unsigned int, int, unsigned long);

}  // namespace

struct X11Env::Impl {
    Display* dpy = nullptr;
    Window root = 0;
    Window check = 0;
    void* xtst = nullptr;
    FakeKeyFn fakeKey = nullptr;
    bool wm = false;
    Atom netState = 0, netAbove = 0, netSupported = 0, netCheck = 0, netActive = 0, netName = 0, utf8 = 0;

    std::vector<Atom> stateOf(Window w) const {
        std::vector<Atom> atoms;
        Atom type = 0;
        int format = 0;
        unsigned long count = 0, after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(dpy, w, netState, 0, 64, False, XA_ATOM, &type, &format, &count, &after, &data) == Success && data) {
            if (type == XA_ATOM && format == 32) {
                const Atom* list = reinterpret_cast<const Atom*>(data);
                for (unsigned long i = 0; i < count; ++i) atoms.push_back(list[i]);
            }
            XFree(data);
        }
        return atoms;
    }

    void setState(Window w, const std::vector<Atom>& atoms) {
        XChangeProperty(dpy, w, netState, XA_ATOM, 32, PropModeReplace, reinterpret_cast<const unsigned char*>(atoms.data()), static_cast<int>(atoms.size()));
    }

    // _NET_WM_STATE client message: data.l[0] 0 remove, 1 add, 2 toggle; l[1] and l[2] the atoms.
    void stateRequest(const XClientMessageEvent& m) {
        std::vector<Atom> state = stateOf(m.window);
        for (int i = 1; i <= 2; ++i) {
            const Atom a = static_cast<Atom>(m.data.l[i]);
            if (!a) continue;
            auto it = std::find(state.begin(), state.end(), a);
            const bool has = it != state.end();
            const long action = m.data.l[0];
            if (action == 1 || (action == 2 && !has)) {
                if (!has) state.push_back(a);
            } else if (has) {
                state.erase(it);
            }
        }
        setState(m.window, state);
    }
};

X11Env::X11Env() : d_(new Impl) {}

X11Env::~X11Env() {
    // The display first: libXtst registers a hook on it that XCloseDisplay calls, so the library must still be loaded.
    if (d_->dpy) XCloseDisplay(d_->dpy);
    if (d_->xtst) dlclose(d_->xtst);
    delete d_;
}

bool X11Env::open(std::string* why) {
    d_->dpy = XOpenDisplay(nullptr);
    if (!d_->dpy) {
        *why = "cannot open the X display";
        return false;
    }
    XSetErrorHandler(errorHandler);
    d_->root = DefaultRootWindow(d_->dpy);
    d_->netState = XInternAtom(d_->dpy, "_NET_WM_STATE", False);
    d_->netAbove = XInternAtom(d_->dpy, "_NET_WM_STATE_ABOVE", False);
    d_->netSupported = XInternAtom(d_->dpy, "_NET_SUPPORTED", False);
    d_->netCheck = XInternAtom(d_->dpy, "_NET_SUPPORTING_WM_CHECK", False);
    d_->netActive = XInternAtom(d_->dpy, "_NET_ACTIVE_WINDOW", False);
    d_->netName = XInternAtom(d_->dpy, "_NET_WM_NAME", False);
    d_->utf8 = XInternAtom(d_->dpy, "UTF8_STRING", False);
    d_->xtst = dlopen("libXtst.so.6", RTLD_NOW);
    if (d_->xtst) d_->fakeKey = reinterpret_cast<FakeKeyFn>(dlsym(d_->xtst, "XTestFakeKeyEvent"));
    return true;
}

bool X11Env::hasXTest() const { return d_->fakeKey != nullptr; }

bool X11Env::startWindowManager() {
    g_badAccess = false;
    XSelectInput(d_->dpy, d_->root, SubstructureRedirectMask | SubstructureNotifyMask);
    XSync(d_->dpy, False);
    if (g_badAccess) return false;
    d_->wm = true;
    // The marks of an EWMH window manager: Qt asks the root window what the manager supports.
    d_->check = XCreateSimpleWindow(d_->dpy, d_->root, -10, -10, 1, 1, 0, 0, 0);
    Atom supported[] = {d_->netState, d_->netAbove, d_->netCheck, d_->netActive};
    XChangeProperty(d_->dpy, d_->root, d_->netSupported, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(supported), 4);
    XChangeProperty(d_->dpy, d_->root, d_->netCheck, XA_WINDOW, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&d_->check), 1);
    XChangeProperty(d_->dpy, d_->check, d_->netCheck, XA_WINDOW, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&d_->check), 1);
    const char name[] = "hourcalc-test-wm";
    XChangeProperty(d_->dpy, d_->check, d_->netName, d_->utf8, 8, PropModeReplace, reinterpret_cast<const unsigned char*>(name), static_cast<int>(sizeof name - 1));
    XFlush(d_->dpy);
    return true;
}

void X11Env::pump() {
    while (XPending(d_->dpy) > 0) {
        XEvent e;
        XNextEvent(d_->dpy, &e);
        switch (e.type) {
        case MapRequest:
            XMapWindow(d_->dpy, e.xmaprequest.window);
            break;
        case MapNotify:
            // The window that was mapped last has the keyboard.
            if (d_->wm && !e.xmap.override_redirect) XSetInputFocus(d_->dpy, e.xmap.window, RevertToParent, CurrentTime);
            break;
        case ConfigureRequest: {
            XWindowChanges changes;
            changes.x = e.xconfigurerequest.x;
            changes.y = e.xconfigurerequest.y;
            changes.width = e.xconfigurerequest.width;
            changes.height = e.xconfigurerequest.height;
            changes.border_width = e.xconfigurerequest.border_width;
            changes.sibling = e.xconfigurerequest.above;
            changes.stack_mode = e.xconfigurerequest.detail;
            XConfigureWindow(d_->dpy, e.xconfigurerequest.window, static_cast<unsigned>(e.xconfigurerequest.value_mask), &changes);
            break;
        }
        case ClientMessage:
            if (e.xclient.message_type == d_->netState) d_->stateRequest(e.xclient);
            else if (e.xclient.message_type == d_->netActive) XSetInputFocus(d_->dpy, e.xclient.window, RevertToParent, CurrentTime);
            break;
        case MappingNotify:
            XRefreshKeyboardMapping(&e.xmapping);
            break;
        default:
            break;
        }
    }
    XFlush(d_->dpy);
}

void X11Env::flush() { XFlush(d_->dpy); }

int X11Env::keycodeForKeysym(const char* name) const {
    const KeySym ks = XStringToKeysym(name);
    return ks == NoSymbol ? 0 : XKeysymToKeycode(d_->dpy, ks);
}

std::string X11Env::keysymAt(int keycode, int level) const {
    const KeySym ks = XkbKeycodeToKeysym(d_->dpy, static_cast<KeyCode>(keycode), 0, level);
    const char* name = ks == NoSymbol ? nullptr : XKeysymToString(ks);
    return name ? name : "";
}

void X11Env::key(int keycode, bool press) {
    if (d_->fakeKey) d_->fakeKey(d_->dpy, static_cast<unsigned>(keycode), press ? True : False, 0);
}

void X11Env::focusWindow(unsigned long xid) { XSetInputFocus(d_->dpy, static_cast<Window>(xid), RevertToParent, CurrentTime); }

std::vector<std::string> X11Env::netWmState(unsigned long xid) const {
    std::vector<std::string> names;
    for (Atom a : d_->stateOf(static_cast<Window>(xid))) {
        char* n = XGetAtomName(d_->dpy, a);
        if (n) {
            names.push_back(n);
            XFree(n);
        }
    }
    return names;
}

}  // namespace testing
}  // namespace hcqt
