#!/bin/sh
# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Test of the launcher (linux/data/hourcalc.in) with two dummy front ends: which one starts
# on which desktop, the HOURCALC_UI override, the fallback when one is missing, the
# arguments and the exit status. Needs nothing but a POSIX shell. Run by "meson test".
#
#   sh test-launcher.sh <path of the built launcher>

launcher="$1"
if [ ! -f "$launcher" ]; then
    echo "usage: sh test-launcher.sh <launcher>" >&2
    exit 2
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/hourcalc-launcher-test.XXXXXX") || exit 2
trap 'rm -rf "$work"' EXIT HUP INT TERM
libexec="$work/libexec"
mkdir "$libexec"

# The installed launcher has its directory built in and takes none from the environment. The test runs a copy with
# that one line (libexecdir="...") pointing at the dummy front ends; everything else is the script as installed.
sed "s|^libexecdir=.*\$|libexecdir=\"$libexec\"|" "$launcher" >"$work/hourcalc"
if ! grep -q "^libexecdir=\"$libexec\"\$" "$work/hourcalc" || [ "$(diff "$launcher" "$work/hourcalc" | grep -c '^>')" -ne 1 ]; then
    echo "the test copy of the launcher differs in more than the libexecdir line" >&2
    exit 2
fi
launcher_under_test="$work/hourcalc"

make_dummy() { # name exit-status
    {
        echo '#!/bin/sh'
        echo "echo \"$1\" \"\$#\" \"\$@\""
        echo "exit $2"
    } >"$libexec/hourcalc-$1"
    chmod +x "$libexec/hourcalc-$1"
}
make_dummy gtk 0
make_dummy qt 0

failures=0
checks=0

# run <desktop> <ui> [args...]: prints the output of the launcher (stdout and stderr) and
# its exit status on a last line "status=N". An empty <desktop> or <ui> means "not set".
run() {
    desktop="$1"
    ui="$2"
    shift 2
    out=$(
        unset XDG_CURRENT_DESKTOP HOURCALC_UI
        [ -n "$desktop" ] && XDG_CURRENT_DESKTOP="$desktop" && export XDG_CURRENT_DESKTOP
        [ -n "$ui" ] && HOURCALC_UI="$ui" && export HOURCALC_UI
        sh "$launcher_under_test" "$@" 2>&1
        echo "status=$?"
    )
    printf '%s\n' "$out"
}

expect() { # name expected-output (as printed by run) actual
    checks=$((checks + 1))
    if [ "$2" != "$3" ]; then
        failures=$((failures + 1))
        echo "FAIL $1"
        echo "  expected: $2"
        echo "  actual:   $3"
    fi
}

nl='
'

expect "no desktop -> gtk" "gtk 0${nl}status=0" "$(run '' '')"
expect "GNOME -> gtk" "gtk 0${nl}status=0" "$(run GNOME '')"
expect "ubuntu:GNOME -> gtk" "gtk 0${nl}status=0" "$(run ubuntu:GNOME '')"
expect "XFCE -> gtk" "gtk 0${nl}status=0" "$(run XFCE '')"
expect "KDE -> qt" "qt 0${nl}status=0" "$(run KDE '')"
expect "KDE in a list -> qt" "qt 0${nl}status=0" "$(run X-Foo:KDE '')"
expect "kde lower case -> qt" "qt 0${nl}status=0" "$(run kde '')"
expect "KDE, HOURCALC_UI=gtk -> gtk" "gtk 0${nl}status=0" "$(run KDE gtk)"
expect "GNOME, HOURCALC_UI=qt -> qt" "qt 0${nl}status=0" "$(run GNOME qt)"
expect "no desktop, HOURCALC_UI=qt -> qt" "qt 0${nl}status=0" "$(run '' qt)"
expect "HOURCALC_UI in upper case -> gtk on KDE" "gtk 0${nl}status=0" "$(run KDE GTK)"
expect "HOURCALC_UI in mixed case -> qt on GNOME" "qt 0${nl}status=0" "$(run GNOME Qt)"
expect "bogus HOURCALC_UI on KDE -> qt and a note" \
    "hourcalc: ignoring HOURCALC_UI='bogus' (use gtk or qt)${nl}qt 0${nl}status=0" "$(run KDE bogus)"
expect "arguments are passed unchanged" "gtk 3 a b c  d${nl}status=0" \
    "$(run GNOME '' 'a b' c ' d')"

# an exit status passes through
make_dummy gtk 3
expect "exit status of the front end" "gtk 0${nl}status=3" "$(run GNOME '')"
make_dummy gtk 0

# the environment cannot point the installed launcher at another directory
other="$work/other"
mkdir "$other"
for name in gtk qt; do
    printf '#!/bin/sh\necho "other %s"\n' "$name" >"$other/hourcalc-$name"
    chmod +x "$other/hourcalc-$name"
done
HOURCALC_LIBEXECDIR="$other"
export HOURCALC_LIBEXECDIR
expect "HOURCALC_LIBEXECDIR is ignored" "gtk 0${nl}status=0" "$(run GNOME '')"
unset HOURCALC_LIBEXECDIR

# one front end missing
mv "$libexec/hourcalc-qt" "$work/qt.away"
expect "qt missing on KDE -> gtk" "gtk 0${nl}status=0" "$(run KDE '')"
expect "qt asked for but missing -> gtk and a note" \
    "hourcalc: the qt front end is not installed, using gtk${nl}gtk 0${nl}status=0" "$(run GNOME qt)"
mv "$work/qt.away" "$libexec/hourcalc-qt"

mv "$libexec/hourcalc-gtk" "$work/gtk.away"
expect "gtk missing on GNOME -> qt" "qt 0${nl}status=0" "$(run GNOME '')"
expect "gtk asked for but missing -> qt and a note" \
    "hourcalc: the gtk front end is not installed, using qt${nl}qt 0${nl}status=0" "$(run KDE gtk)"

# both missing
mv "$libexec/hourcalc-qt" "$work/qt.away"
expect "no front end -> 127" \
    "hourcalc: no front end found in $libexec${nl}status=127" "$(run GNOME '')"

if [ "$failures" -ne 0 ]; then
    echo "launcher test: $failures of $checks checks FAILED"
    exit 1
fi
echo "launcher test: $checks checks passed"
