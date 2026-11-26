<#
    Installer for mosh on Windows.

        irm https://raw.githubusercontent.com/Yuanpeng-Li/mosh-windows/windows/scripts/install.ps1 | iex

    Installs mosh.exe, mosh.ps1, mosh.cmd, mosh-client.exe and mosh-server.exe
    into %LOCALAPPDATA%\Programs\mosh and puts that directory on the user PATH.
    Nothing is written outside that directory except the one PATH entry, and
    nothing needs admin.

    mosh-server has to be on PATH under its own name, not behind a shortcut:
    when someone mosh-es *to* this machine, their client asks ssh to run
    "mosh-server", and a session started by sshd sees only PATH.

    Uninstall:  mosh-uninstall   (dropped alongside).

    Parameters:
      -Version <tag>   install a specific release tag instead of the newest
      -Dir <path>      install somewhere else
      -FromZip <path>  install a locally built archive instead of downloading
      -NoPath          do not touch PATH
#>

[CmdletBinding()]
param(
    [string] $Version,
    [string] $Dir = "$env:LOCALAPPDATA\Programs\mosh",
    [string] $FromZip,
    [switch] $NoPath
)

$ErrorActionPreference = 'Stop'
$Repo = 'Yuanpeng-Li/mosh-windows'

function Info($m)  { Write-Host "  $m" }
function Step($m)  { Write-Host "==> $m" -ForegroundColor Cyan }
function Warn($m)  { Write-Host "!!  $m" -ForegroundColor Yellow }
function Fail($m)  { Write-Host "!!  $m" -ForegroundColor Red; exit 1 }

function Save-InstallState([string] $path, [hashtable] $state) {
    $newState = Join-Path (Split-Path -Parent $path) ([IO.Path]::GetRandomFileName())
    try {
        $state | ConvertTo-Json | Set-Content -LiteralPath $newState -Encoding UTF8
        if ([IO.File]::Exists($path)) {
            [IO.File]::Replace($newState, $path, [System.Management.Automation.Language.NullString]::Value)
        } else {
            [IO.File]::Move($newState, $path)
        }
    } finally {
        if ([IO.File]::Exists($newState)) { [IO.File]::Delete($newState) }
    }
}

# Keep the installed path out of generated batch and PowerShell source.
function Write-Uninstaller([string] $scriptPath, [string] $cmdPath) {
@'
$ErrorActionPreference = 'Stop'
$stateFile = Join-Path $PSScriptRoot 'mosh-install.json'
$state = Get-Content -LiteralPath $stateFile -Raw | ConvertFrom-Json
if ($state.package -ne 'Yuanpeng-Li/mosh-windows') { throw 'Invalid mosh installation record' }
foreach ($name in $state.files) {
    if (!$name -or $name -in @('.', '..') -or [IO.Path]::GetFileName($name) -cne $name -or
        $name.IndexOfAny([IO.Path]::GetInvalidFileNameChars()) -ge 0) {
        throw "Invalid package file: $name"
    }
    $path = Join-Path $PSScriptRoot $name
    if (Test-Path -LiteralPath $path -PathType Leaf) { Remove-Item -LiteralPath $path -Force }
}
if ($state.pathEntry) {
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $entries = [Collections.Generic.List[string]]::new()
    $entries.AddRange([string[]]($userPath -split ';'))
    $index = $entries.FindLastIndex([Predicate[string]]{ param($entry) $entry -eq $state.pathEntry })
    if ($index -ge 0) {
        $entries.RemoveAt($index)
        [Environment]::SetEnvironmentVariable('Path', ($entries -join ';'), 'User')
    }
}
Remove-Item -LiteralPath (Join-Path $PSScriptRoot 'mosh-uninstall.cmd'), $PSCommandPath, $stateFile -Force
if ($state.createdDirectory -and -not (Get-ChildItem -LiteralPath $PSScriptRoot -Force)) {
    [IO.Directory]::Delete($PSScriptRoot, $false)
}
'@ | Set-Content -LiteralPath $scriptPath -Encoding UTF8
@'
@echo off
rem Leave the batch context before PowerShell removes this file.
(goto) 2>nul & powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0mosh-uninstall.ps1"
'@ | Set-Content -LiteralPath $cmdPath -Encoding ASCII
}

try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch { }

Step "Installing mosh for Windows"

# ---------------------------------------------------------------- prereqs --

if ($PSVersionTable.PSVersion.Major -lt 5) {
    Fail "PowerShell 5.1 or newer is required (found $($PSVersionTable.PSVersion))."
}
if (-not (Get-Command ssh -ErrorAction SilentlyContinue)) {
    Warn "ssh was not found on PATH."
    Warn "Install it with:  Add-WindowsCapability -Online -Name OpenSSH.Client~~~~0.0.1.0"
    Warn "Continuing -- mosh will not work until ssh is available."
}

# ----------------------------------------------------------- the release --
# The release archive, not individual raw files: it carries mosh.exe, which is
# a binary and so is not in the git tree.

$downloaded = $false
if ($FromZip) {
    if (-not (Test-Path -LiteralPath $FromZip)) { Fail "no such archive: $FromZip" }
    $tmpZip = (Resolve-Path -LiteralPath $FromZip).Path
    Info "Using $tmpZip"
} else {
    $endpoint = if ($Version) { 'tags/' + [Uri]::EscapeDataString($Version) } else { 'latest' }
    $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/$endpoint" -UseBasicParsing
    $Version = $rel.tag_name
    Info "Release $Version"
    $arch = if ([Environment]::Is64BitOperatingSystem -and $env:PROCESSOR_ARCHITECTURE -eq 'ARM64') { 'arm64' } else { 'x64' }
    $zipName = "mosh-windows-$arch-$Version.zip"
    $asset = @($rel.assets | Where-Object name -ceq $zipName)
    if ($asset.Count -ne 1 -or $asset[0].digest -notmatch '^sha256:([0-9a-f]{64})$') {
        throw "Release $Version has no verified $arch Windows archive."
    }
    $expected = $Matches[1]
    $zipUrl = $asset[0].browser_download_url

    Step "Downloading $zipName"
    $tmpZip = Join-Path ([System.IO.Path]::GetTempPath()) ("mosh-" + [Guid]::NewGuid().ToString('N') + '.zip')
    try {
        Invoke-WebRequest -Uri $zipUrl -OutFile $tmpZip -UseBasicParsing
        if ((Get-FileHash -LiteralPath $tmpZip -Algorithm SHA256).Hash -ne $expected) {
            throw 'ZIP SHA-256 mismatch.'
        }
    } catch {
        if (Test-Path -LiteralPath $tmpZip) { Remove-Item -LiteralPath $tmpZip -Force }
        throw
    }
    $downloaded = $true
}

Step "Installing into $Dir"
$createdDir = -not (Test-Path -LiteralPath $Dir)
$Dir = (New-Item -ItemType Directory -Path $Dir -Force).FullName
$stateFile = Join-Path $Dir 'mosh-install.json'
$uninstallCmd = Join-Path $Dir 'mosh-uninstall.cmd'
$uninstallScript = Join-Path $Dir 'mosh-uninstall.ps1'
$ownedFiles = @()
$ownedPath = $null
$legacy = $false
$previous = $null
if (Test-Path -LiteralPath $stateFile) {
    $previous = Get-Content -Raw -LiteralPath $stateFile | ConvertFrom-Json
    if ($previous.package -ne $Repo) { throw "Unrecognized installation record at $stateFile" }
    $ownedFiles = @($previous.files)
    $createdDir = $previous.createdDirectory
    $ownedPath = $previous.pathEntry
} elseif (Test-Path -LiteralPath $uninstallCmd -PathType Leaf) {
    # The earlier installer recorded neither PATH ownership nor whether it
    # created the directory. Preserve both when upgrading such installs.
    $legacy = (Get-Content -LiteralPath $uninstallCmd) -contains 'rem Uninstall the mosh launcher installed by install.ps1.'
    if (-not $legacy) { throw "File already exists: $uninstallCmd" }
}
if ((Test-Path -LiteralPath $uninstallScript) -and -not $previous) {
    throw "File already exists: $uninstallScript"
}
$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("mosh-x-" + [Guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -LiteralPath $tmpZip -DestinationPath $tmpDir -Force
    # The archive has a single top-level mosh\ directory.
    $src = Join-Path $tmpDir 'mosh'
    if (-not (Test-Path $src)) { $src = $tmpDir }
    $packageFiles = @(Get-ChildItem -LiteralPath $src -File)
    $legacyFiles = @('mosh.exe','mosh-client.exe','mosh-server.exe','mosh.cmd','mosh.ps1','COPYING','README.windows.md')
    if (-not $packageFiles.Count) { throw "No package files in $tmpZip" }
    foreach ($file in $packageFiles) {
        $dest = Join-Path $Dir $file.Name
        if ($dest -in @($stateFile, $uninstallScript, $uninstallCmd)) {
            throw "Archive conflicts with the installer: $($file.Name)"
        }
        if (Test-Path -LiteralPath $dest) {
            $wasOwned = $file.Name -in $ownedFiles -or ($legacy -and $file.Name -in $legacyFiles)
            if (-not $wasOwned -or -not (Test-Path -LiteralPath $dest -PathType Leaf)) {
                throw "File already exists: $dest"
            }
        }
    }
    $ownedFiles = @(($ownedFiles + $packageFiles.Name) | Sort-Object -Unique)
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    $addPath = -not $NoPath -and (($userPath -split ';') -notcontains $Dir)
    if ($ownedPath -and ($userPath -split ';') -notcontains $ownedPath) { $ownedPath = $null }
    $state = @{
        package = $Repo
        files = $ownedFiles
        createdDirectory = $createdDir
        pathEntry = $ownedPath
    }
    # Publish the record before copying. A locked executable or interrupted
    # upgrade can then be retried or uninstalled using the same ownership.
    Save-InstallState $stateFile $state
    Write-Uninstaller $uninstallScript $uninstallCmd
    foreach ($file in $packageFiles) {
        Copy-Item -LiteralPath $file.FullName -Destination (Join-Path $Dir $file.Name) -Force
        Info $file.Name
    }
} finally {
    if ($downloaded) { Remove-Item -LiteralPath $tmpZip -Force -ErrorAction SilentlyContinue }
    Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
}

# ------------------------------------------------------------------- PATH --

if ($addPath) {
    Step "Adding $Dir to your PATH"
    $new = if ([string]::IsNullOrEmpty($userPath)) { $Dir } else { "$userPath;$Dir" }
    [Environment]::SetEnvironmentVariable('Path', $new, 'User')
    try {
        $state.pathEntry = $Dir
        Save-InstallState $stateFile $state
    } catch {
        if ([Environment]::GetEnvironmentVariable('Path', 'User') -ceq $new) {
            [Environment]::SetEnvironmentVariable('Path', $userPath, 'User')
        }
        throw
    }
    $env:Path = "$env:Path;$Dir"
}

# --------------------------------------------------------------- firewall --
# Only needed to be reached *by* mosh. Adding it needs admin, so it is printed
# rather than attempted: an installer that silently opens a port is not one
# people should run from a URL.

$serverExe = Join-Path $Dir 'mosh-server.exe'
$haveRule = $false
if (Test-Path $serverExe) {
    try {
        $haveRule = $null -ne (Get-NetFirewallRule -DisplayName 'mosh-server (UDP)' -ErrorAction SilentlyContinue)
    } catch { }
}

# ------------------------------------------------------------------- done --

Write-Host ""
Step "Installed"
Write-Host @"
  Open a NEW terminal, then:

      mosh user@host

  To reach a Linux or Mac host, it needs mosh installed there (apt install
  mosh, brew install mosh, dnf install mosh). Nothing else is required.

      mosh --help          all options
      mosh-uninstall       remove everything this installed
"@

if ((Test-Path $serverExe) -and -not $haveRule) {
    Write-Host ""
    Write-Host @"
  To let other machines mosh *to* this one, run this once in an elevated
  PowerShell. It is per-program rather than per-port because mosh picks a
  port in 60000-61000 and may change it while roaming:

      New-NetFirewallRule -DisplayName 'mosh-server (UDP)' ``
        -Direction Inbound -Action Allow -Protocol UDP ``
        -Program '$serverExe' -Profile Any

  Undo with:  Remove-NetFirewallRule -DisplayName 'mosh-server (UDP)'
"@
}
