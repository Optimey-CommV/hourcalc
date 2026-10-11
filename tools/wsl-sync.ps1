# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Mirrors the HourCalc repository into a WSL distro (~/hourcalc-src) so it can be built there:
# WSL cannot see the K: network drive. Leaves out build output, the logo rounds, the WPF legacy
# tree and snapshot folders, and restores the executable bits a copy from Windows loses.
# ASCII only (Windows PowerShell 5.1).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\wsl-sync.ps1 -Distro Debian|Ubuntu [-Name hourcalc-src] [-User <name>]
# -Name: the folder under the user's home (give every parallel worker its own).
# -User: the Linux user whose home is used; default: the distro's default user (tools\wsl-user.ps1).

param(
    [Parameter(Mandatory = $true)][ValidateSet('Debian', 'Ubuntu')][string]$Distro,
    [string]$Name = 'hourcalc-src',
    [string]$User = ''
)
if ($Name -notmatch '^[A-Za-z0-9._-]+$') { throw "Invalid -Name: $Name" }

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

# Runs wsl.exe and returns its exit code. Text on stderr never aborts the script: a cold start of a distro can print
# "wsl: Failed to start the systemd user session" (harmless), and Windows PowerShell 5.1 turns stderr text of a native
# command into an error record that $ErrorActionPreference = 'Stop' would make fatal. So inside this function the
# preference is 'Continue', both streams are shown as plain text (wsl.exe writes UTF-16 in places: NUL characters are
# dropped), and only the exit code counts - the caller decides what a non-zero code means.
function Invoke-Wsl([string[]]$WslArgs) {
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & wsl.exe @WslArgs 2>&1 | ForEach-Object {
            $line = if ($_ -is [System.Management.Automation.ErrorRecord]) { $_.Exception.Message } else { [string]$_ }
            $line = $line -replace "`0", ''
            if ($line.Trim().Length -gt 0) { Write-Host $line }
        }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    return $code
}

# Start the distro so its share exists (as the normal user, never root). Only the start matters, not what it prints
# or what exit code the warm-up call returns: the copy below fails by itself when the share is not there.
$null = Invoke-Wsl @('-d', $Distro, '--cd', '/', '--', 'true')

if (-not $User) { $User = & (Join-Path $PSScriptRoot 'wsl-user.ps1') -Distro $Distro }
$dest = "\\wsl.localhost\$Distro\home\$User\$Name"

$excludeDirs = @('obj', 'dist', 'logos', 'legacy', '.git', '__pycache__',
    (Join-Path $root 'docs\snapshots'), (Join-Path $root 'docs\snapshots-native'),
    (Join-Path $root 'docs\compare'), (Join-Path $root 'docs\review'), (Join-Path $root 'docs\review-native'),
    (Join-Path $root 'docs\review-theme'), (Join-Path $root 'docs\review-linux'))
$rcArgs = @($root, $dest, '/MIR', '/NFL', '/NDL', '/NJH', '/NP', '/R:1', '/W:1', '/XD') + $excludeDirs
& robocopy.exe @rcArgs | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }

# Executable bits: debian/rules and every shell script.
$fix = "cd ~/$Name && " + 'chmod -R u+rwX,go+rX . && { [ -f debian/rules ] && chmod +x debian/rules || true; } && find . -name "*.sh" -exec chmod +x {} +'
$fixCode = Invoke-Wsl @('-d', $Distro, '--cd', '/', '--', 'bash', '-lc', $fix)
if ($fixCode -ne 0) { throw "fixing permissions in $Distro failed ($fixCode)" }
Write-Host "== Synced $root -> ${Distro}:~/$Name"
