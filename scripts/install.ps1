<#
    Installer for the Windows mosh launcher.

        irm https://raw.githubusercontent.com/Yuanpeng-Li/mosh-windows/windows/scripts/install.ps1 | iex

    Installs mosh.ps1 and mosh.cmd into %LOCALAPPDATA%\Programs\mosh, puts that
    directory on the user PATH, and fetches a mosh-client. Nothing is written
    outside that directory except the one PATH entry. Nothing needs admin.

    Uninstall:  mosh-uninstall   (dropped alongside), or delete the directory
                and remove the PATH entry.

    Parameters:
      -Version <tag>   install a specific release tag instead of the newest
      -Dir <path>      install somewhere else
      -NoClient        skip downloading mosh-client
      -NoPath          do not touch PATH
#>

[CmdletBinding()]
param(
    [string] $Version,
    [string] $Dir = "$env:LOCALAPPDATA\Programs\mosh",
    [switch] $NoClient,
    [switch] $NoPath
)

$ErrorActionPreference = 'Stop'
$Repo = 'Yuanpeng-Li/mosh-windows'

function Info($m)  { Write-Host "  $m" }
function Step($m)  { Write-Host "==> $m" -ForegroundColor Cyan }
function Warn($m)  { Write-Host "!!  $m" -ForegroundColor Yellow }
function Fail($m)  { Write-Host "!!  $m" -ForegroundColor Red; exit 1 }

try { [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12 } catch { }

Step "Installing the mosh launcher for Windows"

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

if (-not $Version) {
    try {
        $rel = Invoke-RestMethod -Uri "https://api.github.com/repos/$Repo/releases/latest" -UseBasicParsing
        $Version = $rel.tag_name
    } catch {
        Fail "could not reach the GitHub API to find the latest release -- $_"
    }
}
Info "Release $Version"

$zipName = "mosh-windows-launcher-$Version.zip"
$zipUrl  = "https://github.com/$Repo/releases/download/$Version/$zipName"

Step "Downloading $zipName"
$tmpZip = Join-Path ([System.IO.Path]::GetTempPath()) ("mosh-" + [Guid]::NewGuid().ToString('N') + '.zip')
try {
    Invoke-WebRequest -Uri $zipUrl -OutFile $tmpZip -UseBasicParsing
} catch {
    Fail "could not download $zipUrl -- $_"
}

Step "Installing into $Dir"
New-Item -ItemType Directory -Path $Dir -Force | Out-Null
$tmpDir = Join-Path ([System.IO.Path]::GetTempPath()) ("mosh-x-" + [Guid]::NewGuid().ToString('N'))
try {
    Expand-Archive -LiteralPath $tmpZip -DestinationPath $tmpDir -Force
    # The archive has a single top-level mosh\ directory.
    $src = Join-Path $tmpDir 'mosh'
    if (-not (Test-Path $src)) { $src = $tmpDir }
    Get-ChildItem -LiteralPath $src -File | ForEach-Object {
        Copy-Item -LiteralPath $_.FullName -Destination (Join-Path $Dir $_.Name) -Force
        Info $_.Name
    }
} finally {
    Remove-Item -LiteralPath $tmpZip -Force -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath $tmpDir -Recurse -Force -ErrorAction SilentlyContinue
}

# A tiny uninstaller, so removing this is as easy as installing it.
@"
@echo off
rem Uninstall the mosh launcher installed by install.ps1.
echo Removing "$Dir" and its PATH entry.
powershell -NoProfile -Command ^
  "`$p=[Environment]::GetEnvironmentVariable('Path','User'); ^
   `$p=(`$p -split ';' ^| Where-Object { `$_ -and `$_ -ne '$Dir' }) -join ';'; ^
   [Environment]::SetEnvironmentVariable('Path',`$p,'User')"
rmdir /s /q "$Dir"
echo Done. Open a new terminal for the PATH change to take effect.
"@ | Set-Content -Encoding ASCII (Join-Path $Dir 'mosh-uninstall.cmd')
Info "mosh-uninstall.cmd"

# ------------------------------------------------------------------- PATH --

if (-not $NoPath) {
    $userPath = [Environment]::GetEnvironmentVariable('Path', 'User')
    if (($userPath -split ';') -notcontains $Dir) {
        Step "Adding $Dir to your PATH"
        $new = if ([string]::IsNullOrEmpty($userPath)) { $Dir } else { "$userPath;$Dir" }
        [Environment]::SetEnvironmentVariable('Path', $new, 'User')
        $env:Path = "$env:Path;$Dir"
        Info "done (only the user PATH; the system PATH is untouched)"
    } else {
        Info "already on PATH"
    }
}

# ----------------------------------------------------------------- client --

if (-not $NoClient) {
    Step "Fetching a mosh-client"
    & powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $Dir 'mosh.ps1') --setup
    if ($LASTEXITCODE -ne 0) {
        Warn "client setup failed; run 'mosh --setup' yourself later."
    }
}

# ------------------------------------------------------------------- done --

Write-Host ""
Step "Installed"
Write-Host @"
  Open a NEW terminal, then:

      mosh user@host

  The remote host needs mosh-server installed (apt install mosh, brew install
  mosh, dnf install mosh). Nothing else is required there.

      mosh --help          all options
      mosh-uninstall       remove everything this installed
"@
