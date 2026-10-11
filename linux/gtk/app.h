// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - the application: AdwApplication, the first window, command line.

#pragma once

#include <string>

#include "hcwindow.h"

namespace hcgtk {

struct AppOptions {
    GApplicationFlags flags = G_APPLICATION_DEFAULT_FLAGS;
    bool noSettings = false;                       // do not read or write the settings file (tests, measurements)
    void (*onActivate)() = nullptr;                // called when the application is activated, before the window is made
    void (*onWindow)(HcWindow*, void*) = nullptr;  // called with the first window, before it is presented
    void* user = nullptr;
};

// The renderer the window is painted with. GTK picks a GPU renderer (GL, or Vulkan from GTK 4.22). A calculator
// window gets nothing from it, and where the GPU is not usable (a virtual machine without a graphics device, a remote
// desktop, WSL) that renderer runs in software and costs time and memory. Measured with hourcalc-gtk-test
// --measure-startup under Xvfb (software GL / Vulkan), process start to first frame, medians of 7 runs:
//     Debian 13 (GTK 4.18):    GL 332-436 ms, 157 MiB      ->  cairo 227-254 ms, 109 MiB
//     Ubuntu 26.04 (GTK 4.22): Vulkan 643-778 ms, 201 MiB  ->  cairo 241-279 ms, 121 MiB
// (the ranges are separate measurements on the same machine; the toolkit alone, an empty libadwaita window, takes
// 180-230 ms with cairo.) Those numbers exist for the software case only; nothing was measured on a machine with a GPU,
// so nothing is forced there: HourCalc asks for the cairo renderer when no DRM render node (/dev/dri/renderD*) can be
// opened by the user, or LIBGL_ALWAYS_SOFTWARE is set, and otherwise leaves the choice to GTK. The user's own choice
// always wins: GSK_RENDERER, and HOURCALC_RENDERER=gtk (GTK's choice whatever the machine) or any other value of
// HOURCALC_RENDERER (passed on as GSK_RENDERER). Call it before GTK starts.
void ChooseRenderer();

// The pure part of that choice: the value for GSK_RENDERER ("" = leave it to GTK) from the two variables (null: not set)
// and whether the machine has no usable GPU (softwareOnly).
std::string RendererWish(const char* gskRenderer, const char* hourcalcRenderer, bool softwareOnly);

// A render node of the GPU that the user can open (a readable and writable renderD* file in the folder).
bool HasRenderNode(const char* dir);

// Runs the application until the last window is closed; returns the exit code.
int RunApp(int argc, char** argv, const AppOptions& opt);

}  // namespace hcgtk
