# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# HourCalc app build (native C++ / Win32). ASCII only: Windows PowerShell 5.1 reads BOM-less files as ANSI.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File build-app.ps1 [-SkipTests] [-EmbedSource]
#
# 1. dist\HourCalc-<version>-source.zip: what is needed to build the exe (native\ without the console front end
#    native\cli, assets\, the build scripts, tools\make_ico.py, tools\msvc.ps1, tools\cflags.rsp, README.md,
#    LICENSE). The exe embeds it, unless -NoEmbeddedSource.
# 2. native\app\app.rc -> obj\app\app.res (icon, version information, manifest, the source archive).
# 3. native\app\*.cpp (without the test hooks) -> dist\HourCalc.exe, linked against obj\core\hc_core.lib
#    (run build-core.ps1 first). The hybrid CRT (static vcruntime, the system UCRT) is used when it
#    makes the exe smaller.
# 4. Unless -SkipTests: the same sources with /DHC_TESTHOOKS -> obj\HourCalc-test.exe, then its --selftest.
# Default (no -EmbedSource): the exe does not carry the source archive (about 120 KB). Its About window reads the license
#    text from a small deflated copy of LICENSE (resource 102) and shows the address of the source code
#    (embedded::kSourceUrl) instead of "Save source code...". The source archive is still written to dist\ for
#    publishing. Objects, resources and test exe of this variant are kept apart from the default build; both
#    write dist\HourCalc.exe, so that is the exe of the build run last.
# Needs Visual Studio Build Tools 2022 (C++ x64) and the Windows SDK. Nothing is downloaded.

param(
    [switch]$SkipTests,
    [switch]$EmbedSource
)
# Default since 2026-10-10 (owner's choice): no source archive in the exe; -EmbedSource puts it back.
$NoEmbeddedSource = -not $EmbedSource

$ErrorActionPreference = 'Stop'
$root = $PSScriptRoot
. (Join-Path $root 'tools\msvc.ps1')

$variant = if ($NoEmbeddedSource) { 'app-noembed' } else { 'app' }
$obj = Join-Path $root ('obj\' + $variant)
$dist = Join-Path $root 'dist'
$core = Join-Path $root 'obj\core\hc_core.lib'
New-Item -ItemType Directory -Force -Path $obj, $dist | Out-Null
if (-not (Test-Path -LiteralPath $core)) { throw "Missing $core (run build-core.ps1 first)" }
if (-not (Test-Path -LiteralPath (Join-Path $root 'native\app\selftest.cpp'))) { $SkipTests = $true; Write-Host '== No test sources (source archive): skipping the test build' }
$icon = Join-Path $root 'assets\icon\app.ico'
if (-not (Test-Path -LiteralPath $icon)) { throw "Missing $icon (run: python tools\make_ico.py assets\icon\app.svg --small assets\icon\app-16.svg --size 24=assets\icon\app-24.svg assets\icon\app.ico)" }

$versionMatch = [regex]::Match([System.IO.File]::ReadAllText((Join-Path $root 'native\app\version.h')), '#define\s+HC_VER_STR\s+"([^"]+)"')
if (-not $versionMatch.Success) { throw 'HC_VER_STR not found in native\app\version.h' }
$version = $versionMatch.Groups[1].Value

# The source archive (kept next to the exe and, unless -NoEmbeddedSource, embedded in it). The About window reads LICENSE from it.
function New-SourceZip([string]$ZipPath) {
    Add-Type -AssemblyName System.IO.Compression
    $files = New-Object System.Collections.Generic.List[string]
    # The test hooks and the self test are not part of the release exe (they are compiled only with
    # /DHC_TESTHOOKS), so they stay in the repository like the tests do; so does native\cli, the console front
    # end that only the differential test uses (build-core.ps1 -SkipTests does not build it).
    $testOnly = @('testhooks.cpp', 'testhooks.h', 'selftest.cpp', 'selftest.h')
    $cliDir = Join-Path $root 'native\cli\'
    foreach ($dir in 'native', 'assets') {
        Get-ChildItem -LiteralPath (Join-Path $root $dir) -Recurse -File |
            Where-Object { ($testOnly -notcontains $_.Name) -and -not $_.FullName.StartsWith($cliDir, [System.StringComparison]::OrdinalIgnoreCase) } |
            ForEach-Object { $files.Add($_.FullName) }
    }
    foreach ($name in 'build.ps1', 'build-core.ps1', 'build-app.ps1', 'README.md', 'LICENSE', 'tools\make_ico.py', 'tools\msvc.ps1', 'tools\cflags.rsp') {
        $path = Join-Path $root $name
        if (-not (Test-Path -LiteralPath $path)) { throw "Missing $path (needed in the source archive)" }
        $files.Add($path)
    }
    $files.Sort([System.StringComparer]::OrdinalIgnoreCase)
    if (Test-Path -LiteralPath $ZipPath) { Remove-Item -LiteralPath $ZipPath -Force }
    $stream = [System.IO.File]::Open($ZipPath, [System.IO.FileMode]::CreateNew)
    try {
        $zip = New-Object System.IO.Compression.ZipArchive($stream, [System.IO.Compression.ZipArchiveMode]::Create)
        try {
            foreach ($file in $files) {
                $name = $file.Substring($root.Length + 1).Replace('\', '/')
                $entry = $zip.CreateEntry($name, [System.IO.Compression.CompressionLevel]::Optimal)
                $entry.LastWriteTime = New-Object System.DateTimeOffset(2026, 1, 1, 0, 0, 0, [System.TimeSpan]::Zero)
                $bytes = [System.IO.File]::ReadAllBytes($file)
                $entryStream = $entry.Open()
                try { $entryStream.Write($bytes, 0, $bytes.Length) } finally { $entryStream.Dispose() }
            }
        } finally { $zip.Dispose() }
    } finally { $stream.Dispose() }
}

# -NoEmbeddedSource: LICENSE as a raw deflate stream (RFC 1951) behind its length (uint32, little endian), for resource 102.
function New-LicenseResource([string]$Path) {
    Add-Type -AssemblyName System.IO.Compression
    $text = [System.IO.File]::ReadAllBytes((Join-Path $root 'LICENSE'))
    $ms = New-Object System.IO.MemoryStream
    $ms.Write([System.BitConverter]::GetBytes([uint32]$text.Length), 0, 4)
    $ds = New-Object System.IO.Compression.DeflateStream($ms, [System.IO.Compression.CompressionLevel]::Optimal, $true)
    try { $ds.Write($text, 0, $text.Length) } finally { $ds.Dispose() }
    [System.IO.File]::WriteAllBytes($Path, $ms.ToArray())
}

# The manifest without its comments and indentation (about 450 bytes less in the exe; the file itself is unchanged).
function New-SmallManifest([string]$From, [string]$To) {
    $xml = [System.IO.File]::ReadAllText($From)
    $xml = [regex]::Replace($xml, '<!--.*?-->', '', [System.Text.RegularExpressions.RegexOptions]::Singleline)
    $xml = [regex]::Replace($xml, '\s+', ' ').Replace('> <', '><').Trim()
    (New-Object System.Xml.XmlDocument).LoadXml($xml)    # still well-formed
    [System.IO.File]::WriteAllText($To, $xml, (New-Object System.Text.UTF8Encoding($false)))
}

function Invoke-Native([string]$What, [scriptblock]$Cmd) {
    & $Cmd
    if ($LASTEXITCODE -ne 0) { throw "$What failed (exit code $LASTEXITCODE)" }
}

Push-Location $root
try {
    Write-Host "== Packing the source code ($version)"
    $sourceZip = Join-Path $dist "HourCalc-$version-source.zip"
    New-SourceZip $sourceZip

    Write-Host '== Resources'
    $fwd = $root.Replace('\', '/')
    $manifest = Join-Path $obj 'app.manifest'
    New-SmallManifest (Join-Path $root 'native\app\app.manifest') $manifest
    $paths = "#define ICON_FILE ""$fwd/assets/icon/app.ico""`r`n#define MANIFEST_FILE ""$($manifest.Replace('\', '/'))""`r`n"
    $rcDefs = @()
    $cppDefs = @()
    if ($NoEmbeddedSource) {
        $licenseRes = Join-Path $obj 'LICENSE.deflate'
        New-LicenseResource $licenseRes
        $paths += "#define LICENSE_FILE ""$($licenseRes.Replace('\', '/'))""`r`n"
        $rcDefs = @('/DHC_NO_EMBEDDED_SOURCE')
        $cppDefs = @('/DHC_NO_EMBEDDED_SOURCE')
    } else {
        $paths += "#define SOURCE_ZIP ""$fwd/dist/HourCalc-$version-source.zip""`r`n"
    }
    [System.IO.File]::WriteAllText((Join-Path $obj 'res_paths.h'), $paths, [System.Text.Encoding]::ASCII)
    $res = Join-Path $obj 'app.res'
    Invoke-Native 'rc' { & rc /nologo @rcDefs "/i$obj" "/fo$res" (Join-Path $root 'native\app\app.rc') }

    $rsp = '@' + (Join-Path $root 'tools\cflags.rsp')
    # Inlining (docs\SIZE.md): the layout and window code is about 4 KB smaller compiled without inlining (/Ob0), the rest
    # (drawing, dialogs, popups, archive, theme) and the core library with the /Ob2 of /O1.
    $noInline = @('app_layout.cpp', 'app_window.cpp')
    function Compile-App([string[]]$Sources, [string]$OutDir, [string[]]$Extra) {
        $off = @($Sources | Where-Object { (Split-Path $_ -Leaf) -in $noInline })
        $on = @($Sources | Where-Object { (Split-Path $_ -Leaf) -notin $noInline })
        Invoke-Native 'compile' { & cl $rsp @cppDefs @Extra /Ob0 /MP /c "/Fo$OutDir\" $off }
        Invoke-Native 'compile' { & cl $rsp @cppDefs @Extra /MP /c "/Fo$OutDir\" $on }
    }
    $libs = @('gdiplus.lib', 'dwrite.lib', 'user32.lib', 'gdi32.lib', 'advapi32.lib', 'dwmapi.lib', 'comdlg32.lib', 'imm32.lib')
    # comdlg32.dll (the Save dialog of "Save source code...") is delay-loaded; the variant without the source has no such dialog
    # and delay-loads nothing (the arithmetic of the core is own code since the portable Dec: no oleaut32 any more).
    $delay = @()
    if (-not $NoEmbeddedSource) { $delay = @('/DELAYLOAD:comdlg32.dll'); $libs += 'delayimp.lib' }
    $allSources = @(Get-ChildItem -LiteralPath (Join-Path $root 'native\app') -Filter '*.cpp' | Sort-Object Name | ForEach-Object { $_.FullName })
    $releaseSources = @($allSources | Where-Object { (Split-Path $_ -Leaf) -notin @('testhooks.cpp', 'selftest.cpp') })

    # Release exe: first with the plain static CRT, then with the hybrid CRT; keep the smaller one that starts.
    Write-Host '== Compiling HourCalc.exe'
    $rel = Join-Path $obj 'release'
    New-Item -ItemType Directory -Force -Path $rel | Out-Null
    Get-ChildItem -LiteralPath $rel -Filter '*.obj' -ErrorAction SilentlyContinue | Remove-Item -Force
    Compile-App $releaseSources $rel @()
    $relObjs = @(Get-ChildItem -LiteralPath $rel -Filter '*.obj' | ForEach-Object { $_.FullName })
    $exe = Join-Path $dist 'HourCalc.exe'
    # /DEPENDENTLOADFLAG:0x800 (LOAD_LIBRARY_SEARCH_SYSTEM32): the DLLs the exe loads, at start and later, come from the
    # system folder only. dwmapi.dll and dwrite.dll are not KnownDLLs, so without it the loader looks in the exe's folder
    # first and a portable exe in Downloads or on a USB stick could pick up a planted DLL of that name.
    # Size: /NOCOFFGRPINFO leaves out the section-contribution table that the LTCG linker would put in the debug directory
    # (1 KB of data only profiling tools read), /MERGE:.pdata=.rdata puts the (read-only) exception tables in the read-only data
    # section, which saves the padding of a section.
    $linkBase = @('/nologo', '/LTCG', '/OPT:REF', '/OPT:ICF', '/SUBSYSTEM:WINDOWS', '/MANIFEST:NO', '/DEPENDENTLOADFLAG:0x800',
                  '/NOCOFFGRPINFO', '/MERGE:.pdata=.rdata') + $delay
    $plain = Join-Path $obj 'HourCalc-plain.exe'
    $hybrid = Join-Path $obj 'HourCalc-hybrid.exe'
    Invoke-Native 'link (static CRT)' { & link @linkBase "/OUT:$plain" @relObjs $res $core @libs }
    $useHybrid = $false
    & link @linkBase '/NODEFAULTLIB:libucrt.lib' "/OUT:$hybrid" @relObjs $res $core @libs ucrt.lib 2>&1 | Out-Null
    if ($LASTEXITCODE -eq 0 -and (Test-Path -LiteralPath $hybrid)) {
        $sizePlain = (Get-Item -LiteralPath $plain).Length
        $sizeHybrid = (Get-Item -LiteralPath $hybrid).Length
        Write-Host ("   static CRT {0:N0} bytes, hybrid CRT {1:N0} bytes" -f $sizePlain, $sizeHybrid)
        if ($sizeHybrid -lt $sizePlain) { $useHybrid = $true }
    }
    Copy-Item -LiteralPath $(if ($useHybrid) { $hybrid } else { $plain }) -Destination $exe -Force
    Write-Host ("   using the {0} CRT" -f $(if ($useHybrid) { 'hybrid' } else { 'static' }))

    if (-not $SkipTests) {
        Write-Host '== Compiling HourCalc-test.exe (test hooks)'
        $tst = Join-Path $obj 'test'
        New-Item -ItemType Directory -Force -Path $tst | Out-Null
        Get-ChildItem -LiteralPath $tst -Filter '*.obj' -ErrorAction SilentlyContinue | Remove-Item -Force
        Compile-App $allSources $tst @('/DHC_TESTHOOKS')
        $tstObjs = @(Get-ChildItem -LiteralPath $tst -Filter '*.obj' | ForEach-Object { $_.FullName })
        $testExe = Join-Path $root $(if ($NoEmbeddedSource) { 'obj\HourCalc-test-noembed.exe' } else { 'obj\HourCalc-test.exe' })
        Remove-Item -LiteralPath $testExe -Force -ErrorAction SilentlyContinue
        # The same link configuration as the release exe (CRT choice, delay-loaded DLLs), as a console program.
        $testLink = @('/nologo', '/LTCG', '/OPT:REF', '/OPT:ICF', '/SUBSYSTEM:CONSOLE', '/ENTRY:wWinMainCRTStartup', '/MANIFEST:NO', '/DEPENDENTLOADFLAG:0x800',
                      '/NOCOFFGRPINFO', '/MERGE:.pdata=.rdata') + $delay
        $testLibs = @($libs)
        if ($useHybrid) { $testLink += '/NODEFAULTLIB:libucrt.lib'; $testLibs += 'ucrt.lib' }
        Invoke-Native 'link (test)' {
            & link @testLink "/OUT:$testExe" @tstObjs $res $core @testLibs
        }
        Write-Host '== Self test (hidden windows, nothing is shown)'
        & $testExe --selftest
        if ($LASTEXITCODE -ne 0) { throw "Self test failed (exit code $LASTEXITCODE)" }
    }
} finally {
    Pop-Location
}

$size = (Get-Item -LiteralPath $exe).Length
Write-Host ("== Built {0} ({1:N0} bytes, {2:N1} KB){3}" -f $exe, $size, ($size / 1KB), $(if ($NoEmbeddedSource) { ', without the embedded source archive' } else { '' }))
