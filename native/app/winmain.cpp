// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc - entry point.

#include "app.h"
#ifdef HC_TESTHOOKS
#include "testhooks.h"
#endif

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, PWSTR cmdLine, int show) {
#ifdef HC_TESTHOOKS
    int rc = 0;
    if (testhooks::Handle(hInst, cmdLine, show, &rc)) return rc;
#endif
    (void)cmdLine;
    if (!gfx::Init()) {
        MessageBoxW(nullptr, L"HourCalc could not start its text and drawing engine.", L"HourCalc", MB_OK | MB_ICONERROR);
        return 1;
    }
    int code = 0;
    {
        std::wstring ini = hc::settings::DefaultPath();
        app::MainWindow w(ini.c_str());
        w.SetMain();
        if (w.Create(hInst, false)) {
            ShowWindow(w.hwnd(), show);
            MSG m;
            while (GetMessageW(&m, nullptr, 0, 0) > 0) {
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            code = (int)m.wParam;
        } else {
            code = 1;
        }
    }
    gfx::Shutdown();
    return code;
}
