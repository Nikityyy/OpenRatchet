[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [ValidateSet('ALL_BUILD', 'openratchet', 'native_replacements_tests', 'sif_startup_responses_tests', 'sif_rpc_transport_tests')]
    [string]$Target = 'ALL_BUILD',
    # Use only when generated project metadata has changed and MSBuild's
    # incremental tracking has not picked up a newly added source file.
    [switch]$Rebuild
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$buildDir = Join-Path $root 'build\native'
$cachePath = Join-Path $buildDir 'CMakeCache.txt'
$project = Join-Path $buildDir "$Target.vcxproj"
$generateStamp = Join-Path $buildDir 'CMakeFiles\generate.stamp'
$rootCmake = Join-Path $root 'CMakeLists.txt'
$elf = Join-Path $root 'build\extracted\PS2_MAIN.ELF'
$log = Join-Path $buildDir "build-$Configuration.log"

foreach ($required in @(
    $cachePath,
    $rootCmake,
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

# CMake can regenerate Visual Studio project files from ZERO_CHECK while
# MSBuild is already running. A target added by that regeneration is not part of
# the dependency graph MSBuild loaded at process start, so ALL_BUILD can finish
# successfully without building the new target. Configure first whenever the
# root project changed (or the requested .vcxproj does not exist), then start
# MSBuild against the fresh project graph.
$needsConfigure = -not (Test-Path -LiteralPath $project) -or `
    -not (Test-Path -LiteralPath $generateStamp) -or `
    ((Get-Item -LiteralPath $rootCmake).LastWriteTimeUtc -gt
        (Get-Item -LiteralPath $generateStamp).LastWriteTimeUtc)

if ($needsConfigure) {
    $cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
    if (-not $cmake) {
        $cmake = Get-Command cmake -ErrorAction SilentlyContinue
    }
    if (-not $cmake) {
        throw 'cmake was not found on PATH.'
    }

    Write-Host 'CMake project metadata is stale; configuring before MSBuild...'
    & $cmake.Source -S $root -B $buildDir
    if ($LASTEXITCODE -ne 0) {
        throw "CMake configure failed with exit code $LASTEXITCODE."
    }
}

if (-not (Test-Path -LiteralPath $project)) {
    throw "Missing generated Visual Studio project after configure: $project"
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
    $(if ($Rebuild) { '/t:Rebuild' }) `
    /m `
    /v:minimal `
    "/flp:logfile=$log;verbosity=normal"
$exitCode = $LASTEXITCODE
$timer.Stop()

Write-Host "Elapsed: $($timer.Elapsed)"
if ($exitCode -ne 0) {
    throw "Native build failed with exit code $exitCode. See $log"
}
