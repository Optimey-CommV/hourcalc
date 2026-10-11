// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - the entry point of the test build: argument handling, the check that no window can
// reach the desktop, and the start-up measurement (process start to the first painted frame).

#include "testhooks.h"

#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <algorithm>
#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <vector>

#include "app.h"
#include "testutil.h"

extern char** environ;

namespace hctest {

namespace {

const char kUsage[] =
    "usage: hourcalc-gtk-test --selftest [--phase light|dark] [--group NAME] [--font \"Name N\"] [--verbose]\n"
    "       hourcalc-gtk-test --layout-info\n"
    "       hourcalc-gtk-test --render <dir> [--font \"Name N\"] [--sep ,|.] [--icons <linux/data/icons dir or file.svg>] [--system-dark]\n"
    "       hourcalc-gtk-test --measure-startup [--runs N]\n"
    "Run it headless only: linux/gtk/run-headless.sh <command>\n";

long long NowNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000000000LL + ts.tv_nsec;
}

// No window may ever reach the desktop of the owner (WSLg shows Linux windows on his Windows desktop): the test
// build runs on the X11 backend, on a display that is not :0, with no Wayland display in sight.
bool CheckHeadless(std::string* why) {
    const char* wl = getenv("WAYLAND_DISPLAY");
    if (wl && *wl) { *why = "WAYLAND_DISPLAY is set"; return false; }
    const char* be = getenv("GDK_BACKEND");
    if (!be || strcmp(be, "x11") != 0) { *why = "GDK_BACKEND is not x11"; return false; }
    const char* d = getenv("DISPLAY");
    if (!d || !*d) { *why = "DISPLAY is not set"; return false; }
    if (strcmp(d, ":0") == 0 || strncmp(d, ":0.", 3) == 0 || strncmp(d, "localhost", 9) == 0) { *why = std::string("DISPLAY is ") + d + ", a desktop display"; return false; }
    return true;
}

// ------------------------------------------------------------------ running a copy of this program

struct Captured {
    int status = -1;          // exit code, or 128 + signal, or -1
    std::string out;
    bool timedOut = false;
};

std::string SelfPath() {
    char buf[4096];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return "";
    buf[n] = 0;
    return buf;
}

Captured RunSelf(const std::vector<std::string>& args, const std::vector<std::pair<std::string, std::string>>& envSet, const std::vector<std::string>& envUnset,
                 bool silenceStderr, int timeoutMs) {
    Captured c;
    std::string self = SelfPath();
    std::vector<std::string> envs;
    for (char** e = environ; *e; ++e) {
        std::string s = *e;
        std::string key = s.substr(0, s.find('='));
        bool drop = std::find(envUnset.begin(), envUnset.end(), key) != envUnset.end();
        for (const auto& kv : envSet)
            if (kv.first == key) drop = true;
        if (!drop) envs.push_back(s);
    }
    for (const auto& kv : envSet) envs.push_back(kv.first + "=" + kv.second);
    std::vector<char*> envp;
    for (auto& s : envs) envp.push_back(const_cast<char*>(s.c_str()));
    envp.push_back(nullptr);
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(self.c_str()));
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    int fds[2];
    if (pipe(fds) != 0) return c;
    posix_spawn_file_actions_t fa;
    posix_spawn_file_actions_init(&fa);
    posix_spawn_file_actions_adddup2(&fa, fds[1], 1);
    posix_spawn_file_actions_addclose(&fa, fds[0]);
    posix_spawn_file_actions_addclose(&fa, fds[1]);
    if (silenceStderr) posix_spawn_file_actions_addopen(&fa, 2, "/dev/null", O_WRONLY, 0);
    pid_t pid = 0;
    int rc = posix_spawn(&pid, self.c_str(), &fa, nullptr, argv.data(), envp.data());
    posix_spawn_file_actions_destroy(&fa);
    close(fds[1]);
    if (rc != 0) {
        close(fds[0]);
        return c;
    }
    long long deadline = NowNs() + (long long)timeoutMs * 1000000LL;
    char buf[4096];
    for (;;) {
        long long left = (deadline - NowNs()) / 1000000LL;
        if (left <= 0) {
            c.timedOut = true;
            kill(pid, SIGKILL);
            break;
        }
        pollfd p = { fds[0], POLLIN, 0 };
        int pr = poll(&p, 1, (int)std::min<long long>(left, 1000));
        if (pr > 0) {
            ssize_t n = read(fds[0], buf, sizeof buf);
            if (n <= 0) break;
            c.out.append(buf, (size_t)n);
        } else if (pr < 0 && errno != EINTR) {
            break;
        }
    }
    close(fds[0]);
    int st = 0;
    waitpid(pid, &st, 0);
    c.status = WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0);
    return c;
}

// ------------------------------------------------------------------ start-up: the child

int CountLibs() {
    std::ifstream f("/proc/self/maps");
    std::set<std::string> libs;
    std::string line;
    while (std::getline(f, line)) {
        size_t p = line.find('/');
        if (p == std::string::npos) continue;
        std::string path = line.substr(p);
        if (path.find(".so") != std::string::npos) libs.insert(path);
    }
    return (int)libs.size();
}

long RssKb() {
    std::ifstream f("/proc/self/status");
    std::string line;
    while (std::getline(f, line))
        if (line.compare(0, 6, "VmRSS:") == 0) return atol(line.c_str() + 6);
    return -1;
}

// The moments of one start (CLOCK_MONOTONIC, ns): main() entered, the application activated, the window made,
// the window mapped, the first frame painted.
long long g_tMain = 0, g_tActivate = 0, g_tWindow = 0, g_tMap = 0;

void OnFirstFrame(GdkFrameClock*, gpointer win) {
    static bool done = false;
    if (done) return;
    done = true;
    long long t = NowNs();
    GskRenderer* r = gtk_native_get_renderer(GTK_NATIVE(win));
    std::printf("FIRSTFRAME %lld MAIN %lld ACTIVATE %lld WINDOW %lld MAP %lld RENDERER %s LIBS %d RSSKB %ld\n", t, g_tMain, g_tActivate, g_tWindow, g_tMap,
                r ? G_OBJECT_TYPE_NAME(r) : "none", CountLibs(), RssKb());
    std::fflush(stdout);
    _exit(0);
}

void HookWindow(GtkWidget* win) {
    g_tWindow = NowNs();
    g_signal_connect(win, "realize", G_CALLBACK(+[](GtkWidget* widget, gpointer) {
                         GdkFrameClock* fc = gtk_widget_get_frame_clock(widget);
                         if (fc) g_signal_connect(fc, "after-paint", G_CALLBACK(OnFirstFrame), widget);
                     }),
                     nullptr);
    g_signal_connect(win, "map", G_CALLBACK(+[](GtkWidget*, gpointer) { g_tMap = NowNs(); }), nullptr);
}

void OnActivated() { g_tActivate = NowNs(); }

void OnWindow(hcgtk::HcWindow* w, void*) {
    HookWindow(GTK_WIDGET(w->Window()));
}

int MeasureChild(char* argv0) {
    hcgtk::AppOptions opt;
    opt.flags = G_APPLICATION_NON_UNIQUE;       // no D-Bus registration: a bare Xvfb has no session bus
    opt.noSettings = true;
    opt.onActivate = OnActivated;
    opt.onWindow = OnWindow;
    char* av[] = { argv0, nullptr };
    return hcgtk::RunApp(1, av, opt);
}

// The floor: a libadwaita application with an empty window (one label), nothing else.
int MeasureChildEmpty(char* argv0) {
    AdwApplication* app = adw_application_new("be.optimey.HourCalc.Empty", G_APPLICATION_NON_UNIQUE);
    g_signal_connect(app, "activate", G_CALLBACK(+[](GApplication* a, gpointer) {
                         g_tActivate = NowNs();
                         GtkWidget* win = adw_application_window_new(GTK_APPLICATION(a));
                         gtk_window_set_default_size(GTK_WINDOW(win), 360, 600);
                         adw_application_window_set_content(ADW_APPLICATION_WINDOW(win), gtk_label_new("HourCalc"));
                         HookWindow(win);
                         gtk_window_present(GTK_WINDOW(win));
                     }),
                     nullptr);
    char* av[] = { argv0, nullptr };
    int rc = g_application_run(G_APPLICATION(app), 1, av);
    g_object_unref(app);
    return rc;
}

// ------------------------------------------------------------------ start-up: the parent

struct Sample {
    bool ok = false;
    double total = 0, toMain = 0, toActivate = 0, toWindow = 0, toMap = 0, toFrame = 0;     // ms: the phases add up to the total
    std::string renderer;
    int libs = 0;
    long rssKb = 0;
};

Sample MeasureOnce(const char* mode, const std::vector<std::pair<std::string, std::string>>& env) {
    Sample s;
    long long t0 = NowNs();
    // the time is taken as close to the spawn as possible; the child reports its own CLOCK_MONOTONIC readings
    Captured c = RunSelf({ mode }, env, {}, true, 30000);
    size_t p = c.out.find("FIRSTFRAME ");
    if (p == std::string::npos) return s;
    long long tf = 0, tm = 0, ta = 0, tw = 0, tp = 0;
    char renderer[128] = "";
    int libs = 0;
    long rss = 0;
    if (std::sscanf(c.out.c_str() + p, "FIRSTFRAME %lld MAIN %lld ACTIVATE %lld WINDOW %lld MAP %lld RENDERER %127s LIBS %d RSSKB %ld", &tf, &tm, &ta, &tw, &tp,
                    renderer, &libs, &rss) < 8)
        return s;
    s.ok = true;
    s.total = (double)(tf - t0) / 1e6;
    s.toMain = (double)(tm - t0) / 1e6;
    s.toActivate = (double)(ta - tm) / 1e6;
    s.toWindow = (double)(tw - ta) / 1e6;
    s.toMap = (double)(tp - tw) / 1e6;
    s.toFrame = (double)(tf - tp) / 1e6;
    s.renderer = renderer;
    s.libs = libs;
    s.rssKb = rss;
    return s;
}

double Median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

int MeasureStartup(int runs) {
    struct Config {
        const char* name;
        const char* mode;
        std::vector<std::pair<std::string, std::string>> env;
    };
    std::vector<Config> configs = {
        { "HourCalc as shipped (cairo)", "--measure-child", {} },
        { "HourCalc, the renderer GTK picks", "--measure-child", { { "HOURCALC_RENDERER", "gtk" } } },
        { "HourCalc as shipped, GTK_A11Y=none", "--measure-child", { { "GTK_A11Y", "none" } } },
        { "HourCalc, GTK's renderer, GTK_A11Y=none", "--measure-child", { { "HOURCALC_RENDERER", "gtk" }, { "GTK_A11Y", "none" } } },
        { "empty window, the renderer GTK picks", "--measure-child-empty", {} },
        { "empty window, GSK_RENDERER=cairo", "--measure-child-empty", { { "GSK_RENDERER", "cairo" } } },
    };
    std::printf("startup: GTK %u.%u.%u, libadwaita %u.%u.%u, process start to first frame, median of %d runs after 1 warm-up run (Xvfb, no D-Bus session)\n",
                gtk_get_major_version(), gtk_get_minor_version(), gtk_get_micro_version(), adw_get_major_version(), adw_get_minor_version(),
                adw_get_micro_version(), runs);
    std::printf("  [exec and linking | toolkit and application start | window made | map | first frame] (medians, ms)\n");
    int failures = 0;
    for (const Config& cfg : configs) {
        MeasureOnce(cfg.mode, cfg.env);       // warm-up: the page cache
        std::vector<Sample> samples;
        for (int i = 0; i < runs; i++) {
            Sample s = MeasureOnce(cfg.mode, cfg.env);
            if (s.ok) samples.push_back(s);
            else ++failures;
        }
        if (samples.empty()) {
            std::printf("  %-36s no result\n", cfg.name);
            continue;
        }
        std::vector<double> tot, a, b, c, d, e;
        for (auto& s : samples) {
            tot.push_back(s.total);
            a.push_back(s.toMain);
            b.push_back(s.toActivate);
            c.push_back(s.toWindow);
            d.push_back(s.toMap);
            e.push_back(s.toFrame);
        }
        std::vector<double> sorted = tot;
        std::sort(sorted.begin(), sorted.end());
        std::printf("  %-36s median %6.1f ms  min %6.1f  max %6.1f   [%5.1f | %5.1f | %5.1f | %5.1f | %5.1f]   %s, %d libraries, %.0f MiB\n", cfg.name,
                    Median(tot), sorted.front(), sorted.back(), Median(a), Median(b), Median(c), Median(d), Median(e), samples[0].renderer.c_str(),
                    samples[0].libs, samples[0].rssKb / 1024.0);
        std::string all;
        for (double m : sorted) {
            char buf[32];
            std::snprintf(buf, sizeof buf, " %.1f", m);
            all += buf;
        }
        std::printf("      all runs (sorted):%s\n", all.c_str());
    }
    return failures == 0 ? 0 : 1;
}

// ------------------------------------------------------------------ Ctrl+Q with a settings write pending

// The child: the real application (RunApp, the file in $XDG_CONFIG_HOME). The first window changes the theme and the
// decimal places, and the application is quit through its "quit" action (the Ctrl+Q one) before the idle callback
// that writes the settings could run.
int QuitProbeChild(char* argv0, const char* dir) {
    setenv("XDG_CONFIG_HOME", dir, 1);
    hcgtk::AppOptions opt;
    opt.flags = G_APPLICATION_NON_UNIQUE;
    opt.onWindow = [](hcgtk::HcWindow* w, void*) {
        w->SetThemeMode(hc::settings::ThemeMode::Dark);
        w->SetDecimalPlaces(4);
        g_idle_add_full(
            G_PRIORITY_HIGH,
            [](gpointer) -> gboolean {
                g_action_group_activate_action(G_ACTION_GROUP(g_application_get_default()), "quit", nullptr);
                return G_SOURCE_REMOVE;
            },
            nullptr, nullptr);
    };
    char* av[] = { argv0, nullptr };
    return hcgtk::RunApp(1, av, opt);
}

// The parent: runs the child and looks at the file. Returns the number of failed checks.
int QuitProbe() {
    char* tmp = g_dir_make_tmp("hcgtk-quit-XXXXXX", nullptr);
    if (!tmp) {
        std::fprintf(stderr, "FAIL [quit] cannot make a temporary folder\n");
        return 1;
    }
    std::string dir = tmp;
    g_free(tmp);
    int failures = 0;
    Captured c = RunSelf({ "--quit-probe", dir }, {}, {}, true, 30000);
    std::string text;
    {
        std::ifstream f(dir + "/hourcalc/hourcalc.ini", std::ios::binary);
        std::stringstream ss;
        ss << f.rdbuf();
        text = ss.str();
    }
    std::string flat;
    for (char ch : text)
        if (ch != '\r') flat += ch;
    if (c.status != 0 || c.timedOut) {
        std::fprintf(stderr, "FAIL [quit] the application ended with status %d%s\n", c.status, c.timedOut ? " (timed out)" : "");
        ++failures;
    }
    if (flat != "DecimalPlaces=4\nTheme=dark\n") {
        std::fprintf(stderr, "FAIL [quit] Ctrl+Q with the settings write pending: the file holds <%s>, expected DecimalPlaces=4 and Theme=dark\n", flat.c_str());
        ++failures;
    }
    std::printf("quit probe (Ctrl+Q with a settings write pending): %s\n", failures == 0 ? "the settings were saved" : "FAILED");
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    return failures;
}

// ------------------------------------------------------------------ the self test, in both system schemes

int SelfTestPhase(bool dark, bool verbose, const std::string& only, const std::string& font) {
    if (dark) setenv("ADW_DEBUG_COLOR_SCHEME", "prefer-dark", 1);
    else unsetenv("ADW_DEBUG_COLOR_SCHEME");
    InitToolkit(font.c_str());
    return SelfTest(dark, verbose, only, font == "Cantarell 11");
}

}  // namespace

int Main(int argc, char** argv) {
    g_tMain = NowNs();
    if (argc < 2) {
        std::fputs(kUsage, stderr);
        return 2;
    }
    std::string mode = argv[1];
    std::string why;
    if (!CheckHeadless(&why)) {
        std::fprintf(stderr, "hourcalc-gtk-test: refusing to start: %s. Use linux/gtk/run-headless.sh (xvfb-run, GDK_BACKEND=x11).\n", why.c_str());
        return 2;
    }
    if (mode == "--quit-probe" && argc >= 3) return QuitProbeChild(argv[0], argv[2]);
    if (mode == "--measure-child") return MeasureChild(argv[0]);
    if (mode == "--measure-child-empty") return MeasureChildEmpty(argv[0]);
    if (mode == "--layout-info") return LayoutInfo();
    if (mode == "--selftest") {
        std::string phase;
        std::string only;
        std::string font = "Cantarell 11";
        bool verbose = false;
        for (int i = 2; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--phase" && i + 1 < argc) phase = argv[++i];
            else if (a == "--group" && i + 1 < argc) only = argv[++i];
            else if (a == "--font" && i + 1 < argc) font = argv[++i];
            else if (a == "--verbose") verbose = true;
            else {
                std::fprintf(stderr, "unknown option %s\n%s", a.c_str(), kUsage);
                return 2;
            }
        }
        if (!phase.empty() && phase != "light" && phase != "dark") {
            std::fputs(kUsage, stderr);
            return 2;
        }
        if (!phase.empty()) return SelfTestPhase(phase == "dark", verbose, only, font);
        // Both system schemes: this process runs the light one, a copy of it the dark one (libadwaita reads the
        // scheme once, at start).
        int rc = SelfTestPhase(false, verbose, only, font);
        std::fflush(stdout);
        std::vector<std::string> args = { "--selftest", "--phase", "dark", "--font", font };
        if (verbose) args.push_back("--verbose");
        if (!only.empty()) {
            args.push_back("--group");
            args.push_back(only);
        }
        Captured c = RunSelf(args, {}, {}, false, 600000);
        std::fputs(c.out.c_str(), stdout);
        if (c.status != 0) rc = 1;
        if (only.empty() || std::string("quit").find(only) != std::string::npos) {
            if (QuitProbe() != 0) rc = 1;
        }
        if (only.empty() || std::string("high contrast").find(only) != std::string::npos) {
            // libadwaita reads the high contrast setting once, at start: a copy of this program runs the group with it on.
            Captured hcRun = RunSelf({ "--selftest", "--phase", "light", "--group", "high contrast", "--font", font }, { { "ADW_DEBUG_HIGH_CONTRAST", "1" }, { "HC_REQUIRE_HIGH_CONTRAST", "1" } }, {},
                                  false, 120000);
            std::fputs(hcRun.out.c_str(), stdout);
            if (hcRun.status != 0) rc = 1;
            std::printf("high contrast run: %s\n", hcRun.status == 0 ? "PASS" : "FAIL");
        }
        std::printf("selftest overall: %s\n", rc == 0 ? "PASS" : "FAIL");
        return rc;
    }
    if (mode == "--render") {
        for (int i = 2; i < argc; i++)
            if (std::string(argv[i]) == "--system-dark") setenv("ADW_DEBUG_COLOR_SCHEME", "prefer-dark", 1);
        std::string font = "Cantarell 11";
        for (int i = 2; i + 1 < argc; i++)
            if (std::string(argv[i]) == "--font") font = argv[i + 1];
        InitToolkit(font.c_str());
        return Render(argc, argv, 2);
    }
    if (mode == "--measure-startup") {
        int runs = 7;
        for (int i = 2; i < argc; i++) {
            std::string a = argv[i];
            if (a == "--runs" && i + 1 < argc) runs = std::max(1, atoi(argv[++i]));
            else {
                std::fprintf(stderr, "unknown option %s\n%s", a.c_str(), kUsage);
                return 2;
            }
        }
        return MeasureStartup(runs);
    }
    std::fprintf(stderr, "unknown option %s\n%s", mode.c_str(), kUsage);
    return 2;
}

}  // namespace hctest
