#!/bin/sh
# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Maintains the signed APT repository in apt/ (docs/LINUX.md, section 5), which
# .github/workflows/apt.yml publishes as https://apt.optimey.be. Only the metadata lives in git:
# the packages stay release assets, listed in apt/pool.txt with their SHA-256, and the workflow
# copies them into the site.
#
#   tools/apt-repo.sh --key FILE [--add URL]... [--url https://apt.optimey.be]
# --key: the armored secret signing key, or - to read it from stdin. It is kept outside the
#        repository; the script imports it into a temporary GnuPG home in tmpfs and removes that.
# --add: a .deb release asset to add (repeat for more). Its Debian version picks the suite
#        (table DISTS below). When the release has a SHA256SUMS.txt, the download must match it.
# --url: the address the .sources files point at.
# Without --add, the indexes are rebuilt and signed again from apt/pool.txt (after a key change,
# for example). Needs gnupg (with gpg-agent), dpkg-dev, curl and coreutils.

set -eu

# One line per distribution: version suffix, suite, name of the .sources file, description.
DISTS='deb13:trixie:debian13:Debian 13 (trixie)
ubuntu26.04:resolute:ubuntu26.04:Ubuntu 26.04 LTS (resolute)'

die() { echo "apt-repo: $*" >&2; exit 1; }
# dist KEY COLUMN: the COLUMN of the DISTS line whose suffix or suite is KEY
dist() { printf '%s\n' "$DISTS" | awk -F: -v k="$1" -v c="$2" '$1 == k || $2 == k { print $c }'; }
fetch() { curl -fsSL --retry 3 -o "$2" "$1"; }

key='' url='https://apt.optimey.be' adds=''
while [ $# -gt 0 ]; do
    case $1 in
        --key) [ $# -ge 2 ] || die "--key needs a value"; key=$2; shift 2 ;;
        --add) [ $# -ge 2 ] || die "--add needs a value"; adds="$adds $2"; shift 2 ;;
        --url) [ $# -ge 2 ] || die "--url needs a value"; url=$2; shift 2 ;;
        *) die "unknown option: $1" ;;
    esac
done
[ -n "$key" ] || die "--key is required"
for u in $adds; do
    case $u in *.deb) ;; *) die "not a .deb: $u" ;; esac
done

root=$(CDPATH='' cd -- "$(dirname -- "$0")/.." && pwd)
apt="$root/apt"
mkdir -p "$apt"

# Work directory in tmpfs where there is one: the secret key is imported there.
for tmp in "${XDG_RUNTIME_DIR:-}" /dev/shm /tmp; do
    [ -n "$tmp" ] && [ -d "$tmp" ] && [ -w "$tmp" ] && break
done
work=$(umask 077 && mktemp -d "$tmp/apt-repo.XXXXXX")
export GNUPGHOME="$work/gnupg"
cleanup() { gpgconf --kill gpg-agent 2>/dev/null || true; rm -rf "$work"; }
trap cleanup EXIT
trap 'exit 1' HUP INT TERM
(umask 077 && mkdir "$GNUPGHOME")

if [ "$key" = - ]; then
    gpg --batch --quiet --import
else
    [ -f "$key" ] || die "key file not found: $key"
    gpg --batch --quiet --import "$key"
fi
fpr=$(gpg --batch --with-colons --list-secret-keys | awk -F: '$1 == "fpr" { print $10; exit }')
[ -n "$fpr" ] || die "no secret key in $key"
echo "== Signing key $fpr"

# apt/pool.txt: suite, SHA-256, path in the repository, release asset URL.
mkdir "$work/dl"
if [ -f "$apt/pool.txt" ]; then
    cp "$apt/pool.txt" "$work/pool.txt"
else
    echo '# suite sha256 path url (written by tools/apt-repo.sh)' > "$work/pool.txt"
fi

for u in $adds; do
    asset=${u##*/}
    f="$work/dl/$asset"
    fetch "$u" "$f" || die "download failed: $u"
    if curl -fsSL -o "$work/sums" "${u%/*}/SHA256SUMS.txt" 2>/dev/null; then
        want=$(awk -v n="$asset" '$2 == n || $2 == "*" n { print $1 }' "$work/sums")
        [ -n "$want" ] || die "$asset is not in the SHA256SUMS.txt of its release"
        echo "$want  $f" | sha256sum -c --quiet - || die "$asset does not match SHA256SUMS.txt"
    fi
    pkg=$(dpkg-deb -f "$f" Package)
    ver=$(dpkg-deb -f "$f" Version)
    suite=$(dist "${ver##*~}" 2)
    [ -n "$suite" ] || die "$asset: no suite for version $ver"
    case $pkg in
        lib?*) prefix=$(printf %.4s "$pkg") ;;
        *) prefix=$(printf %.1s "$pkg") ;;
    esac
    path="pool/main/$prefix/$pkg/$asset"
    if awk -v p="$path" '$3 == p { found = 1 } END { exit !found }' "$work/pool.txt"; then
        die "$path is already in apt/pool.txt"
    fi
    sha=$(sha256sum "$f" | cut -d' ' -f1)
    echo "$suite $sha $path $u" >> "$work/pool.txt"
    echo "== Added $pkg $ver to $suite"
done

for suite in $(printf '%s\n' "$DISTS" | cut -d: -f2); do
    grep -q "^$suite " "$work/pool.txt" || continue
    tree="$work/tree/$suite"
    grep "^$suite " "$work/pool.txt" > "$work/entries"
    while read -r _ sha path u; do
        f="$work/dl/${path##*/}"
        [ -f "$f" ] || fetch "$u" "$f" || die "download failed: $u"
        echo "$sha  $f" | sha256sum -c --quiet - || die "${path##*/} does not match apt/pool.txt"
        mkdir -p "$tree/${path%/*}"
        cp "$f" "$tree/$path"
    done < "$work/entries"
    (cd "$tree" && dpkg-scanpackages --multiversion pool) > "$work/scan"
    # without the empty line after the last stanza, so the end-of-file hook leaves the signed file alone
    sed '${/^$/d;}' "$work/scan" > "$work/Packages"

    d="$apt/dists/$suite"
    rm -rf "$d"
    mkdir -p "$d/main/binary-amd64"
    cp "$work/Packages" "$d/main/binary-amd64/Packages"
    {
        echo "Origin: Optimey"
        echo "Label: Optimey"
        echo "Suite: $suite"
        echo "Codename: $suite"
        echo "Date: $(LC_ALL=C date -u '+%a, %d %b %Y %H:%M:%S UTC')"
        echo "Architectures: amd64"
        echo "Components: main"
        echo "Description: Optimey packages for $(dist "$suite" 4)"
        echo "SHA256:"
        for i in main/binary-amd64/Packages; do
            echo " $(sha256sum < "$d/$i" | cut -d' ' -f1) $(wc -c < "$d/$i" | tr -d ' ') $i"
        done
    } > "$d/Release"
    gpg --batch --yes --quiet --local-user "$fpr" --digest-algo SHA512 --clearsign \
        --output "$d/InRelease" "$d/Release"
    gpg --batch --yes --quiet --local-user "$fpr" --digest-algo SHA512 --armor --detach-sign \
        --output "$d/Release.gpg" "$d/Release"
    gpg --batch --quiet --verify "$d/InRelease" 2>/dev/null || die "InRelease of $suite does not verify"
    gpg --batch --quiet --verify "$d/Release.gpg" "$d/Release" 2>/dev/null || die "Release.gpg of $suite does not verify"
    echo "== Signed $suite: $(grep -c '^Package:' "$d/main/binary-amd64/Packages") package(s)"
done

# The public key, alone and inside one .sources file per distribution (deb822, key inline).
gpg --batch --armor --export "$fpr" > "$apt/optimey.asc"
for suite in $(printf '%s\n' "$DISTS" | cut -d: -f2); do
    [ -d "$apt/dists/$suite" ] || continue
    {
        echo "# Optimey APT repository for $(dist "$suite" 4): $url"
        echo "# Save as /etc/apt/sources.list.d/optimey.sources"
        echo "Types: deb"
        echo "URIs: $url"
        echo "Suites: $suite"
        echo "Components: main"
        echo "Architectures: amd64"
        echo "Signed-By:"
        sed -e 's/^$/./' -e 's/^/ /' "$apt/optimey.asc"
    } > "$apt/$(dist "$suite" 3).sources"
done

cp "$work/pool.txt" "$apt/pool.txt"
echo "== apt/ is up to date; commit it on a branch and open a pull request"
