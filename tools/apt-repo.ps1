# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs tools/apt-repo.sh in a WSL distro and copies the result back (docs/LINUX.md, section 5):
#   1. tools\wsl-sync.ps1 mirrors the repository into ~/<Name> in the distro,
#   2. tools/apt-repo.sh runs there; the secret key goes to it over stdin, so it is never written
#      to the distro's disk (the script imports it into a temporary GnuPG home in tmpfs),
#   3. apt\ in this repository is replaced by apt/ from the distro.
# WSL commands run as the normal user and no window is ever shown. ASCII only (Windows PowerShell 5.1).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\apt-repo.ps1 -Key <file> [-Add <url>,<url>] [-Distro Ubuntu|Debian] [-Name hourcalc-apt] [-User <name>]
# -Key: the armored secret signing key (kept outside the repository).
# -Add: .deb release assets to add (see tools/apt-repo.sh); without -Add the indexes are signed again.
# -Distro: Ubuntu by default; the distro needs gnupg with gpg-agent, dpkg-dev and curl.
# -User: the Linux user whose home is used; default: the distro's default user (tools\wsl-user.ps1).

param(
    [Parameter(Mandatory = $true)][string]$Key,
    [string[]]$Add = @(),
    [ValidateSet('Debian', 'Ubuntu')][string]$Distro = 'Ubuntu',
    [string]$Name = 'hourcalc-apt',
    [string]$User = ''
)
if ($Name -notmatch '^[A-Za-z0-9._-]+$') { throw "Invalid -Name: $Name" }
# powershell -File passes "-Add a,b" as one string
$Add = @($Add | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
foreach ($u in $Add) {
    if ($u -notmatch '^https://[A-Za-z0-9./_~%+-]+\.deb$') { throw "Invalid -Add URL: $u" }
}

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$keyText = [System.IO.File]::ReadAllText((Resolve-Path -LiteralPath $Key).ProviderPath)

if (-not $User) { $User = & (Join-Path $PSScriptRoot 'wsl-user.ps1') -Distro $Distro }
& (Join-Path $PSScriptRoot 'wsl-sync.ps1') -Distro $Distro -Name $Name -User $User

$addArgs = ($Add | ForEach-Object { "--add '$_'" }) -join ' '
$cmd = "cd ~/$Name && sh tools/apt-repo.sh --key - $addArgs"
# Windows PowerShell writes a pipe into a native program with [Console]::InputEncoding; on a UTF-8
# console that encoding puts a byte order mark in front, and gpg then finds no armor. Same code
# page, without the mark, for the length of the call.
$savedInput = [Console]::InputEncoding
# stderr of the script is progress and diagnostics, only the exit code counts
$saved = $ErrorActionPreference
$ErrorActionPreference = 'Continue'
try {
    [Console]::InputEncoding = New-Object System.Text.UTF8Encoding($false)
    $keyText | & wsl.exe -d $Distro --cd / -- bash -lc $cmd
    $code = $LASTEXITCODE
} finally {
    $ErrorActionPreference = $saved
    [Console]::InputEncoding = $savedInput
}
if ($code -ne 0) { Write-Host "== tools/apt-repo.sh FAILED (exit code $code), apt\ is unchanged"; exit 1 }

$src = "\\wsl.localhost\$Distro\home\$User\$Name\apt"
& robocopy.exe $src (Join-Path $root 'apt') /MIR /NFL /NDL /NJH /NP /R:1 /W:1 | Out-Null
if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
Write-Host "== apt\ updated from ${Distro}:~/$Name/apt"
exit 0
