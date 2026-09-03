[CmdletBinding()]
param(
    [int]$LevelIndex = 0,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$viewer = Join-Path $root "build\native\$Configuration\native_level_viewer.exe"
$toc = Join-Path $root 'build\toc.json'
$extracted = Join-Path $root 'build\extracted'
$levelFile = Join-Path $extracted ("levels\level_{0:D2}.wad" -f $LevelIndex)

foreach ($required in @($viewer, $toc, $extracted, $levelFile)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing native level viewer input: $required"
    }
}

Write-Host "Viewer: $viewer"
Write-Host "Level:  $levelFile"
Write-Host 'Controls: free camera (WASD/mouse/wheel), TAB wireframe, ESC close'
& $viewer $toc $extracted $LevelIndex
if ($LASTEXITCODE -ne 0) {
    throw "Native level viewer failed with exit code $LASTEXITCODE."
}
