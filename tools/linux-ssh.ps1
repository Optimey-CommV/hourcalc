# SPDX-FileCopyrightText: 2026 Optimey CommV
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Shared by the Linux tools (tools\linux-sync.ps1, tools\build-deb.ps1, tools\apt-repo.ps1, tests\linux-core-tests.ps1),
# which build and test on a Debian 13 or Ubuntu 26.04 machine reached over SSH (docs\LINUX.md, "Build environment").
# Dot-source it: . (Join-Path $PSScriptRoot 'linux-ssh.ps1'). ASCII only (Windows PowerShell 5.1).
#
# The machine is an SSH host name: -SshHost of the calling tool, else hourcalc-debian or hourcalc-ubuntu, which
# ~/.ssh/config maps to the address, the user (a normal user, never root) and the key. Every call runs with
# BatchMode=yes: nothing ever asks for a password, and nothing is shown in a window.

$script:LinuxSsh = (Get-Command ssh.exe -ErrorAction Stop).Source
$script:LinuxScp = (Get-Command scp.exe -ErrorAction Stop).Source
$script:LinuxSshOptions = @('-o', 'BatchMode=yes', '-o', 'LogLevel=ERROR')

# The SSH host for a distro: $SshHost when given, else hourcalc-debian / hourcalc-ubuntu.
function Get-LinuxHost([string]$Distro, [string]$SshHost) {
    $h = if ($SshHost) { $SshHost } else { 'hourcalc-' + $Distro.ToLowerInvariant() }
    if ($h -notmatch '^[A-Za-z0-9._@-]+$') { throw "Invalid SSH host: $h" }
    return $h
}

# Shows the lines of a native program: text on stderr arrives as error records (Windows PowerShell 5.1), which are
# shown as plain text; NUL characters are dropped and empty lines skipped.
function Write-LinuxLine($Item) {
    $line = if ($Item -is [System.Management.Automation.ErrorRecord]) { $Item.Exception.Message } else { [string]$Item }
    $line = $line -replace "`0", ''
    if ($line.Trim().Length -gt 0) { Write-Host $line }
}

# Runs a bash script on the machine as a login shell (as a terminal there would) and returns its exit code.
# The script travels base64-encoded and runs from a temporary file, so no quoting rule of Windows PowerShell 5.1,
# ssh or the remote shell can change it, and the standard input of ssh stays free: -InputText is sent there
# (apt-repo.ps1 sends the signing key that way, so it never reaches a disk). Without -InputText the standard input
# is empty (ssh -n). Text on stderr never aborts the caller ($ErrorActionPreference = 'Stop' would make the error
# records of Windows PowerShell 5.1 fatal); only the exit code counts, and the caller decides what it means.
function Invoke-Linux {
    param(
        [Parameter(Mandatory = $true)][string]$SshHost,
        [Parameter(Mandatory = $true)][string]$Script,
        [string]$InputText
    )
    # Never as root (docs\LINUX.md): an SSH host that logs in as root is refused before anything runs.
    $guard = 'if [ "$(id -u)" = 0 ]; then echo "linux-ssh.ps1: refusing to run as root on this machine" >&2; exit 97; fi' + "`n"
    $b64 = [Convert]::ToBase64String([System.Text.Encoding]::UTF8.GetBytes((($guard + $Script) -replace "`r`n", "`n")))
    $remote = 'f=$(mktemp) && echo ' + $b64 + ' | base64 -d > $f && bash -l $f; rc=$?; rm -f $f; exit $rc'
    $ssh = $script:LinuxSsh; $opts = $script:LinuxSshOptions
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        if ($PSBoundParameters.ContainsKey('InputText')) {
            $InputText | & $ssh -T @opts $SshHost $remote 2>&1 | ForEach-Object { Write-LinuxLine $_ }
        } else {
            & $ssh -T -n @opts $SshHost $remote 2>&1 | ForEach-Object { Write-LinuxLine $_ }
        }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    return $code
}

# Copies a folder of the machine (a path relative to the user's home) into a new folder under $Destination, and
# returns the path of that copy ($Destination\<last part of the path>). Throws when the copy fails.
function Copy-FromLinux([string]$SshHost, [string]$RemotePath, [string]$Destination) {
    if ($RemotePath -notmatch '^[A-Za-z0-9._/-]+$' -or $RemotePath -match '(^|/)\.\.(/|$)') { throw "Invalid remote path: $RemotePath" }
    New-Item -ItemType Directory -Force -Path $Destination | Out-Null
    $scp = $script:LinuxScp; $opts = $script:LinuxSshOptions
    $saved = $ErrorActionPreference
    $ErrorActionPreference = 'Continue'
    try {
        & $scp -q -r -B @opts "${SshHost}:$RemotePath" $Destination 2>&1 | ForEach-Object { Write-LinuxLine $_ }
        $code = $LASTEXITCODE
    } finally {
        $ErrorActionPreference = $saved
    }
    if ($code -ne 0) { throw "copying ${SshHost}:$RemotePath failed (scp exit code $code)" }
    return (Join-Path $Destination (Split-Path -Leaf $RemotePath))
}

# A new, empty folder for this run under the temporary folder (the caller removes it).
function New-LinuxTempFolder([string]$Prefix) {
    $path = Join-Path ([System.IO.Path]::GetTempPath()) ("{0}-{1}-{2}" -f $Prefix, $PID, [Guid]::NewGuid().ToString('N').Substring(0, 8))
    New-Item -ItemType Directory -Force -Path $path | Out-Null
    return $path
}
