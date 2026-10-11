# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Dot-source this file to load the MSVC x64 build environment (cl, link, rc) into the current
# PowerShell session:   . "$PSScriptRoot\tools\msvc.ps1"
# Uses vswhere to find Visual Studio / Build Tools with the C++ toolset, then imports the
# environment that vcvars64.bat sets. ASCII only (Windows PowerShell 5.1).

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) { throw "vswhere.exe not found: $vswhere" }
$vsPath = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if (-not $vsPath) { throw 'No Visual Studio or Build Tools with the C++ x64 toolset found' }
$vcvars = Join-Path $vsPath 'VC\Auxiliary\Build\vcvars64.bat'
if (-not (Test-Path -LiteralPath $vcvars)) { throw "vcvars64.bat not found: $vcvars" }

if (-not $env:HOURCALC_MSVC_LOADED) {
    $lines = & cmd.exe /c "`"$vcvars`" >nul 2>&1 && set"
    if ($LASTEXITCODE -ne 0) { throw "vcvars64.bat failed ($LASTEXITCODE)" }
    foreach ($line in $lines) {
        $eq = $line.IndexOf('=')
        if ($eq -gt 0) { [System.Environment]::SetEnvironmentVariable($line.Substring(0, $eq), $line.Substring($eq + 1), 'Process') }
    }
    $env:HOURCALC_MSVC_LOADED = '1'
}
foreach ($tool in 'cl.exe', 'link.exe', 'rc.exe') {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) { throw "$tool not on PATH after vcvars64" }
}
