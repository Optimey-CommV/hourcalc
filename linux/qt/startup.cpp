// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// --measure-startup: process start to first paint, offscreen. The parent starts the probe as a child
// process N times; it takes the time on the monotonic clock just before fork() and the child reports the
// time of its first paint on the same clock (the clock is system wide), so the figure holds everything:
// fork and exec, the dynamic linker, Qt's start-up, building the window, the first frame.

#include "testhooks.h"

#include "mainwindow.h"
#include "util.h"

#include "hc_core.h"

#include <QApplication>
#include <QFile>
#include <QLabel>
#include <QSet>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

#include <sys/wait.h>
#include <unistd.h>

namespace hcqt {
namespace testing {

namespace {

long long monotonicNs() {
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return static_cast<long long>(ts.tv_sec) * 1000000000LL + ts.tv_nsec;
}

QStringList loadedLibraries() {
    QStringList libs;
    QSet<QString> seen;
    QFile maps(QStringLiteral("/proc/self/maps"));
    if (maps.open(QIODevice::ReadOnly)) {
        // /proc files report no size: read it all, then split
        const QStringList lines = QString::fromLocal8Bit(maps.readAll()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString& raw : lines) {
            const QString line = raw.trimmed();
            const int slash = static_cast<int>(line.indexOf(QLatin1Char('/')));
            if (slash < 0) continue;
            const QString path = line.mid(slash);
            if (!path.contains(QLatin1String(".so")) || seen.contains(path)) continue;
            seen.insert(path);
            libs << path;
        }
    }
    return libs;
}

long long g_mainNs = 0;          // main() was entered
long long g_appNs = 0;           // QApplication exists
long long g_initNs = 0;          // Qt's widget, style and font machinery is up (one throwaway label)
long long g_windowNs = 0;        // the window is built
long long g_shownNs = 0;         // show() returned

double median(std::vector<double> v) {
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

}  // namespace

void noteMainEntry() { g_mainNs = monotonicNs(); }

int runStartupProbe() {
    g_appNs = monotonicNs();
    {
        // The first widget pays for Qt: the style, the font database, the first font engine. Timed apart, so the
        // cost of the window itself shows.
        QLabel probe(QStringLiteral("00:00 Hours : minutes"));
        probe.ensurePolished();
        (void)probe.sizeHint();
    }
    g_initNs = monotonicNs();
    MainWindow window(q(hc::settings::DefaultPath()));
    g_windowNs = monotonicNs();
    MainWindow::setFirstPaintHook([] {
        const long long now = monotonicNs();
        std::printf("MAIN %lld\nAPP %lld\nINIT %lld\nWINDOW %lld\nSHOWN %lld\nFIRSTPAINT %lld\n", g_mainNs, g_appNs, g_initNs, g_windowNs, g_shownNs, now);
        for (const QString& lib : loadedLibraries()) std::printf("LIB %s\n", lib.toLocal8Bit().constData());
        std::fflush(stdout);
        QApplication::quit();
    });
    QTimer::singleShot(15000, [] { QApplication::exit(3); });      // the first paint never came
    window.show();
    g_shownNs = monotonicNs();
    return QApplication::exec();
}

int runMeasureStartup(const char*, int runs, bool xcb) {
    char exe[4096];
    const ssize_t n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0) {
        std::fprintf(stderr, "cannot find the path of the test program\n");
        return 1;
    }
    exe[n] = 0;

    // per run: total, and the phases: fork+exec+dynamic linking, Qt start-up, building the window, show(), first frame
    std::vector<double> total, toMain, toApp, toInit, toWindow, toShown, toPaint;
    QStringList libs;
    for (int i = 0; i < runs; ++i) {
        int fds[2];
        if (pipe(fds) != 0) return 1;
        const long long t0 = monotonicNs();
        const pid_t pid = fork();
        if (pid < 0) return 1;
        if (pid == 0) {
            dup2(fds[1], 1);
            close(fds[0]);
            close(fds[1]);
            if (xcb) execl(exe, exe, "--startup-probe", "--xcb", static_cast<char*>(nullptr));
            else execl(exe, exe, "--startup-probe", static_cast<char*>(nullptr));
            _exit(127);
        }
        close(fds[1]);
        std::string out;
        char buf[4096];
        ssize_t got;
        while ((got = read(fds[0], buf, sizeof buf)) > 0) out.append(buf, static_cast<size_t>(got));
        close(fds[0]);
        int status = 0;
        waitpid(pid, &status, 0);
        long long mark[6] = {0, 0, 0, 0, 0, 0};       // MAIN APP INIT WINDOW SHOWN FIRSTPAINT
        const char* names[6] = {"MAIN ", "APP ", "INIT ", "WINDOW ", "SHOWN ", "FIRSTPAINT "};
        QStringList runLibs;
        for (const QString& line : QString::fromLocal8Bit(out.c_str()).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
            for (int k = 0; k < 6; ++k)
                if (line.startsWith(QLatin1String(names[k]))) mark[k] = line.mid(static_cast<int>(std::strlen(names[k]))).toLongLong();
            if (line.startsWith(QLatin1String("LIB "))) runLibs << line.mid(4);
        }
        if (mark[5] == 0) {
            std::fprintf(stderr, "run %d: no first paint reported (exit status %d)\n", i + 1, status);
            return 1;
        }
        total.push_back((mark[5] - t0) / 1e6);
        toMain.push_back((mark[0] - t0) / 1e6);
        toApp.push_back((mark[1] - mark[0]) / 1e6);
        toInit.push_back((mark[2] - mark[1]) / 1e6);
        toWindow.push_back((mark[3] - mark[2]) / 1e6);
        toShown.push_back((mark[4] - mark[3]) / 1e6);
        toPaint.push_back((mark[5] - mark[4]) / 1e6);
        libs = runLibs;
    }

    std::vector<double> sorted = total;
    std::sort(sorted.begin(), sorted.end());
    std::printf("startup, process start to first paint (%s), %d runs:\n", xcb ? "xcb platform on the virtual display" : "offscreen", runs);
    for (size_t i = 0; i < total.size(); ++i) std::printf("  run %zu: %.1f ms\n", i + 1, total[i]);
    std::printf("median %.1f ms, min %.1f ms, max %.1f ms\n", median(total), sorted.front(), sorted.back());
    std::printf("phases (medians): fork+exec+linking %.1f ms, QApplication %.1f ms, Qt widget/font init %.1f ms, the window %.1f ms, show() %.1f ms, first frame %.1f ms\n",
                median(toMain), median(toApp), median(toInit), median(toWindow), median(toShown), median(toPaint));
    std::printf("loaded shared objects at the first paint: %d\n", static_cast<int>(libs.size()));
    for (const QString& lib : libs) std::printf("  %s\n", lib.toLocal8Bit().constData());
    return 0;
}

}  // namespace testing
}  // namespace hcqt
