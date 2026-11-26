# Package the tested x64 binaries for GitHub Releases and the portable installer.
[CmdletBinding()]
param(
    [Parameter(Mandatory)][string] $BuildDir,
    [Parameter(Mandatory)][string] $OutDir,
    [ValidatePattern('^(dev|v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*))$')][string] $Version = 'dev'
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$out = (New-Item -ItemType Directory -Force -Path $OutDir).FullName
$temp = Join-Path ([IO.Path]::GetTempPath()) ("mosh-package-" + [guid]::NewGuid())
$stage = (New-Item -ItemType Directory -Path "$temp/mosh" -Force).FullName
try {
    $binaries = @('mosh.exe','mosh-client.exe','mosh-server.exe')
    foreach ($name in $binaries) {
        $found = @(Get-ChildItem -LiteralPath $BuildDir -Recurse -File -Filter $name)
        if ($found.Count -ne 1) { throw "Expected exactly one $name in $BuildDir, found $($found.Count)." }
        Copy-Item -LiteralPath $found[0].FullName -Destination $stage
        Copy-Item -LiteralPath "$stage/$name" -Destination $out -Force
    }
    Copy-Item -LiteralPath "$PSScriptRoot/mosh.ps1","$PSScriptRoot/mosh-update.ps1","$PSScriptRoot/mosh.cmd","$root/COPYING","$root/README.windows.md" -Destination $stage
    Set-Content -LiteralPath "$stage/mosh-version.txt" -Value $Version -Encoding ASCII
    $zip = "mosh-windows-x64-$Version.zip"
    Compress-Archive -LiteralPath $stage -DestinationPath "$out/$zip" -CompressionLevel Optimal -Force
    $checksums = foreach ($name in ($binaries + $zip)) {
        (Get-FileHash -LiteralPath "$out/$name" -Algorithm SHA256).Hash.ToLowerInvariant() + "  " + $name
    }
    [IO.File]::WriteAllText("$out/SHA256SUMS", ($checksums -join "`n") + "`n", [Text.Encoding]::ASCII)
} finally { Remove-Item -LiteralPath $temp -Recurse -Force }
