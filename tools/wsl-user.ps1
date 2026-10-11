# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Prints the default (non-root) user of a WSL distro, so the build scripts need no user name of their own.
# Text on stderr (for example "Failed to start the systemd user session" on a cold start) is ignored.
# ASCII only (Windows PowerShell 5.1).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\wsl-user.ps1 -Distro Debian|Ubuntu

param([Parameter(Mandatory = $true)][ValidateSet('Debian', 'Ubuntu')][string]$Distro)

$saved = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    $out = & wsl.exe -d $Distro --cd / -- id -un 2>$null
} finally {
    $ErrorActionPreference = $saved
}
$user = $out | ForEach-Object { ([string]$_ -replace "`0", '').Trim() } |
    Where-Object { $_ -match '^[a-z_][a-z0-9_-]*$' } | Select-Object -Last 1
if (-not $user) { throw "Could not determine the default user of WSL distro $Distro" }
if ($user -eq 'root') { throw "The default user of $Distro is root; HourCalc builds as a normal user only" }
$user
