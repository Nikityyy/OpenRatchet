[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$cachePath = Join-Path $root 'build\native\CMakeCache.txt'
$project = Join-Path $root 'build\native\openratchet.vcxproj'
$elf = Join-Path $root 'build\extracted\PS2_MAIN.ELF'
$log = Join-Path $root "build\native\build-$Configuration.log"

foreach ($required in @(
    $cachePath,
    $project,
    $elf,
    (Join-Path $root 'generated'),
    (Join-Path $root 'third_party\PS2Recomp')
)) {
    if (-not (Test-Path -LiteralPath $required)) {
        throw "Missing required build input: $required"
    }
}

$cmakeRoot = $root.Replace('\', '/')
$cache = Get-Content -Raw -LiteralPath $cachePath
foreach ($expected in @(
    "CMAKE_HOME_DIRECTORY:INTERNAL=$cmakeRoot",
    "PS2RECOMP_SOURCE_DIR:PATH=$cmakeRoot/third_party/PS2Recomp",
    "RATCHET_BOOT_ELF:FILEPATH=$cmakeRoot/build/extracted/PS2_MAIN.ELF",
    'CMAKE_GENERATOR:INTERNAL=Visual Studio 18 2026'
)) {
    if ($cache -notmatch "(?m)^$([regex]::Escape($expected))`r?$") {
        throw "Stale CMake cache: expected '$expected'."
    }
}

$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
if (-not (Test-Path -LiteralPath $vswhere)) {
    throw "vswhere.exe not found at '$vswhere'."
}
$msbuild = & $vswhere -latest -products '*' `
    -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 `
    -find 'MSBuild\Current\Bin\MSBuild.exe' | Select-Object -First 1
if (-not $msbuild -or -not (Test-Path -LiteralPath $msbuild)) {
    throw 'Visual Studio MSBuild with C++ tools was not found.'
}

$pathValue = $env:Path
$env:PATH = $null
$env:Path = $pathValue

Write-Host "MSBuild: $msbuild"
Write-Host "Project: $project"
Write-Host "Log:     $log"
$timer = [Diagnostics.Stopwatch]::StartNew()
& $msbuild $project `
    "/p:Configuration=$Configuration" `
    /p:Platform=x64 `
    /m `
    /v:minimal `
    "/flp:logfile=$log;verbosity=normal"
$exitCode = $LASTEXITCODE
$timer.Stop()

Write-Host "Elapsed: $($timer.Elapsed)"
if ($exitCode -ne 0) {
    throw "Native build failed with exit code $exitCode. See $log"
}
