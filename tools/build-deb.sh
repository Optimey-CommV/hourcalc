#!/bin/bash
# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the HourCalc .deb for the distro this runs on, and checks it (docs/LINUX.md,
# section 4 and acceptance 5).
#
#   bash tools/build-deb.sh          from the copy of the repository on the build machine
#
# Normally started by tools\build-deb.ps1 over SSH, which copies the repository there first
# (tools\linux-sync.ps1) and copies the results back to dist\linux\<distro>\.
#
# What it does:
#   - works in a clean copy of the tree, <repo>-deb/hourcalc-<version>, next to the repository
#     (the repository copy itself is not touched);
#   - writes the distro-specific first line of debian/changelog:
#         Debian 13 (trixie)        1.0.0-1~deb13
#         Ubuntu 26.04 (resolute)   1.0.0-1~ubuntu26.04
#     (the upstream version comes from the root meson.build, the Debian revision from the
#     committed debian/changelog);
#   - dpkg-buildpackage -us -uc -b (never signs anything);
#   - lintian (errors and warnings fail the script), desktop-file-validate and
#     appstreamcli validate on the files of the package, apt-get install --simulate, and a
#     comparison of the package contents with the layout of docs/LINUX.md section 4.
# Results go to <repo>-deb/result/: the .deb, lintian.txt, checks.txt and contents.txt.
# The exit status is 0 only when every check passed.
#
# HC_JOBS (default 4) is the number of parallel compile jobs; the Debian build machine has 4
# cores and the Ubuntu one 2, and other work runs next to a build.

set -euo pipefail

root=$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)
jobs=${HC_JOBS:-4}

die() {
    echo "build-deb.sh: $*" >&2
    exit 2
}

# ---- which distro ------------------------------------------------------------------------
[ -r /etc/os-release ] || die "no /etc/os-release"
# shellcheck disable=SC1091
. /etc/os-release
case "${ID:-}:${VERSION_ID:-}" in
    debian:13) suffix=deb13 ;;
    ubuntu:26.04) suffix=ubuntu26.04 ;;
    *) die "unsupported distro ${ID:-?} ${VERSION_ID:-?} (supported: Debian 13, Ubuntu 26.04)" ;;
esac
codename=${VERSION_CODENAME:?no VERSION_CODENAME in /etc/os-release}

# ---- the version and the application id come from the root meson.build ---------------------
upstream=$(sed -n "s/^[[:space:]]*version[[:space:]]*:[[:space:]]*'\([^']*\)'.*/\1/p" "$root/meson.build" | head -n 1)
app_id=$(sed -n "s/^app_id[[:space:]]*=[[:space:]]*'\([^']*\)'.*/\1/p" "$root/meson.build" | head -n 1)
[ -n "$upstream" ] || die "no project version in $root/meson.build"
[ -n "$app_id" ] || die "no app_id in $root/meson.build"

# ---- a clean work tree ---------------------------------------------------------------------
out="$(dirname "$root")/$(basename "$root")-deb"
case "$out" in
    */?*-deb) ;;
    *) die "unexpected work directory $out" ;;
esac
tree="$out/hourcalc-$upstream"
result="$out/result"
rm -rf "$out"
mkdir -p "$tree" "$result"
(cd "$root" && tar --exclude=./.git -cf - .) | tar -C "$tree" -xf -

# ---- debian/changelog: the version and distribution of this distro --------------------------
cl="$tree/debian/changelog"
current=$(dpkg-parsechangelog -l"$cl" -SVersion)
base=${current%%~*}
case "$base" in
    "$upstream"-*) ;;
    *) die "debian/changelog is at $current but meson.build says $upstream: add a changelog entry" ;;
esac
version="$upstream-${base#"$upstream"-}~$suffix"
sed -i "1s/^hourcalc ([^)]*) [^;]*;/hourcalc ($version) $codename;/" "$cl"
[ "$(dpkg-parsechangelog -l"$cl" -SVersion)" = "$version" ] || die "could not set the changelog version to $version"
echo "== hourcalc $version for $codename, work tree $tree"

# ---- build -----------------------------------------------------------------------------------
export DEB_BUILD_OPTIONS="${DEB_BUILD_OPTIONS:+$DEB_BUILD_OPTIONS }parallel=$jobs"
(cd "$tree" && dpkg-buildpackage -us -uc -b)

deb="$out/hourcalc_${version}_amd64.deb"
[ -f "$deb" ] || die "the build did not produce $deb"
# lintian reads the .changes file and the .buildinfo file it names, so both go along
for f in "$out/hourcalc_${version}_amd64".{deb,changes,buildinfo}; do
    mv "$f" "$result/"
done
deb="$result/hourcalc_${version}_amd64.deb"
changes="$result/hourcalc_${version}_amd64.changes"
for f in "$out"/hourcalc-dbgsym_*; do
    if [ -e "$f" ]; then mv "$f" "$result/"; fi
done

# ---- checks ------------------------------------------------------------------------------------
checks="$result/checks.txt"
lintian_log="$result/lintian.txt"
contents="$result/contents.txt"
: >"$checks"
: >"$lintian_log"
failed=()
log=$checks

# gate <name> <command...>: runs the command, logs its output, a non-zero status is a failure.
gate() {
    local name=$1 output rc=0
    shift
    output=$("$@" 2>&1) || rc=$?
    {
        echo "=== $name"
        echo "\$ $*"
        [ -z "$output" ] || printf '%s\n' "$output"
        echo "-> exit status $rc"
        echo
    } | tee -a "$log"
    if [ "$rc" -ne 0 ]; then
        failed+=("$name")
    fi
}

# info <name> <command...>: like gate, but only for the record.
info() {
    local name=$1 output rc=0
    shift
    output=$("$@" 2>&1) || rc=$?
    {
        echo "=== $name (informational)"
        echo "\$ $*"
        [ -z "$output" ] || printf '%s\n' "$output"
        echo "-> exit status $rc"
        echo
    } | tee -a "$log"
}

# no output lines of the given kinds (appstreamcli exits 0 on warnings)
no_issue_lines() { # <pattern> <command...>
    local pattern=$1 output rc=0
    shift
    output=$("$@" 2>&1) || rc=$?
    printf '%s\n' "$output"
    if printf '%s\n' "$output" | grep -Eq "$pattern"; then rc=1; fi
    return "$rc"
}

# --- lintian
{
    echo "lintian $(lintian --version | sed 's/^Lintian v//') on $PRETTY_NAME, package $(basename "$deb")"
    echo
} | tee -a "$lintian_log"
log=$lintian_log
gate "lintian (errors and warnings fail)" lintian --fail-on error,warning "$changes"
info "lintian --pedantic -I" lintian --pedantic --display-info "$changes"
log=$checks

# --- the files of the package, as installed
stage=$(mktemp -d "${TMPDIR:-/tmp}/hourcalc-stage.XXXXXX")
trap 'rm -rf "$stage"' EXIT
dpkg-deb -x "$deb" "$stage"

{
    basename "$deb"
    dpkg-deb --info "$deb" | sed -n '/^ Package:/,$p'
} >"$result/info.txt"
dpkg-deb --contents "$deb" >"$contents"
echo "== contents of $(basename "$deb")"
cat "$contents"
echo

desktop_file="$stage/usr/share/applications/$app_id.desktop"
metainfo_file="$stage/usr/share/metainfo/$app_id.metainfo.xml"
gate "desktop-file-validate" desktop-file-validate "$desktop_file"
gate "appstreamcli validate" no_issue_lines '^[EW]:' appstreamcli validate --no-net "$metainfo_file"
info "appstreamcli validate --pedantic" appstreamcli validate --no-net --pedantic "$metainfo_file"
gate "appstreamcli validate-tree" no_issue_lines '^[EW]:' appstreamcli validate-tree --no-net "$stage"

launcher_ok() {
    sh -n "$stage/usr/bin/hourcalc" && [ "$(head -n 1 "$stage/usr/bin/hourcalc")" = '#!/bin/sh' ] &&
        echo "usr/bin/hourcalc is a POSIX shell script that parses"
}
apt_simulate() { # the package resolves on this distro (a simulation, as the normal user)
    local output
    output=$(apt-get install --simulate "$deb") || {
        printf '%s\n' "$output"
        return 1
    }
    printf '%s\n' "$output"
    printf '%s\n' "$output" | grep -q '^Inst hourcalc '
}
gate "launcher syntax and first line" launcher_ok
gate "apt-get install --simulate" apt_simulate

# --- the front ends of the extracted package (docs/LINUX.md, acceptance 5): the shipped files themselves run headless.
# Both answer --version; each is then started for a few seconds on a virtual display (GTK under Xvfb on its X11 backend,
# Qt on the offscreen platform) and must still be running when it is stopped (exit status 124 of timeout), with a
# settings folder of its own, so nothing of the user's is touched and nothing is written without a change.
# The self tests of the front ends (HOURCALC_DEB_TESTS=1) run in the build itself, on the build tree; see debian/rules.
shipped_front_ends() {
    local libexec="$stage/usr/libexec/hourcalc" cfg rc=0 name out
    cfg=$(mktemp -d "${TMPDIR:-/tmp}/hourcalc-shipped.XXXXXX")
    for name in gtk qt; do
        out=$("$libexec/hourcalc-$name" --version 2>&1) || { echo "hourcalc-$name --version failed: $out"; rc=1; continue; }
        if [ "$out" != "hourcalc-$name $upstream" ]; then
            echo "hourcalc-$name --version printed '$out', expected 'hourcalc-$name $upstream'"
            rc=1
        else
            echo "hourcalc-$name --version: $out"
        fi
    done
    # no D-Bus session, no accessibility bus, nothing from the real desktop
    export XDG_CONFIG_HOME="$cfg" GTK_A11Y=none GSETTINGS_BACKEND=memory
    unset WAYLAND_DISPLAY
    if command -v xvfb-run >/dev/null 2>&1; then
        local status=0
        GDK_BACKEND=x11 timeout 4 xvfb-run -a "$libexec/hourcalc-gtk" >"$cfg/gtk.out" 2>&1 || status=$?
        if [ "$status" -eq 124 ]; then echo "hourcalc-gtk started on Xvfb and was still running after 4 s"
        else echo "hourcalc-gtk on Xvfb ended with status $status: $(head -c 400 "$cfg/gtk.out")"; rc=1; fi
    else
        echo "xvfb-run is not installed: the GTK start was not tried"
    fi
    local qt_status=0
    QT_QPA_PLATFORM=offscreen timeout 4 "$libexec/hourcalc-qt" >"$cfg/qt.out" 2>&1 || qt_status=$?
    if [ "$qt_status" -eq 124 ]; then echo "hourcalc-qt started (offscreen) and was still running after 4 s"
    else echo "hourcalc-qt (offscreen) ended with status $qt_status: $(head -c 400 "$cfg/qt.out")"; rc=1; fi
    if find "$cfg" -name '*.ini' -o -name '*.tmp' | grep -q .; then
        echo "a settings file was written although no setting was changed"
        rc=1
    fi
    rm -rf "$cfg"
    return "$rc"
}
gate "the shipped front ends start headless" shipped_front_ends

# --- the layout of docs/LINUX.md, section 4: exactly these files, nothing else
expected=(
    "./usr/bin/hourcalc"
    "./usr/libexec/hourcalc/hourcalc-gtk"
    "./usr/libexec/hourcalc/hourcalc-qt"
    "./usr/share/applications/$app_id.desktop"
    "./usr/share/metainfo/$app_id.metainfo.xml"
    "./usr/share/icons/hicolor/scalable/apps/$app_id.svg"
    "./usr/share/man/man1/hourcalc.1.gz"
    "./usr/share/doc/hourcalc/copyright"
    "./usr/share/doc/hourcalc/changelog.Debian.gz"
)
for size in 16 24 32 48 64 128 256; do
    expected+=("./usr/share/icons/hicolor/${size}x${size}/apps/$app_id.png")
done
# regular files and symlinks only (a directory entry ends in a slash), the path is the last
# column of "dpkg-deb --contents"
actual=$(awk '$1 !~ /^d/ { sub(/ -> .*/, ""); print $NF }' "$contents" | LC_ALL=C sort)
want=$(printf '%s\n' "${expected[@]}" | LC_ALL=C sort)
layout() {
    local missing extra
    missing=$(comm -13 <(printf '%s\n' "$actual") <(printf '%s\n' "$want"))
    extra=$(comm -23 <(printf '%s\n' "$actual") <(printf '%s\n' "$want"))
    [ -z "$missing" ] || printf 'missing from the package:\n%s\n' "$missing"
    [ -z "$extra" ] || printf 'not in docs/LINUX.md section 4:\n%s\n' "$extra"
    [ -z "$missing" ] && [ -z "$extra" ] && echo "the package contains exactly the $(printf '%s\n' "$want" | wc -l) files of section 4"
}
modes() {
    local bad=0 path mode
    for path in ./usr/bin/hourcalc ./usr/libexec/hourcalc/hourcalc-gtk ./usr/libexec/hourcalc/hourcalc-qt; do
        mode=$(awk -v p="$path" '$NF == p { print $1, $2 }' "$contents")
        if [ "$mode" != "-rwxr-xr-x root/root" ]; then
            echo "$path: '$mode', expected '-rwxr-xr-x root/root'"
            bad=1
        fi
    done
    [ "$bad" -eq 0 ] || return 1
    echo "the launcher and the front ends are executable, owned by root"
}
gate "package contents match docs/LINUX.md section 4" layout
gate "modes and owners" modes

# ---- summary --------------------------------------------------------------------------------------
{
    echo "== $(basename "$deb"): ${#failed[@]} check(s) failed"
    for name in "${failed[@]}"; do
        echo "   FAILED: $name"
    done
    echo "   results in $result"
} | tee -a "$checks"
[ "${#failed[@]}" -eq 0 ]
