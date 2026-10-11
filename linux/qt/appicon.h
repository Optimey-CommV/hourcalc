// SPDX-FileCopyrightText: 2026 Optimey CommV
// SPDX-License-Identifier: GPL-3.0-or-later

// The application icon. On an installed system it comes from the icon theme (the package installs the
// hicolor icons under the application id); in a build tree, where nothing is installed, the logo is
// painted from its geometry (assets/icon/app.svg) so the window, the About dialog and the renders
// still have it. No Qt SVG module is needed for that.

#pragma once

#include <QIcon>
#include <QPixmap>

namespace hcqt {

// One frame of the painted logo, 'px' device pixels square (transparent background).
QPixmap paintAppIcon(int px);

// The theme icon of the application id, or the painted logo.
QIcon appIcon();

}  // namespace hcqt
