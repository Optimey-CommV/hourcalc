// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - helpers shared by the test hooks (self test, render tool, start-up measurement).

#pragma once

#include <string>

#include "hcwindow.h"
#include "util.h"

namespace hctest {

using hcgtk::HcWindow;

// The toolkit for the tests: GTK and libadwaita initialised, an application object (no D-Bus, no main loop run
// by it) that the windows belong to, animations off. font: a GtkSettings font name ("" keeps the default).
void InitToolkit(const char* font);
GtkApplication* App();

// Runs the pending events of the main loop.
void Pump();
// Runs the main loop until done() or the timeout.
void WaitUntil(const std::function<bool()>& done, int timeoutMs);

// A window for a test: destroyed (and the loop pumped) when the scope ends.
class Win {
public:
    explicit Win(bool show = false, const std::wstring& settingsPath = std::wstring());
    ~Win();
    HcWindow* operator->() const { return w_; }
    HcWindow& operator*() const { return *w_; }
    HcWindow* get() const { return w_; }
private:
    HcWindow* w_;
};

int SelfTest(bool systemDark, bool verbose, const std::string& onlyGroup, bool strictMin);
int Render(int argc, char** argv, int firstArg);
int LayoutInfo();
// Self test with real input through the XTest extension of the X server; false when that is not available.
bool RealInputTests(const std::function<void(const std::wstring&, bool)>& check);

}  // namespace hctest
