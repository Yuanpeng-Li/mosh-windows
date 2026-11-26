# Use the release ZIP and the normal PowerShell launcher. Stub only GitHub.
param([Parameter(Mandatory)][string] $Archive)
$ErrorActionPreference = 'Stop'
$root = Join-Path ([IO.Path]::GetTempPath()) ("mosh-update-test-" + [guid]::NewGuid())
$null = New-Item -ItemType Directory -Path $root
$releaseTag = 'v0.10.0'
$previous = 'v0.9.0'
$fixture = Join-Path $root 'release.zip'
$install = Join-Path $root "owner's tools/mosh"
$launcher = Join-Path $install 'mosh.ps1'
$versionFile = Join-Path $install 'mosh-version.txt'
$downloads = [Collections.Generic.List[string]]::new()
function Invoke-RestMethod {
    param($Uri, [switch] $UseBasicParsing)
    if ($Uri -ne 'https://api.github.com/repos/Yuanpeng-Li/mosh-windows/releases/latest') { throw "Unexpected URL: $Uri" }
    return @{ tag_name=$releaseTag; draft=$false; prerelease=$false
              assets=@(@{ name="mosh-windows-x64-$releaseTag.zip"; digest="sha256:$digest" }) }
}
function Invoke-WebRequest {
    param($Uri, $OutFile, [switch] $UseBasicParsing)
    if ($Uri -ne "https://github.com/Yuanpeng-Li/mosh-windows/releases/download/$releaseTag/mosh-windows-x64-$releaseTag.zip") {
        throw "Unexpected asset URL: $Uri"
    }
    $downloads.Add($Uri)
    Copy-Item -LiteralPath $fixture -Destination $OutFile
}
try {
    Expand-Archive -LiteralPath $Archive -DestinationPath "$root/template"
    Set-Content -LiteralPath "$root/template/mosh/mosh-version.txt" -Value $releaseTag -Encoding ASCII
    Compress-Archive -LiteralPath "$root/template/mosh" -DestinationPath $fixture
    Expand-Archive -LiteralPath $fixture -DestinationPath (Split-Path -Parent $install)
    Set-Content -LiteralPath $versionFile -Value $previous -Encoding ASCII
    Set-Content -LiteralPath "$install/README.windows.md" -Value 'previous readme'
    Set-Content -LiteralPath "$install/keep.txt" -Value 'user data'
    # Ensure the running launcher itself needs replacement.
    Add-Content -LiteralPath "$install/mosh.exe" -Value 'old build' -Encoding ASCII
    $digest = '0' * 64
    & $launcher update --check
    if ($LASTEXITCODE -or $downloads.Count) { throw 'Version check downloaded a release or failed' }
    & $launcher update
    if (!$LASTEXITCODE -or (Get-Content $versionFile) -ne $previous) { throw 'Bad digest accepted' }

    $digest = (Get-FileHash -LiteralPath $fixture).Hash
    # A later locked file must roll back the earlier README replacement.
    Add-Content -LiteralPath "$install/mosh-client.exe" -Value 'old client' -Encoding ASCII
    $locked = [IO.File]::Open("$install/mosh-client.exe", 'Open', 'Read', 'Read')
    try { & $launcher update } finally { $locked.Dispose() }
    if (!$LASTEXITCODE -or (Get-Content -LiteralPath "$install/README.windows.md") -ne 'previous readme' -or
        (Get-Content -LiteralPath $versionFile) -ne $previous) { throw 'Failed update did not roll back' }

    # Exercise the installed native entry point while its executable is
    # mapped. Only the old test launcher's HTTP calls see the fixture.
    $stub = @'
$fixture = Join-Path $PSScriptRoot '../../release.zip'
$releaseTag = 'v0.10.0'
$digest = (Get-FileHash -LiteralPath $fixture).Hash
$downloads = [Collections.Generic.List[string]]::new()
'@
    foreach ($name in @('Invoke-RestMethod','Invoke-WebRequest')) {
        $stub += "`nfunction $name { " + (Get-Command $name).Definition + "`n}"
    }
    $oldScript = Get-Content -LiteralPath $launcher -Raw
    Set-Content -LiteralPath $launcher -Encoding UTF8 -Value ($stub + "`n" + $oldScript)
    & "$install/mosh.exe" update
    if ($LASTEXITCODE -or (Get-Content -LiteralPath $versionFile) -ne $releaseTag) { throw 'Upgrade failed' }
    foreach ($file in (Get-ChildItem -LiteralPath "$root/template/mosh" -File)) {
        if ((Get-FileHash -LiteralPath "$install/$($file.Name)").Hash -ne (Get-FileHash -LiteralPath $file.FullName).Hash) {
            throw "Package mismatch: $($file.Name)"
        }
    }
    $count = $downloads.Count
    & $launcher update
    if ($LASTEXITCODE -or $downloads.Count -ne $count -or (Get-Content -LiteralPath "$install/keep.txt") -ne 'user data' -or
        (Get-ChildItem -LiteralPath $install -Force -Filter '.mosh-update*')) { throw 'Update was not idempotent' }
    if ((& "$install/mosh.exe" --version | Out-String) -notmatch [regex]::Escape($releaseTag)) { throw 'Wrong launcher version' }
    & "$install/mosh.exe" update --invalid
    if (!$LASTEXITCODE) { throw 'Native launcher accepted an invalid update option' }

    # Simulate a process stopping between atomic file replacements.
    $id = [guid]::NewGuid().ToString('N')
    $work = Join-Path $install ".mosh-update-$id"
    $null = New-Item -ItemType Directory "$work/backup" -Force
    Copy-Item "$install/README.windows.md" "$work/backup/README.windows.md"
    Set-Content "$install/README.windows.md" 'interrupted write'
    @{ id=$id; tag='v0.11.0' } | ConvertTo-Json | Set-Content "$install/.mosh-update.pending"
    & $launcher update
    if ($LASTEXITCODE -or (Test-Path $work) -or (Test-Path "$install/.mosh-update.pending") -or
        (Get-FileHash "$install/README.windows.md").Hash -ne (Get-FileHash "$root/template/mosh/README.windows.md").Hash) {
        throw 'Interrupted update did not recover'
    }
} finally {
    Remove-Item -LiteralPath $root -Recurse -Force
}
