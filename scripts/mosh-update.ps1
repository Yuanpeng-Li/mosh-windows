# Invoked by the Windows launcher. A release always updates as one bundle.
param([switch] $Check)
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$repo = 'Yuanpeng-Li/mosh-windows'
$tagPattern = '^v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$'
$versionFile = Join-Path $PSScriptRoot 'mosh-version.txt'
$journal = Join-Path $PSScriptRoot '.mosh-update.pending'
# The version marker is the last atomic replacement and commits the update.
$files = @('COPYING','README.windows.md','mosh-client.exe','mosh-server.exe',
           'mosh.exe','mosh.cmd','mosh.ps1','mosh-update.ps1','mosh-version.txt')
$noBackup = [System.Management.Automation.Language.NullString]::Value

function Complete-Transaction {
    $record = Get-Content -LiteralPath $journal -Raw | ConvertFrom-Json
    if ($record.id -notmatch '^[0-9a-f]{32}$' -or $record.tag -cnotmatch $tagPattern) {
        throw "Invalid update journal at $journal"
    }
    $work = Join-Path $PSScriptRoot ('.mosh-update-' + $record.id)
    if ((Get-Content -LiteralPath $versionFile -Raw).Trim() -cne $record.tag) {
        # Replace consumes its source. A second recovery skips files that
        # the first recovery already restored. Files still in use but never
        # changed need no write access.
        foreach ($name in $files) {
            $backup = Join-Path "$work/backup" $name
            $dest = Join-Path $PSScriptRoot $name
            if ([IO.File]::Exists($backup) -and
                (Get-FileHash -LiteralPath $backup).Hash -ne (Get-FileHash -LiteralPath $dest).Hash) {
                [IO.File]::Replace($backup, $dest, $noBackup)
            }
        }
    }
    [IO.File]::Delete($journal)
    Remove-Item -LiteralPath $work -Recurse -Force
}

if (!(Test-Path -LiteralPath $versionFile -PathType Leaf)) {
    throw 'This is a source build. Install a Windows release to use mosh update.'
}
$sha = [Security.Cryptography.SHA256]::Create()
try {
    $hash = $sha.ComputeHash([Text.Encoding]::UTF8.GetBytes($PSScriptRoot.ToUpperInvariant()))
} finally { $sha.Dispose() }
$mutex = [Threading.Mutex]::new($false, ('Global\MoshWindowsUpdate-' + [BitConverter]::ToString($hash)))
$locked = $false
try {
    try { $locked = $mutex.WaitOne(0) } catch [Threading.AbandonedMutexException] { $locked = $true }
    if (!$locked) { throw 'Another update is in progress.' }
    if (Test-Path -LiteralPath $journal) {
        if ($Check) { throw 'An update was interrupted. Run mosh update to recover it.' }
        Complete-Transaction
    }
    $current = (Get-Content -LiteralPath $versionFile -Raw).Trim()
    if ($current -cnotmatch $tagPattern) { throw "Self-update requires a release build (installed: $current)." }
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/$repo/releases/latest" -UseBasicParsing
    $tag = $release.tag_name
    if ($release.draft -or $release.prerelease -or $tag -cnotmatch $tagPattern) {
        throw 'GitHub did not return a stable Windows release.'
    }
    if ([version]$current.Substring(1) -ge [version]$tag.Substring(1)) {
        Write-Output "Installed $current. No newer stable release (latest: $tag)."
        return
    }
    Write-Output "Update available: $current -> $tag"
    if ($Check) { return }
    if ($PSScriptRoot -match '[\\/]YuanpengLi\.MoshWindows_[^\\/]+([\\/]|$)') {
        & winget.exe upgrade --id YuanpengLi.MoshWindows --exact
        if ($LASTEXITCODE) { throw "winget upgrade exited with status $LASTEXITCODE" }
        return
    }

    $zipName = "mosh-windows-x64-$tag.zip"
    $asset = @($release.assets | Where-Object name -ceq $zipName)
    if ($asset.Count -ne 1 -or $asset[0].digest -notmatch '^sha256:([0-9a-f]{64})$') {
        throw "Missing $zipName or its SHA-256 digest in release $tag."
    }
    $expected = $Matches[1]
    $id = [Guid]::NewGuid().ToString('N')
    $work = Join-Path $PSScriptRoot ".mosh-update-$id"
    $null = New-Item -ItemType Directory -Path $work
    try {
        $zip = Join-Path $work 'download.zip'
        Invoke-WebRequest -Uri "https://github.com/$repo/releases/download/$tag/$zipName" -OutFile $zip -UseBasicParsing
        if ((Get-FileHash -LiteralPath $zip -Algorithm SHA256).Hash -ne $expected) { throw 'ZIP SHA-256 mismatch.' }
        Expand-Archive -LiteralPath $zip -DestinationPath "$work/unpacked"
        $src = Join-Path $work 'unpacked/mosh'
        $members = @(Get-ChildItem -LiteralPath $src -Force)
        if (($members | Where-Object PSIsContainer) -or (Compare-Object $files $members.Name) -or
            (Get-Content -LiteralPath "$src/mosh-version.txt" -Raw).Trim() -cne $tag) {
            throw 'The release archive has an unexpected layout or version.'
        }
        $backupDir = New-Item -ItemType Directory -Path "$work/backup"
        $changed = @(foreach ($name in $files) {
            $dest = Join-Path $PSScriptRoot $name
            if ((Get-FileHash -LiteralPath $dest).Hash -ne (Get-FileHash -LiteralPath "$src/$name").Hash) {
                [IO.File]::Copy($dest, (Join-Path $backupDir.FullName $name))
                $name
            }
        })
        # Publish the journal only after every backup is ready. Copy backups
        # first so running Windows images can be replaced without retaining a
        # locked backup of the mapped executable.
        @{ id = $id; tag = $tag } | ConvertTo-Json | Set-Content -LiteralPath "$work/pending" -Encoding ASCII
        [IO.File]::Move("$work/pending", $journal)
        try {
            foreach ($name in $changed) {
                [IO.File]::Replace((Join-Path $src $name), (Join-Path $PSScriptRoot $name), $noBackup)
            }
        } finally { Complete-Transaction }
        Write-Output "Updated Mosh for Windows to $tag."
    } finally {
        if (!(Test-Path -LiteralPath $journal) -and (Test-Path -LiteralPath $work)) {
            Remove-Item -LiteralPath $work -Recurse -Force
        }
    }
} finally {
    if ($locked) { $mutex.ReleaseMutex() }
    $mutex.Dispose()
}
