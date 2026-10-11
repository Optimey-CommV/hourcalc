// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "testutil.h"

#include "app.h"

#include <cstdlib>

namespace hctest {

static GtkApplication* g_app = nullptr;

void InitToolkit(const char* font) {
    hcgtk::ChooseRenderer();       // what the shipped program paints with
    // Quiet, fast and self-contained: no accessibility bus, no dconf.
    setenv("GTK_A11Y", "none", 0);
    setenv("GSETTINGS_BACKEND", "memory", 0);
    gtk_init();
    adw_init();
    GtkSettings* s = gtk_settings_get_default();
    g_object_set(s, "gtk-enable-animations", FALSE, nullptr);
    if (font && *font) g_object_set(s, "gtk-font-name", font, nullptr);
    // The desktop's text scaling factor, for a render or a run at large text: HC_TEST_TEXT_SCALE=2 (96 dpi * the factor).
    const char* scale = getenv("HC_TEST_TEXT_SCALE");
    if (scale && *scale && atof(scale) > 0) g_object_set(s, "gtk-xft-dpi", (int)(96.0 * 1024.0 * atof(scale) + 0.5), nullptr);
    // What GNOME shows in the header bar: the close button only (a bare GTK has minimize and maximize as well).
    g_object_set(s, "gtk-decoration-layout", ":close", nullptr);
    g_app = GTK_APPLICATION(adw_application_new("io.github.Optimey_CommV.HourCalc.Test", G_APPLICATION_NON_UNIQUE));
    GError* err = nullptr;
    if (!g_application_register(G_APPLICATION(g_app), nullptr, &err)) {
        g_printerr("cannot register the test application: %s\n", err ? err->message : "?");
        if (err) g_error_free(err);
        exit(2);
    }
}

GtkApplication* App() { return g_app; }

void Pump() {
    while (g_main_context_iteration(nullptr, FALSE)) {
    }
}

void WaitUntil(const std::function<bool()>& done, int timeoutMs) {
    bool timedOut = false;
    guint t = g_timeout_add(timeoutMs, [](gpointer d) -> gboolean { *static_cast<bool*>(d) = true; return G_SOURCE_REMOVE; }, &timedOut);
    while (!done() && !timedOut) g_main_context_iteration(nullptr, TRUE);
    if (!timedOut) g_source_remove(t);
}

Win::Win(bool show, const std::wstring& settingsPath) : w_(HcWindow::Create(g_app, settingsPath)) {
    // Counts as the active window, so the keyboard focus rules apply as they do in a window that is in use
    // (a virtual display has no window manager that would activate it).
    w_->SetWindowActive(true);
    if (show) w_->Settle();
}

Win::~Win() {
    w_->Destroy();
    Pump();
}

}  // namespace hctest
