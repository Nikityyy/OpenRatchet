[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [ValidateSet('ALL_BUILD', 'openratchet', 'native_replacements_tests', 'native_vfs_tests', 'wad_decompressor_tests', 'sif_startup_responses_tests', 'sif_rpc_transport_tests')]
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

# Patch ZIPs deliberately contain only changed files. Archive extraction can
# preserve an older LastWriteTime than an object file already present under
# build/native. Both CMake and MSBuild are timestamp-driven, so that can cause a
# successful-looking build to silently reuse a binary from the previous phase.
#
# Git is the authoritative phase boundary: after each accepted phase is
# committed, applying the next patch leaves exactly its changed/new files dirty.
# Touch build-relevant dirty files before configuring so incremental MSBuild
# cannot mistake patched source for an older input.
$git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $git) {
    $git = Get-Command git -ErrorAction SilentlyContinue
}
if (-not $git) {
    throw 'git was not found on PATH; cannot establish dirty build inputs safely.'
}

$dirtyRelativePaths = @()
$dirtyRelativePaths += & $git.Source -C $root diff --name-only --diff-filter=ACMRTUXB --
if ($LASTEXITCODE -ne 0) { throw 'git diff failed while determining dirty build inputs.' }
$dirtyRelativePaths += & $git.Source -C $root diff --cached --name-only --diff-filter=ACMRTUXB --
if ($LASTEXITCODE -ne 0) { throw 'git diff --cached failed while determining dirty build inputs.' }
$dirtyRelativePaths += & $git.Source -C $root ls-files --others --exclude-standard
if ($LASTEXITCODE -ne 0) { throw 'git ls-files failed while determining untracked build inputs.' }

$buildInputExtensions = @(
    '.c', '.cc', '.cpp', '.cxx',
    '.h', '.hh', '.hpp', '.hxx', '.inl',
    '.cmake', '.toml', '.json', '.csv'
)
$dirtyBuildInputs = @()
foreach ($relativePath in ($dirtyRelativePaths | Sort-Object -Unique)) {
    if (-not $relativePath) { continue }
    $candidate = Join-Path $root ($relativePath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }

    $leaf = Split-Path -Leaf $candidate
    $extension = [IO.Path]::GetExtension($candidate).ToLowerInvariant()
    if ($leaf -eq 'CMakeLists.txt' -or $buildInputExtensions -contains $extension) {
        $dirtyBuildInputs += $candidate
    }
}

if ($dirtyBuildInputs.Count -gt 0) {
    $touchTime = [DateTime]::UtcNow
    foreach ($inputPath in $dirtyBuildInputs) {
        (Get-Item -LiteralPath $inputPath).LastWriteTimeUtc = $touchTime
    }
    Write-Host "Refreshed timestamps for $($dirtyBuildInputs.Count) dirty build input(s)."
}

# Always invoke configure explicitly. This is intentionally cheap compared with
# compiling the generated game and guarantees that CMake reads the current
# project contents even when an extracted CMakeLists.txt has an old timestamp.
$cmake = Get-Command cmake.exe -ErrorAction SilentlyContinue
if (-not $cmake) {
    $cmake = Get-Command cmake -ErrorAction SilentlyContinue
}
if (-not $cmake) {
    throw 'cmake was not found on PATH.'
}

Write-Host 'Configuring CMake project before MSBuild...'
& $cmake.Source -S $root -B $buildDir
if ($LASTEXITCODE -ne 0) {
    throw "CMake configure failed with exit code $LASTEXITCODE."
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
