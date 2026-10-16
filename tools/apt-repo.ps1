# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Runs tools/apt-repo.sh on the Linux build machine and copies the result back (docs/LINUX.md, section 5):
#   1. tools\linux-sync.ps1 mirrors the repository into ~/<Name> on the machine,
#   2. tools/apt-repo.sh runs there; the secret key goes to it over the standard input of ssh, so it is
#      never written to the machine's disk (the script imports it into a temporary GnuPG home in tmpfs),
#   3. apt\ in this repository is replaced by apt/ from the machine.
# The commands run as the normal user over SSH and no window is ever shown. ASCII only (Windows PowerShell 5.1).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\apt-repo.ps1 -Key <file> [-Add <url>,<url>] [-Distro Ubuntu|Debian] [-Name hourcalc-apt] [-SshHost <host>]
# -Key: the armored secret signing key (kept outside the repository).
# -Add: .deb release assets to add (see tools/apt-repo.sh); without -Add the indexes are signed again.
# -Distro: Ubuntu by default; the machine needs gnupg with gpg-agent, dpkg-dev and curl.
# -SshHost: the SSH host to use; default hourcalc-debian or hourcalc-ubuntu (tools\linux-ssh.ps1).

param(
    [Parameter(Mandatory = $true)][string]$Key,
    [string[]]$Add = @(),
    [ValidateSet('Debian', 'Ubuntu')][string]$Distro = 'Ubuntu',
    [string]$Name = 'hourcalc-apt',
    [string]$SshHost = ''
)
if ($Name -notmatch '^[A-Za-z0-9._-]+$') { throw "Invalid -Name: $Name" }
# powershell -File passes "-Add a,b" as one string
$Add = @($Add | ForEach-Object { $_ -split ',' } | Where-Object { $_ })
foreach ($u in $Add) {
    if ($u -notmatch '^https://[A-Za-z0-9./_~%+-]+\.deb$') { throw "Invalid -Add URL: $u" }
}

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
. (Join-Path $PSScriptRoot 'linux-ssh.ps1')
$h = Get-LinuxHost $Distro $SshHost
$keyPath = (Resolve-Path -LiteralPath $Key).ProviderPath
# A key inside the repository would travel to the machine's disk with tools\linux-sync.ps1.
$rootFull = [System.IO.Path]::GetFullPath($root).TrimEnd('\') + '\'
if ([System.IO.Path]::GetFullPath($keyPath).StartsWith($rootFull, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "The key must be kept outside the repository: $keyPath"
}
$keyText = [System.IO.File]::ReadAllText($keyPath)

& (Join-Path $PSScriptRoot 'linux-sync.ps1') -Distro $Distro -Name $Name -SshHost $h

$addArgs = ($Add | ForEach-Object { "--add '$_'" }) -join ' '
$cmd = "cd ~/$Name && sh tools/apt-repo.sh --key - $addArgs"
# Windows PowerShell writes a pipe into a native program with [Console]::InputEncoding; on a UTF-8
# console that encoding puts a byte order mark in front, and gpg then finds no armor. Same code
# page, without the mark, for the length of the call.
$savedInput = [Console]::InputEncoding
try {
    [Console]::InputEncoding = New-Object System.Text.UTF8Encoding($false)
    $code = Invoke-Linux -SshHost $h -Script $cmd -InputText $keyText
} finally {
    [Console]::InputEncoding = $savedInput
}
$keyText = $null
if ($code -ne 0) { Write-Host "== tools/apt-repo.sh FAILED (exit code $code), apt\ is unchanged"; exit 1 }

$tmp = New-LinuxTempFolder 'hourcalc-apt'
try {
    $src = Copy-FromLinux $h "$Name/apt" $tmp
    & robocopy.exe $src (Join-Path $root 'apt') /MIR /NFL /NDL /NJH /NP /R:1 /W:1 | Out-Null
    if ($LASTEXITCODE -ge 8) { throw "robocopy failed with exit code $LASTEXITCODE" }
} finally {
    Remove-Item -LiteralPath $tmp -Recurse -Force -ErrorAction SilentlyContinue
}
Write-Host "== apt\ updated from ${h}:~/$Name/apt"
exit 0
