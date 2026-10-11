# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HourCalc core build. ASCII only: Windows PowerShell 5.1 reads BOM-less files as ANSI.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File build-core.ps1 [-SkipTests] [-Quick] [-Full]
#
# 1. native\core\*.cpp -> obj\core\hc_core.lib (lib /LTCG; the app links it)
# With -SkipTests that is all: the exe needs only the library (the source archive in the exe has no native\cli
# and no tests\, and a build from it passes -SkipTests). Otherwise:
# 2. native\cli\hc_cli.cpp -> obj\core\hc_cli.exe (console front end of the differential test, line protocol)
# 3. tests\native\core_tests.cpp -> obj\core\core_tests.exe (C++ unit tests)
# 4.-7. tests\core-tests.ps1: run core_tests.exe, the C# unit tests of the reference (tests\Tests.cs) and the
#    differential tests, native core against the C# reference (tests\Differential.cs and the adversarial
#    tests\DifferentialReview.cs, the second one as a sample unless -Full is given; -Quick runs smaller samples).
# Needs Visual Studio Build Tools 2022 (C++ x64) and the Windows SDK; the tests also need the .NET SDK (Roslyn csc)
# plus the .NET Framework 4.8 that ships with Windows. Nothing is downloaded.

param(
    [switch]$SkipTests,
    [switch]$Quick,
    [switch]$Full
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
. (Join-Path $root 'tools\msvc.ps1')

$obj = Join-Path $root 'obj\core'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$rsp = '@' + (Join-Path $root 'tools\cflags.rsp')
$objDir = $obj.Replace('\', '/') + '/'     # /Fo wants a trailing slash; a backslash before the closing quote would swallow it
$sw = [System.Diagnostics.Stopwatch]::StartNew()

function Invoke-Native([string]$What, [scriptblock]$Cmd) {
    & $Cmd
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit code $LASTEXITCODE)" }
}

# Compile (and link) from inside the repository root so that the diagnostics show relative paths.
Push-Location $root
try {
    # 1. The core library.
    Write-Host '== Core library'
    # platform_posix.cpp is the Linux half of the platform layer (meson builds it there); Windows has platform_win.cpp.
    $coreSources = @(Get-ChildItem -LiteralPath (Join-Path $root 'native\core') -Filter '*.cpp' | Where-Object { $_.Name -ne 'platform_posix.cpp' } | Sort-Object Name | ForEach-Object { $_.FullName })
    if ($coreSources.Count -eq 0) { throw 'No sources in native\core' }
    Get-ChildItem -LiteralPath $obj -Filter '*.obj' -ErrorAction SilentlyContinue | Remove-Item -Force
    Invoke-Native 'core compile' { & cl $rsp /MP /c "/Fo$objDir" @coreSources }
    $coreObjects = @($coreSources | ForEach-Object { Join-Path $obj ([System.IO.Path]::GetFileNameWithoutExtension($_) + '.obj') })
    $lib = Join-Path $obj 'hc_core.lib'
    $libNew = Join-Path $obj 'hc_core.new.lib'
    Invoke-Native 'lib' { & lib /nologo /LTCG "/OUT:$libNew" @coreObjects }
    # Replace the library in one step: the app build may be linking it at this moment.
    for ($try = 1; ; $try++) {
        try { Move-Item -LiteralPath $libNew -Destination $lib -Force; break }
        catch { if ($try -ge 20) { throw }; Start-Sleep -Milliseconds 500 }
    }

    # 2. The command line front end and 3. the C++ unit tests (and the state reporting front end of the review),
    # all linked against the library. Only the tests use them, so -SkipTests builds none of them.
    $programs = @()
    if (-not $SkipTests) {
        $programs = @(
            @{ Name = 'hc_cli'; Source = 'native\cli\hc_cli.cpp'; Required = $true },
            @{ Name = 'core_tests'; Source = 'tests\native\core_tests.cpp'; Required = $false },
            @{ Name = 'review_state_cli'; Source = 'tests\native\review_state_cli.cpp'; Required = $false })
    }
    foreach ($exe in $programs) {
        $source = Join-Path $root $exe.Source
        if (-not (Test-Path -LiteralPath $source)) {
            if ($exe.Required) { throw "Missing $source" }
            Write-Host ("== {0}: no {1} here, skipped" -f $exe.Name, $exe.Source)
            continue
        }
        Write-Host ("== {0}" -f $exe.Name)
        $out = Join-Path $obj ($exe.Name + '.exe')
        $o = Join-Path $obj ($exe.Name + '.obj')
        Invoke-Native ($exe.Name + ' build') {
            & cl $rsp "/Fo$o" "/Fe$out" $source /link /LTCG /OPT:REF /OPT:ICF /SUBSYSTEM:CONSOLE $lib
        }
    }
} finally {
    Pop-Location
}
Write-Host ("== Core built in {0:N1} s: {1}" -f $sw.Elapsed.TotalSeconds, $lib)

if ($SkipTests) { return }

# ---- Tests ----------------------------------------------------------------------------------
# They are in tests\core-tests.ps1 (not part of the source archive in the exe), dot-sourced so that it sees
# $root, $obj, $sw, $Quick, $Full and Invoke-Native.

$testScript = Join-Path $root 'tests\core-tests.ps1'
if (-not (Test-Path -LiteralPath $testScript)) { throw "Missing $testScript (pass -SkipTests to build the library only)" }
. $testScript
