# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# The tests of the core on Linux, driven from Windows (docs\LINUX.md, section 1). ASCII only: Windows PowerShell 5.1.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tests\linux-core-tests.ps1 -Distro Debian|Ubuntu [-Quick] [-Full] [-Threads 6] [-SkipDifferential]
#
# 1. tools\wsl-sync.ps1 mirrors the repository into ~/<Name> of the distro (default hc-core).
# 2. meson + ninja build the core there (~/<Name>-build: libhccore, hc_cli, core_tests, review_state_cli; no front ends),
#    then core_tests runs.
# 3. tests\Differential.cs and tests\DifferentialReview.cs (compiled here like tests\core-tests.ps1 does, into obj\core\) run against
#    the Linux hc_cli through --cli "wsl.exe -d <Distro> --cd / -- <hc_cli>": the default suite in full (-Quick: the quick one), the
#    review suite as a sample (--scale=0.05 --cultures=2; -Quick: --scale=0.01; -Full: everything), once against hc_cli and once, with
#    the private state of the calculator, against review_state_cli. Every suite must end with 0 mismatches.
# Nothing is shown on screen: the programs under test are console programs in WSL, run as the normal user (never root, never sudo).
# -Threads is the number of worker processes (default 6: WSL has 8 processors, see CLAUDE.md).

param(
    [Parameter(Mandatory = $true)][ValidateSet('Debian', 'Ubuntu')][string]$Distro,
    [string]$Name = 'hc-core',
    [string]$User = '',
    [int]$Threads = 6,
    [switch]$Quick,
    [switch]$Full,
    [switch]$SkipDifferential
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$obj = Join-Path $root 'obj\core'
New-Item -ItemType Directory -Force -Path $obj | Out-Null
$sw = [System.Diagnostics.Stopwatch]::StartNew()

function Invoke-Wsl([string]$Command) {
    & wsl.exe -d $Distro --cd / -- bash -lc $Command
    if ($LASTEXITCODE -ne 0) { throw "in $Distro failed ($LASTEXITCODE): $Command" }
}

# -User: the Linux user whose home is used; default: the distro's default user.
if (-not $User) { $User = & (Join-Path $root 'tools\wsl-user.ps1') -Distro $Distro }
Write-Host "== Sync to $Distro"
& (Join-Path $root 'tools\wsl-sync.ps1') -Distro $Distro -Name $Name -User $User
if (-not $?) { throw 'wsl-sync.ps1 failed' }

$build = "~/$Name-build"
Write-Host "== Build the core in $Distro (meson, ninja)"
Invoke-Wsl "cd ~/$Name && ([ -d $build ] || meson setup $build . -Dgtk=false -Dqt=false) && ninja -C $build"
Write-Host "== core_tests in $Distro"
Invoke-Wsl "$build/native/core/core_tests"

if ($SkipDifferential) { Write-Host ("== Linux core tests ({0}) passed in {1:N1} s" -f $Distro, $sw.Elapsed.TotalSeconds); return }

# ---- the C# side (the same compiler and references as tests\core-tests.ps1)
$fw = 'C:\Windows\Microsoft.NET\Framework64\v4.0.30319'
$sdkRoot = Join-Path $env:ProgramFiles 'dotnet\sdk'
$csc = Get-ChildItem -LiteralPath $sdkRoot -Directory | Sort-Object Name -Descending |
    ForEach-Object { Join-Path $_.FullName 'Roslyn\bincore\csc.dll' } | Where-Object { Test-Path -LiteralPath $_ } | Select-Object -First 1
if (-not $csc) { throw "Roslyn csc.dll not found under $sdkRoot" }
$refs = @("-r:$fw\mscorlib.dll", "-r:$fw\System.dll", "-r:$fw\System.Core.dll")
$oracle = @(Get-ChildItem -LiteralPath (Join-Path $root 'oracle') -Filter '*.cs' | Sort-Object Name | ForEach-Object { $_.FullName })
$config = '<?xml version="1.0" encoding="utf-8"?>' + "`n" +
    '<configuration><runtime><gcServer enabled="true"/><gcConcurrent enabled="false"/></runtime></configuration>' + "`n"
$tools = @{}
foreach ($t in @(@{ Exe = 'differential.exe'; Source = 'tests\Differential.cs' }, @{ Exe = 'differential_review.exe'; Source = 'tests\DifferentialReview.cs' })) {
    $out = Join-Path $obj $t.Exe
    & dotnet $csc -nologo -noconfig -nostdlib -langversion:7.3 -target:exe -optimize+ -debug- "-out:$out" @refs @oracle (Join-Path $root $t.Source)
    if ($LASTEXITCODE -ne 0) { throw "csc failed for $($t.Source)" }
    [System.IO.File]::WriteAllText($out + '.config', $config)
    $tools[$t.Exe] = $out
}

function Invoke-Native([string]$What, [scriptblock]$Cmd) {
    & $Cmd
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit code $LASTEXITCODE)" }
}

$home1 = "/home/$User/$Name-build/native/core"
$cli = "wsl.exe -d $Distro --cd / -- $home1/hc_cli"
$stateCli = "wsl.exe -d $Distro --cd / -- $home1/review_state_cli"
$diffArgs = @('--cli', $cli, "--threads=$Threads")
if ($Quick) { $diffArgs += '--quick' }
if ($Full) { $reviewArgs = @() } elseif ($Quick) { $reviewArgs = @('--scale=0.01', '--cultures=2') } else { $reviewArgs = @('--scale=0.05', '--cultures=2') }

Write-Host "== Differential test: C# oracle vs the Linux hc_cli ($Distro)"
Invoke-Native 'differential test' { & $tools['differential.exe'] @diffArgs }
Write-Host "== Review differential test vs the Linux hc_cli ($Distro)"
Invoke-Native 'review differential test' { & $tools['differential_review.exe'] --cli $cli "--threads=$Threads" @reviewArgs }
Write-Host "== Review differential test with the private state of the calculator ($Distro)"
Invoke-Native 'review differential test (state)' { & $tools['differential_review.exe'] --cli $stateCli --state "--threads=$Threads" @reviewArgs }

Write-Host ("== Linux core tests ({0}) passed in {1:N1} s" -f $Distro, $sw.Elapsed.TotalSeconds)
