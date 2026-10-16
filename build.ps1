# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HourCalc build (native C++ / Win32). ASCII only: Windows PowerShell 5.1 reads BOM-less files
# as ANSI.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File build.ps1 [-Sign] [-SkipTests] [-Full] [-EmbedSource]
#
# 1. build-core.ps1: the core, hc_cli.exe and the core tests (C++ unit tests, the C# oracle
#    tests and the two differential tests oracle vs native), unless -SkipTests or tests\ is absent.
#    -Full runs the adversarial differential test (tests\DifferentialReview.cs) in full, not as a sample.
# 2. build-app.ps1: dist\HourCalc.exe (release) and, with tests, the test exe plus its --selftest run.
#    By default the exe does not carry the source zip (owner's choice, 2026-10-10: about 115 KB smaller; the
#    About window shows where the source code is). -EmbedSource puts the zip back in the exe ("Save source
#    code..."). The zip is always written to dist\ for publishing.
# 3. -Sign: Authenticode SHA256 with the Optimey Code Signing certificate, DigiCert timestamp.
# Needs Visual Studio Build Tools 2022 (C++ x64) and the Windows SDK; the tests also need the
# .NET SDK (Roslyn) for the C# oracle. Nothing is downloaded.

param(
    [switch]$Sign,
    [switch]$SkipTests,
    [switch]$Full,
    [switch]$EmbedSource
)

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
# The Optimey Code Signing certificates (same name and CA), by SHA-1 thumbprint. The second takes over
# from the first, whose key cannot be exported from the PC it was made on. -Sign uses the first one this
# PC has, opened by its path Cert:\CurrentUser\My\<thumbprint>; the store is never listed.
$thumbprints = @('DC5A657A60F166815ACDA04E71581F7313553BEA', '55711A7F59D7D47D0CBF76673718446EB4CF4A61')
$timestampUrl = 'http://timestamp.digicert.com'

if (-not (Test-Path -LiteralPath (Join-Path $root 'tests'))) { $SkipTests = $true; Write-Host '== No tests\ folder (source archive): skipping tests' }

. (Join-Path $root 'tools\msvc.ps1')

& (Join-Path $root 'build-core.ps1') -SkipTests:$SkipTests -Full:$Full
if (-not $?) { throw 'build-core.ps1 failed' }
& (Join-Path $root 'build-app.ps1') -SkipTests:$SkipTests -EmbedSource:$EmbedSource
if (-not $?) { throw 'build-app.ps1 failed' }

$exe = Join-Path $root 'dist\HourCalc.exe'
if ($Sign) {
    Write-Host '== Signing'
    $cert = $null
    foreach ($t in $thumbprints) {
        $path = "Cert:\CurrentUser\My\$t"
        if (Test-Path -LiteralPath $path) { $cert = Get-Item -LiteralPath $path; break }
    }
    if (-not $cert) { throw "None of the signing certificates ($($thumbprints -join ', ')) is in Cert:\CurrentUser\My" }
    if (-not $cert.HasPrivateKey) { throw "Certificate $($cert.Thumbprint) has no private key here" }
    Write-Host "   with certificate $($cert.Thumbprint)"
    $sig = Set-AuthenticodeSignature -FilePath $exe -Certificate $cert -HashAlgorithm SHA256 -TimestampServer $timestampUrl
    if ($sig.Status -ne 'Valid') { throw "Signing failed: $($sig.Status) $($sig.StatusMessage)" }
}

$size = (Get-Item -LiteralPath $exe).Length
Write-Host ("== Built {0} ({1:N0} bytes, {2:N1} KB)" -f $exe, $size, ($size / 1KB))
