#!/bin/sh
# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs a command on a virtual X display (Xvfb) with the X11 backend of GTK, so that no window can ever
# reach a real desktop (under WSLg the Wayland and X displays of the session would put Linux windows on
# the Windows desktop). Used for the headless tests of the GTK front end:
#
#   linux/gtk/run-headless.sh ./hourcalc-gtk-test --selftest
#   linux/gtk/run-headless.sh ./hourcalc-gtk-test --render out/ --icon-svg assets/icon/app.svg
#   linux/gtk/run-headless.sh ./hourcalc-gtk-test --measure-startup
#
# It never falls back to the display of the session: xvfb-run picks a free server number of its own and
# fails when it cannot start Xvfb.

if [ "$#" -eq 0 ]; then
    echo "usage: run-headless.sh command [args...]" >&2
    exit 2
fi

unset WAYLAND_DISPLAY WAYLAND_SOCKET
GDK_BACKEND=x11
export GDK_BACKEND
# No session bus, no dconf, no screen reader for the tests.
exec xvfb-run -a -s "-screen 0 1280x1024x24 -nolisten tcp" "$@"
