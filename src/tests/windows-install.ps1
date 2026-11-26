# Run against the assembled release ZIP in the Windows CI job.
param([string] $Archive)
$ErrorActionPreference = 'Stop'
$installer = Join-Path $PSScriptRoot '../../scripts/install.ps1'
$root = Join-Path ([IO.Path]::GetTempPath()) ("mosh-install-test-" + [guid]::NewGuid())
$shared = Join-Path $root "owner's tools"
$fresh = Join-Path $root 'fresh'
$originalPath = [Environment]::GetEnvironmentVariable('Path', 'User')
try {
    New-Item -Path $shared -ItemType Directory -Force | Out-Null
    Set-Content -LiteralPath (Join-Path $shared 'keep.txt') 'keep'
    $existingPath = "$originalPath;$shared"
    [Environment]::SetEnvironmentVariable('Path', $existingPath, 'User')
    & $installer -FromZip $Archive -Dir $shared
    & $installer -FromZip $Archive -Dir $shared -NoPath
    & (Join-Path $shared 'mosh-uninstall.cmd')
    if ($LASTEXITCODE -ne 0 -or @(Get-ChildItem -LiteralPath $shared -Force).Count -ne 1 -or
        (Get-Content -LiteralPath (Join-Path $shared 'keep.txt')) -ne 'keep' -or
        [Environment]::GetEnvironmentVariable('Path', 'User') -cne $existingPath) {
        throw 'Uninstall did not preserve the shared directory and its original PATH entry'
    }

    # A shared directory can also contain a file whose name matches an
    # archive member. Reject that install before writing any package files.
    $collision = Join-Path $shared 'COPYING'
    Set-Content -LiteralPath $collision 'belongs to another program'
    $rejected = $false
    try { & $installer -FromZip $Archive -Dir $shared } catch { $rejected = $true }
    if (-not $rejected -or (Get-Content -LiteralPath $collision) -ne 'belongs to another program' -or
        @(Get-ChildItem -LiteralPath $shared -Force).Count -ne 2) {
        throw 'Installer overwrote files in a shared directory'
    }

    & $installer -FromZip $Archive -Dir $fresh
    & $installer -FromZip $Archive -Dir $fresh -NoPath
    if (([Environment]::GetEnvironmentVariable('Path', 'User') -split ';') -notcontains $fresh) {
        throw 'Installer did not add its directory to PATH'
    }
    & (Join-Path $fresh 'mosh-uninstall.cmd')
    if ($LASTEXITCODE -ne 0 -or (Test-Path -LiteralPath $fresh) -or
        [Environment]::GetEnvironmentVariable('Path', 'User') -cne $existingPath) {
        throw 'Uninstall did not remove the owned directory and PATH entry'
    }
} finally {
    [Environment]::SetEnvironmentVariable('Path', $originalPath, 'User')
    if (Test-Path -LiteralPath $root) { Remove-Item -LiteralPath $root -Recurse -Force }
}
