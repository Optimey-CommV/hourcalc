// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

#include "app.h"

#include <dirent.h>
#include <unistd.h>

#include <cstdio>
#include <cstring>

#include "appinfo.h"
#include "util.h"

namespace hcgtk {

namespace {

void OnActivate(GApplication* app, gpointer data) {
    const AppOptions* opt = static_cast<const AppOptions*>(data);
    if (opt->onActivate) opt->onActivate();
    // A second start brings the running window to the front.
    GtkWindow* existing = gtk_application_get_active_window(GTK_APPLICATION(app));
    if (existing) {
        gtk_window_present(existing);
        return;
    }
    std::wstring path = opt->noSettings ? std::wstring() : hc::settings::DefaultPath();
    HcWindow* w = HcWindow::Create(GTK_APPLICATION(app), path);
    if (opt->onWindow) opt->onWindow(w, opt->user);
    w->Present();
}

gint OnLocalOptions(GApplication*, GVariantDict* options, gpointer) {
    if (g_variant_dict_contains(options, "version")) {
        std::printf("hourcalc-gtk %s\n", HC_VERSION);
        return 0;
    }
    return -1;
}

// The application ends (Ctrl+Q, the quit action, the last window closed): a setting that was changed a moment ago is
// still waiting for its write (it is written when the main loop is idle). Closing a window writes it, quitting the
// application does not close the windows first, so it is written here.
void OnShutdown(GApplication* app, gpointer) {
    for (GList* l = gtk_application_get_windows(GTK_APPLICATION(app)); l; l = l->next) {
        HcWindow* w = HcWindow::Of(GTK_WINDOW(l->data));
        if (w) w->FlushSettings();
    }
}

// The text of --help: the usage line names the program (GLib would print the application id, which is the program name
// here for the sake of the desktop: the window manager matches the window to the desktop file by it).
bool WantsHelp(int argc, char** argv) {
    for (int i = 1; i < argc; ++i)
        if (!std::strcmp(argv[i], "--help") || !std::strcmp(argv[i], "-h")) return true;
    return false;
}

void PrintHelp() {
    std::printf(
        "Usage:\n"
        "  hourcalc-gtk [OPTION...]\n"
        "\n"
        "HourCalc: converts hours between h:mm and decimal hours, and calculates with hours.\n"
        "\n"
        "Options:\n"
        "  -h, --help       Show this help and exit\n"
        "  --version        Show the version and exit\n"
        "\n"
        "The look of the window comes from GTK and libadwaita (the usual GTK_* and ADW_* variables apply). HOURCALC_RENDERER=gtk\n"
        "lets GTK choose its renderer, any other value is passed on as GSK_RENDERER (see hourcalc(1)).\n");
}

}  // namespace

bool HasRenderNode(const char* dir) {
    DIR* d = opendir(dir);
    if (!d) return false;
    bool found = false;
    while (const dirent* e = readdir(d)) {
        if (std::strncmp(e->d_name, "renderD", 7) != 0) continue;
        std::string path = std::string(dir) + "/" + e->d_name;
        if (access(path.c_str(), R_OK | W_OK) == 0) {
            found = true;
            break;
        }
    }
    closedir(d);
    return found;
}

std::string RendererWish(const char* gskRenderer, const char* hourcalcRenderer, bool softwareOnly) {
    if (gskRenderer && *gskRenderer) return std::string();               // the user chose
    if (hourcalcRenderer && *hourcalcRenderer) {
        if (std::strcmp(hourcalcRenderer, "gtk") == 0) return std::string();   // GTK's own choice
        return hourcalcRenderer;
    }
    return softwareOnly ? "cairo" : std::string();
}

void ChooseRenderer() {
    const char* soft = g_getenv("LIBGL_ALWAYS_SOFTWARE");
    bool software = (soft && *soft && std::strcmp(soft, "0") != 0) || !HasRenderNode("/dev/dri");
    std::string wish = RendererWish(g_getenv("GSK_RENDERER"), g_getenv("HOURCALC_RENDERER"), software);
    if (!wish.empty()) g_setenv("GSK_RENDERER", wish.c_str(), FALSE);
}

int RunApp(int argc, char** argv, const AppOptions& opt) {
    if (WantsHelp(argc, argv)) {
        PrintHelp();
        return 0;
    }
    ChooseRenderer();
    g_set_prgname(HC_APP_ID);
    g_set_application_name("HourCalc");
    AdwApplication* app = adw_application_new(HC_APP_ID, opt.flags);
    g_signal_connect(app, "startup", G_CALLBACK(+[](GApplication*, gpointer) { gtk_window_set_default_icon_name(HC_APP_ID); }), nullptr);
    g_application_add_main_option(G_APPLICATION(app), "version", 0, G_OPTION_FLAG_NONE, G_OPTION_ARG_NONE, "Show the version and exit", nullptr);
    g_signal_connect(app, "handle-local-options", G_CALLBACK(OnLocalOptions), nullptr);
    g_signal_connect(app, "activate", G_CALLBACK(OnActivate), const_cast<AppOptions*>(&opt));
    g_signal_connect(app, "shutdown", G_CALLBACK(OnShutdown), nullptr);

    GSimpleAction* quit = g_simple_action_new("quit", nullptr);
    g_signal_connect(quit, "activate", G_CALLBACK(+[](GSimpleAction*, GVariant*, gpointer a) { g_application_quit(G_APPLICATION(a)); }), app);
    g_action_map_add_action(G_ACTION_MAP(app), G_ACTION(quit));
    g_object_unref(quit);
    const char* quitAccels[] = { "<Primary>q", nullptr };
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "app.quit", quitAccels);
    const char* closeAccels[] = { "<Primary>w", nullptr };
    gtk_application_set_accels_for_action(GTK_APPLICATION(app), "window.close", closeAccels);

    int rc = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return rc;
}

}  // namespace hcgtk
