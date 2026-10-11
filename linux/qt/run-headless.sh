#!/bin/sh
# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs a command on a virtual X display (Xvfb) with the xcb platform of Qt, so that no window can ever reach a
# real desktop (under WSLg the Wayland and X displays of the session would put Linux windows on the Windows
# desktop). Used for the tests of the Qt front end that need the real platform plugin:
#
#   linux/qt/run-headless.sh ./hourcalc-qt-test --xcb-selftest
#   linux/qt/run-headless.sh ./hourcalc-qt-test --measure-startup 15 --xcb
#
# It never falls back to the display of the session: xvfb-run picks a free server number of its own and
# fails when it cannot start Xvfb. (The offscreen tests, --selftest and --render, need no display.)

if [ "$#" -eq 0 ]; then
    echo "usage: run-headless.sh command [args...]" >&2
    exit 2
fi

unset WAYLAND_DISPLAY WAYLAND_SOCKET
QT_QPA_PLATFORM=xcb
export QT_QPA_PLATFORM
# No screen reader, no session bus for the tests.
exec xvfb-run -a -s "-screen 0 1280x1024x24 -nolisten tcp" "$@"
