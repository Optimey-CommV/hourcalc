// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// HourCalc GTK front end - the identity of the application. meson writes hcgtk_config.h (HC_APP_ID, HC_VERSION,
// HC_SOURCE_URL from the root meson.build, where they are defined once); the defaults keep a stray compile working.

#pragma once

#if defined(__has_include)
#if __has_include("hcgtk_config.h")
#include "hcgtk_config.h"
#endif
#endif

#ifndef HC_APP_ID
#define HC_APP_ID "io.github.Optimey_CommV.HourCalc"
#endif
#ifndef HC_VERSION
#define HC_VERSION "1.0.0"
#endif
#ifndef HC_SOURCE_URL
#define HC_SOURCE_URL "https://github.com/Optimey-CommV/hourcalc"
#endif
