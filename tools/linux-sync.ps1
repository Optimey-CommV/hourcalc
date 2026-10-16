# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Mirrors the HourCalc repository into ~/<Name> on the Linux build machine (docs\LINUX.md, "Build environment"), so
# it can be built there: the machine cannot see the K: network drive. Leaves out build output, the logo rounds, the
# WPF legacy tree and snapshot folders, and restores the executable bits a copy from Windows loses.
# The copy travels as one tar archive (tar.exe of Windows, scp) and replaces ~/<Name> as a whole, so files deleted
# here disappear there too; the modification times are kept, so an existing build folder next to it stays valid.
# ASCII only (Windows PowerShell 5.1).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\linux-sync.ps1 -Distro Debian|Ubuntu [-Name hourcalc-src] [-SshHost <host>]
# -Name: the folder under the user's home (give every parallel worker its own).
# -SshHost: the SSH host to use; default hourcalc-debian or hourcalc-ubuntu (tools\linux-ssh.ps1).

param(
    [Parameter(Mandatory = $true)][ValidateSet('Debian', 'Ubuntu')][string]$Distro,
    [string]$Name = 'hourcalc-src',
    [string]$SshHost = ''
)
if ($Name -notmatch '^[A-Za-z0-9._-]+$') { throw "Invalid -Name: $Name" }

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'linux-ssh.ps1')
$h = Get-LinuxHost $Distro $SshHost

# Names left out wherever they are, and folders of docs\ left out by their path.
$exclude = @('obj', 'dist', 'logos', 'legacy', '.git', '__pycache__',
    'docs/snapshots', 'docs/snapshots-native', 'docs/compare', 'docs/review', 'docs/review-native',
    'docs/review-theme', 'docs/review-linux')
$tarArgs = @('-c', '-f')
$tmp = New-LinuxTempFolder 'hourcalc-sync'
try {
    $tar = Join-Path $tmp "$Name.tar"
    $tarArgs += $tar
    foreach ($e in $exclude) { $tarArgs += @('--exclude', $e) }
    $tarArgs += @('-C', $root, '.')
    & (Join-Path $env:SystemRoot 'System32\tar.exe') @tarArgs
    if ($LASTEXITCODE -ne 0) { throw "tar failed with exit code $LASTEXITCODE" }

    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $script:LinuxScp -q -B -o BatchMode=yes -o LogLevel=ERROR $tar "${h}:.hourcalc-sync-$Name.tar" 2>&1 | ForEach-Object { Write-LinuxLine $_ }
        $scpCode = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($scpCode -ne 0) { throw "copying the archive to $h failed (scp exit code $scpCode)" }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}

# Unpack into a new folder, fix the executable bits (debian/rules and every shell script), then swap it in.
$unpack = @"
set -e
cd ~
n='$Name'
rm -rf "`$n.new" "`$n.old"
mkdir "`$n.new"
tar -x -f ".hourcalc-sync-`$n.tar" -C "`$n.new" --no-same-owner --warning=no-unknown-keyword
rm -f ".hourcalc-sync-`$n.tar"
cd "`$n.new"
chmod -R u+rwX,go+rX .
if [ -f debian/rules ]; then chmod +x debian/rules; fi
find . -name '*.sh' -exec chmod +x {} +
cd ~
if [ -d "`$n" ]; then mv "`$n" "`$n.old"; fi
mv "`$n.new" "`$n"
rm -rf "`$n.old"
"@
$code = Invoke-Linux -SshHost $h -Script $unpack
if ($code -ne 0) { throw "unpacking on $h failed ($code)" }
Write-Host "== Synced $root -> ${h}:~/$Name"
