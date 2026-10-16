# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Builds the HourCalc .deb on the Linux build machine and copies it back (docs/LINUX.md, section 4):
#   1. tools\linux-sync.ps1 mirrors the repository into ~/<Name> on the machine,
#   2. tools/build-deb.sh runs there (dpkg-buildpackage, lintian, desktop-file-validate,
#      appstreamcli validate, apt-get install --simulate, the contents check),
#   3. the .deb and the reports (lintian.txt, checks.txt, contents.txt, info.txt) are copied
#      to dist\linux\debian13\ (Debian 13) or dist\linux\ubuntu26.04\ (Ubuntu 26.04).
# The exit code is not 0 when a build or a check failed; the results are copied anyway.
# Nothing is signed, the commands run as the normal user over SSH, and no window is ever shown.
# ASCII only (Windows PowerShell 5.1).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\build-deb.ps1 [-Distro Debian|Ubuntu|Both] [-Name hourcalc-src] [-SshHost <host>] [-Jobs 4] [-SkipSync]
# -SshHost: the SSH host to use (one -Distro only); default hourcalc-debian or hourcalc-ubuntu (tools\linux-ssh.ps1).
# -Name: the folder under the user's home (give every parallel worker its own); the build
#        happens in ~/<Name>-deb next to it.
# -Jobs: parallel compile jobs (HC_JOBS); the Debian machine has 4 cores and the Ubuntu one 2 (docs\LINUX.md).
# -SkipSync: build what is already in ~/<Name> (it was synced by hand).
# -Tests: also build the test programs of the front ends and run their headless tests during the package
#         build (HOURCALC_DEB_TESTS=1, see debian/rules): the launcher test, the GTK self test and the Qt tests under
#         Xvfb. Needs xvfb and xauth on the machine; where libXtst is not installed the real-input tests report that
#         they did not run.

param(
    [ValidateSet('Debian', 'Ubuntu', 'Both')][string]$Distro = 'Both',
    [string]$Name = 'hourcalc-src',
    [string]$SshHost = '',
    [ValidateRange(1, 8)][int]$Jobs = 4,
    [switch]$SkipSync,
    [switch]$Tests
)
if ($Name -notmatch '^[A-Za-z0-9._-]+$') { throw "Invalid -Name: $Name" }
if ($SshHost -and $Distro -eq 'Both') { throw '-SshHost needs one -Distro (Debian or Ubuntu)' }

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$sync = Join-Path $PSScriptRoot 'linux-sync.ps1'
. (Join-Path $PSScriptRoot 'linux-ssh.ps1')

# distro -> folder under dist\linux
$outNames = @{ 'Debian' = 'debian13'; 'Ubuntu' = 'ubuntu26.04' }
$distros = if ($Distro -eq 'Both') { @('Debian', 'Ubuntu') } else { @($Distro) }

$failed = @()
foreach ($d in $distros) {
    Write-Host "==== $d"
    $h = Get-LinuxHost $d $SshHost
    if (-not $SkipSync) { & $sync -Distro $d -Name $Name -SshHost $h }

    $testEnv = if ($Tests) { 'HOURCALC_DEB_TESTS=1 ' } else { '' }
    $code = Invoke-Linux -SshHost $h -Script "cd ~/$Name && ${testEnv}HC_JOBS=$Jobs bash tools/build-deb.sh"

    $dst = Join-Path $root ("dist\linux\" + $outNames[$d])
    $hasResult = (Invoke-Linux -SshHost $h -Script "test -d ~/$Name-deb/result") -eq 0
    if ($hasResult) {
        $tmp = New-LinuxTempFolder 'hourcalc-deb'
        try {
            $src = Copy-FromLinux $h "$Name-deb/result" $tmp
            New-Item -ItemType Directory -Force -Path $dst | Out-Null
            # only what a build writes: the previous results of this distro are replaced
            Get-ChildItem -LiteralPath $dst -File | Where-Object { $_.Name -like 'hourcalc*' -or $_.Name -in @('lintian.txt', 'checks.txt', 'contents.txt', 'info.txt') } | Remove-Item -Force
            foreach ($f in Get-ChildItem -LiteralPath $src -File) {
                # the .changes and .buildinfo files and the debug symbols stay on the machine
                if ($f.Name -like '*.changes' -or $f.Name -like '*.buildinfo' -or $f.Name -like '*dbgsym*') { continue }
                Copy-Item -LiteralPath $f.FullName -Destination $dst -Force
            }
        } finally {
            Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
        }
        Write-Host "== Results of $d copied to $dst"
        Get-ChildItem -LiteralPath $dst -File | ForEach-Object { Write-Host ("   {0}  ({1:N0} bytes)" -f $_.Name, $_.Length) }
    } else {
        Write-Host "== No results found in ${h}:~/$Name-deb/result"
    }
    if ($code -ne 0) {
        Write-Host "== $d : the build or a check FAILED (exit code $code)"
        $failed += $d
    }
}

if ($failed.Count -gt 0) { Write-Host ("== FAILED: " + ($failed -join ', ')); exit 1 }
Write-Host "== OK"
exit 0
