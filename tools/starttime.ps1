# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Startup timing: starts an exe N times, measures the time until it has a visible main
# window, reports each run and the median, then closes it. The window appears on screen
# (and takes focus) for a moment. ASCII only.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File tools\starttime.ps1 -Exe dist\HourCalc.exe [-Runs 5]

param([Parameter(Mandatory = $true)][string]$Exe, [int]$Runs = 5, [string]$Arguments = '')

$times = @()
foreach ($i in 1..$Runs) {
    $sw = [System.Diagnostics.Stopwatch]::StartNew()
    if ($Arguments) { $p = Start-Process -FilePath $Exe -ArgumentList $Arguments -PassThru } else { $p = Start-Process -FilePath $Exe -PassThru }
    while ($sw.Elapsed.TotalSeconds -lt 20) {
        $p.Refresh()
        if ($p.MainWindowHandle -ne [IntPtr]::Zero) { break }
        Start-Sleep -Milliseconds 5
    }
    $t = $sw.Elapsed.TotalMilliseconds
    Start-Sleep -Milliseconds 300
    $mods = ($p.Modules | Where-Object { $_.ModuleName -match '^(dxgi|d3d|d2d1|D3D10Warp)' } | ForEach-Object { $_.ModuleName }) -join ','
    try { $p.CloseMainWindow() | Out-Null; if (-not $p.WaitForExit(1000)) { $p.Kill() } } catch { }
    $times += $t
    Write-Host ("run {0}: {1,6:N0} ms  DirectX modules: [{2}]" -f $i, $t, $mods)
    Start-Sleep -Milliseconds 400
}
$sorted = $times | Sort-Object
Write-Host ("median: {0:N0} ms" -f $sorted[[int][Math]::Floor($sorted.Count / 2)])
