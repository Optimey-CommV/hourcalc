// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - the render tool (hourcalc-gtk-test --render <dir>): drives the real window headless
// (under Xvfb) through the states of the app and writes a PNG of each, made from the widget tree
// (GtkWidgetPaintable -> GskRenderNode -> GskRenderer -> GdkTexture -> PNG).

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

#include "appinfo.h"
#include "testutil.h"

namespace hctest {

namespace {

using hc::settings::ThemeMode;
using hcgtk::FromUtf8;
using hcgtk::ToUtf8;

struct Options {
    std::string dir;
    std::string font = "Cantarell 11";
    std::wstring sep = L",";
    std::string iconSvg;
    std::string tag;           // part of the file names ("" for none)
};

int g_written = 0, g_problems = 0;

// The mean luminance (0..1) of a texture; the check that a "dark" image is dark and a "light" one light.
double MeanLuma(GdkTexture* tex) {
    int w = gdk_texture_get_width(tex), h = gdk_texture_get_height(tex);
    std::vector<guchar> data((size_t)w * (size_t)h * 4);
    gdk_texture_download(tex, data.data(), (gsize)w * 4);
    double sum = 0;
    for (size_t i = 0; i < (size_t)w * (size_t)h; i++) {
        const guchar* p = &data[i * 4];     // B8G8R8A8 (premultiplied; the images are opaque)
        sum += (0.2126 * p[2] + 0.7152 * p[1] + 0.0722 * p[0]) / 255.0;
    }
    return sum / ((double)w * (double)h);
}

bool Snapshot(GtkWidget* widget, const std::string& file, double* luma) {
    int width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
    if (width <= 0 || height <= 0) return false;
    GdkPaintable* p = gtk_widget_paintable_new(widget);
    GtkSnapshot* snap = gtk_snapshot_new();
    gdk_paintable_snapshot(p, snap, width, height);
    g_object_unref(p);
    GskRenderNode* node = gtk_snapshot_free_to_node(snap);
    if (!node) return false;
    GskRenderer* r = gtk_native_get_renderer(gtk_widget_get_native(widget));
    graphene_rect_t bounds;
    graphene_rect_init(&bounds, 0.0f, 0.0f, (float)width, (float)height);
    GdkTexture* tex = r ? gsk_renderer_render_texture(r, node, &bounds) : nullptr;
    gsk_render_node_unref(node);
    if (!tex) return false;
    bool ok = gdk_texture_save_to_png(tex, file.c_str()) != FALSE;
    if (luma) *luma = MeanLuma(tex);
    g_object_unref(tex);
    return ok;
}

struct Ctx {
    const Options* opt;
    std::string theme;        // light, dark, auto-system-light, ...
    std::string size;         // default, min
    bool expectDark;
    bool expectKnown;
};

void Shot(const Ctx& c, GtkWidget* widget, const std::string& state) {
    std::string name = state + "--" + c.theme + "--" + c.size + ".png";
    std::string file = c.opt->dir + "/" + name;
    double luma = -1;
    if (!Snapshot(widget, file, &luma)) {
        std::fprintf(stderr, "render: cannot write %s\n", name.c_str());
        ++g_problems;
        return;
    }
    ++g_written;
    bool ok = true;
    if (c.expectKnown) ok = c.expectDark ? luma < 0.35 : luma > 0.65;
    if (!ok) {
        std::fprintf(stderr, "render: %s has mean luminance %.2f, which is wrong for %s\n", name.c_str(), luma, c.expectDark ? "dark" : "light");
        ++g_problems;
    }
    std::printf("  %-52s %4dx%-4d luma %.2f\n", name.c_str(), gtk_widget_get_width(widget), gtk_widget_get_height(widget), luma);
}

void Keys(HcWindow& w, const wchar_t* keys) {
    for (const wchar_t* p = keys; *p; ++p) w.Press(std::wstring(1, *p));
}

void RenderSet(const Options& opt, ThemeMode mode, const std::string& themeName, bool expectKnown, bool expectDark, int width, int height,
               const char* sizeName, bool full) {
    Ctx c{ &opt, themeName, sizeName, expectDark, expectKnown };
    Win wp;
    HcWindow& w = *wp;
    w.SetThemeMode(mode);
    gtk_window_set_default_size(w.Window(), width, height);
    w.Settle(4);
    GtkWidget* win = GTK_WIDGET(w.Window());
    auto shot = [&](const char* state) {
        w.WaitFrames(3);
        Shot(c, win, state);
    };
    shot("converter-idle");
    if (!full) {
        w.SelectTab(1);
        Keys(w, L"7:30+7:20=");
        shot("calculator-result");
        return;
    }
    w.SetConverterText(0, L"08:30");
    w.GotFocus(0, false);
    shot("converter-left-0830");
    w.Press(L"RESET");
    w.SetConverterText(1, L"8" + opt.sep + L"5");
    w.GotFocus(1, false);
    shot("converter-right-8-5");
    w.Press(L"RESET");
    w.SetConverterText(0, L"9999999:99");
    shot("converter-long");
    w.Press(L"RESET");

    w.SelectTab(1);
    shot("calculator-idle");
    Keys(w, L"7:30+7:20=");
    shot("calculator-result");
    w.Press(L"C");
    Keys(w, L"7:30+7:2");
    shot("calculator-entry");
    w.Press(L"C");
    Keys(w, L"7:30/0=");
    shot("calculator-divide-by-zero");
    w.Press(L"C");
    for (int i = 0; i < 14; i++) Keys(w, L"7:30+");
    shot("calculator-chain");
    w.Press(L"C");
    Keys(w, L"1234567:59*7=");
    shot("calculator-long");
    w.Press(L"C");
    w.SelectTab(0);

    // The primary menu: a popup of its own, snapshot on its own.
    w.OpenMenu();
    WaitUntil([&] { return w.IsMenuOpen() && gtk_widget_get_mapped(w.MenuPopover()); }, 3000);
    w.WaitFrames(4);
    GtkWidget* pop = w.MenuPopover();
    if (gtk_widget_get_width(pop) > 0) {
        // the popover's contents (the menu box inside the popover)
        Shot(c, pop, "menu");
    } else {
        std::fprintf(stderr, "render: the menu popover has no size\n");
        ++g_problems;
    }
    w.CloseMenu();
    WaitUntil([&] { return !w.IsMenuOpen(); }, 2000);
    Pump();

    w.ShowAbout();
    WaitUntil([&] { return w.IsAboutOpen(); }, 2000);
    w.WaitFrames(6);
    shot("about");
    w.CloseAbout();
    Pump();
}

bool Parse(int argc, char** argv, int first, Options& o) {
    for (int i = first; i < argc; i++) {
        std::string a = argv[i];
        auto val = [&](std::string& dst) {
            if (i + 1 >= argc) return false;
            dst = argv[++i];
            return true;
        };
        if (a == "--font") { if (!val(o.font)) return false; }
        else if (a == "--icons" || a == "--icon-svg") { if (!val(o.iconSvg)) return false; }
        else if (a == "--tag") { if (!val(o.tag)) return false; }
        else if (a == "--sep") {
            std::string s;
            if (!val(s)) return false;
            o.sep = FromUtf8(s);
        } else if (a == "--system-dark") {
            // handled before the toolkit starts
        } else if (o.dir.empty() && a.compare(0, 2, "--") != 0) {
            o.dir = a;
        } else {
            std::fprintf(stderr, "render: unknown option %s\n", a.c_str());
            return false;
        }
    }
    return !o.dir.empty();
}

}  // namespace

int Render(int argc, char** argv, int firstArg) {
    Options opt;
    if (!Parse(argc, argv, firstArg, opt)) {
        std::fprintf(stderr, "usage: --render <dir> [--font \"Name N\"] [--sep ,|.] [--icons <linux/data/icons dir or file.svg>] [--system-dark]\n");
        return 2;
    }
    std::error_code ec;
    std::filesystem::create_directories(opt.dir, ec);
    if (ec) {
        std::fprintf(stderr, "render: cannot create %s\n", opt.dir.c_str());
        return 2;
    }
    hc::timemath::SetDecimalSeparatorOverride(opt.sep);
    // The app icon of the About dialog, from the source tree when it is not installed: --icons names the icon
    // folder of linux/data (hicolor/NxN/apps/hourcalc.png, copied under the application id) or an SVG file.
    std::string iconRoot;
    if (!opt.iconSvg.empty()) {
        char* tmp = g_dir_make_tmp("hcgtk-icons-XXXXXX", nullptr);
        if (tmp) {
            iconRoot = tmp;
            g_free(tmp);
            namespace fs = std::filesystem;
            if (fs::is_directory(opt.iconSvg, ec)) {
                for (const auto& size : fs::directory_iterator(fs::path(opt.iconSvg) / "hicolor", ec)) {
                    fs::path src = size.path() / "apps" / "hourcalc.png";
                    if (!fs::exists(src, ec)) continue;
                    fs::path dst = fs::path(iconRoot) / "hicolor" / size.path().filename() / "apps";
                    fs::create_directories(dst, ec);
                    fs::copy_file(src, dst / (std::string(HC_APP_ID) + ".png"), ec);
                }
            } else {
                std::string apps = iconRoot + "/hicolor/scalable/apps";
                fs::create_directories(apps, ec);
                fs::copy_file(opt.iconSvg, apps + "/" + HC_APP_ID + ".svg", ec);
            }
            gtk_icon_theme_add_search_path(gtk_icon_theme_get_for_display(gdk_display_get_default()), iconRoot.c_str());
        }
    }
    std::printf("render: GTK %u.%u.%u, libadwaita %u.%u.%u, font \"%s\", separator '%s' -> %s\n", gtk_get_major_version(), gtk_get_minor_version(),
                gtk_get_micro_version(), adw_get_major_version(), adw_get_minor_version(), adw_get_micro_version(), opt.font.c_str(),
                ToUtf8(opt.sep).c_str(), opt.dir.c_str());
    const struct { int w, h; const char* name; } sizes[] = {
        { HcWindow::kDefaultWidth, HcWindow::kDefaultHeight, "default" },
        { 100, 100, "min" },               // asked to be tiny: the window ends up at its minimum size (360 x 480)
    };
    for (const auto& s : sizes) {
        RenderSet(opt, ThemeMode::Light, "light", true, false, s.w, s.h, s.name, true);
        RenderSet(opt, ThemeMode::Dark, "dark", true, true, s.w, s.h, s.name, true);
    }
    // Auto follows the system scheme (light, or dark when started with --system-dark).
    bool sysDark = g_getenv("ADW_DEBUG_COLOR_SCHEME") && std::strcmp(g_getenv("ADW_DEBUG_COLOR_SCHEME"), "prefer-dark") == 0;
    RenderSet(opt, ThemeMode::Auto, sysDark ? "auto-system-dark" : "auto-system-light", true, sysDark, HcWindow::kDefaultWidth, HcWindow::kDefaultHeight,
              "default", false);
    if (!iconRoot.empty()) std::filesystem::remove_all(iconRoot, ec);
    std::printf("render: %d images written, %d problems\n", g_written, g_problems);
    return g_problems == 0 ? 0 : 1;
}

}  // namespace hctest
