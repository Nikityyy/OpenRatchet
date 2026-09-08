[CmdletBinding()]
param(
    [int]$LevelIndex = 0,
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [ValidateRange(0, 3600)]
    [double]$SmokeSeconds = 0
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
if ($SmokeSeconds -gt 0) {
    Write-Host ("Mode:   automated smoke ({0} seconds)" -f $SmokeSeconds)
} else {
    Write-Host 'Controls: free camera (WASD/mouse/wheel), TAB wireframe, ESC close'
}

$viewerArgs = @($toc, $extracted, $LevelIndex)
if ($SmokeSeconds -gt 0) {
    $viewerArgs += $SmokeSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)
}

& $viewer @viewerArgs
if ($LASTEXITCODE -ne 0) {
    throw "Native level viewer failed with exit code $LASTEXITCODE."
}
