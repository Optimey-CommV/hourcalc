# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The tests of the core, run by build-core.ps1 (dot-sourced after it built the library, hc_cli.exe, core_tests.exe
# and review_state_cli.exe; it uses $root, $obj, $sw, $Quick, $Full and Invoke-Native of that script). It lives in
# tests\ so that the source archive in the exe, which has no tests, does not carry it. ASCII only: Windows
# PowerShell 5.1 reads BOM-less files as ANSI.
#
# 4. run core_tests.exe
# 5. tests\Tests.cs + oracle\*.cs (the C# unit tests of the reference) -> obj\core\oracle_tests.exe, run
# 6. tests\Differential.cs + oracle\*.cs -> obj\core\differential.exe, run against hc_cli.exe: every
#    string of the native core must equal the C# reference. -Quick runs a smaller sample (development).
# 7. tests\DifferentialReview.cs + oracle\*.cs -> obj\core\differential_review.exe: a second, independent
#    comparison (long calculator sequences, Paste fuzz, a grid of System.Decimal values, settings files), run
#    once against hc_cli.exe and once, with the private fields of the calculator compared too, against
#    review_state_cli.exe (tests\native\review_state_cli.cpp). Default: a sample (--scale=0.05, two decimal
#    separators); -Quick a smaller one; -Full the whole run (about 30 minutes).
# Needs the .NET SDK (Roslyn csc) plus the .NET Framework 4.8 that ships with Windows. Nothing is downloaded.

function Find-Csc {
    $sdkRoot = Join-Path $env:ProgramFiles 'dotnet\sdk'
    $preferred = Join-Path $sdkRoot '6.0.428\Roslyn\bincore\csc.dll'
    if (Test-Path -LiteralPath $preferred) { return $preferred }
    $found = Get-ChildItem -LiteralPath $sdkRoot -Directory -ErrorAction SilentlyContinue |
        Sort-Object Name -Descending |
        ForEach-Object { Join-Path $_.FullName 'Roslyn\bincore\csc.dll' } |
        Where-Object { Test-Path -LiteralPath $_ } |
        Select-Object -First 1
    if (-not $found) { throw "Roslyn csc.dll not found under $sdkRoot" }
    return $found
}

$fw = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319'
$csc = Find-Csc
$refs = @("-r:$fw\mscorlib.dll", "-r:$fw\System.dll", "-r:$fw\System.Core.dll")
$oracle = @(Get-ChildItem -LiteralPath (Join-Path $root 'oracle') -Filter '*.cs' | Sort-Object Name | ForEach-Object { $_.FullName })

function Invoke-Csc([string]$Out, [string[]]$Sources) {
    & dotnet $csc -nologo -noconfig -nostdlib -langversion:7.3 -target:exe -optimize+ -debug- "-out:$Out" @refs @Sources
    if ($LASTEXITCODE -ne 0) { throw "csc failed with exit code $LASTEXITCODE" }
}

Write-Host '== C++ unit tests (core_tests.exe)'
Invoke-Native 'core_tests' { & (Join-Path $obj 'core_tests.exe') }

Write-Host '== C# oracle unit tests (tests\Tests.cs)'
$oracleTests = Join-Path $obj 'oracle_tests.exe'
Invoke-Csc $oracleTests ($oracle + @(Join-Path $root 'tests\Tests.cs'))
Invoke-Native 'oracle unit tests' { & $oracleTests }

Write-Host '== Differential test: native core vs C# oracle'
$diff = Join-Path $obj 'differential.exe'
Invoke-Csc $diff ($oracle + @(Join-Path $root 'tests\Differential.cs'))
# The test allocates a lot from many threads: server garbage collection keeps it from serializing on the GC.
[System.IO.File]::WriteAllText($diff + '.config', '<?xml version="1.0" encoding="utf-8"?>' + "`n" +
    '<configuration><runtime><gcServer enabled="true"/><gcConcurrent enabled="false"/></runtime></configuration>' + "`n")
$diffArgs = @((Join-Path $obj 'hc_cli.exe'))
if ($Quick) { $diffArgs += '--quick' }
Invoke-Native 'differential test' { & $diff @diffArgs }

Write-Host '== Review differential test: a second comparison, native core vs C# oracle'
$review = Join-Path $obj 'differential_review.exe'
Invoke-Csc $review ($oracle + @(Join-Path $root 'tests\DifferentialReview.cs'))
Copy-Item -LiteralPath ($diff + '.config') -Destination ($review + '.config') -Force
if ($Full) { $reviewArgs = @() }
elseif ($Quick) { $reviewArgs = @('--scale=0.01', '--cultures=2') }
else { $reviewArgs = @('--scale=0.05', '--cultures=2') }
Invoke-Native 'review differential test' { & $review (Join-Path $obj 'hc_cli.exe') @reviewArgs }
Write-Host '== Review differential test with the private state of the calculator'
Invoke-Native 'review differential test (state)' { & $review (Join-Path $obj 'review_state_cli.exe') '--state' @reviewArgs }

Write-Host ("== Core tests passed in {0:N1} s" -f $sw.Elapsed.TotalSeconds)
