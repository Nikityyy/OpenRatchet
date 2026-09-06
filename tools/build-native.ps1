[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',
    [ValidateSet('ALL_BUILD', 'openratchet', 'native_replacements_tests', 'native_vfs_tests', 'native_io_tests', 'rac1_level_tests', 'rac1_collision_tests', 'rac1_moby_tests', 'native_level_inspector', 'native_level_viewer', 'wad_decompressor_tests', 'sif_startup_responses_tests', 'sif_rpc_transport_tests')]
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
# build/native. CMake/MSBuild are timestamp-driven, so a newly extracted source
# can otherwise be silently older than a stale object from the previous phase.
#
# Do NOT touch every Git-dirty file on every build: long-running phases keep the
# same files dirty across many validation cycles, and repeatedly touching a
# public header invalidates the OpenRatchet PCH and recompiles hundreds of
# generated Retail translation units. Instead, remember the SHA-256 of each
# dirty build input. A file is touched only when its content differs from the
# last content this build tree has seen.
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
    '.cmake', '.toml', '.json', '.csv', '.patch', '.txt'
)
$dirtyBuildInputs = @()
foreach ($relativePath in ($dirtyRelativePaths | Sort-Object -Unique)) {
    if (-not $relativePath) { continue }
    $candidate = Join-Path $root ($relativePath -replace '/', '\')
    if (-not (Test-Path -LiteralPath $candidate -PathType Leaf)) { continue }

    $leaf = Split-Path -Leaf $candidate
    $extension = [IO.Path]::GetExtension($candidate).ToLowerInvariant()
    if ($leaf -eq 'CMakeLists.txt' -or $buildInputExtensions -contains $extension) {
        $dirtyBuildInputs += [PSCustomObject]@{
            RelativePath = ($relativePath -replace '\\', '/')
            FullPath = $candidate
        }
    }
}

$hashStatePath = Join-Path $buildDir '.openratchet-build-input-hashes.json'
$hashState = @{}
if (Test-Path -LiteralPath $hashStatePath) {
    try {
        $decoded = Get-Content -Raw -LiteralPath $hashStatePath | ConvertFrom-Json
        if ($decoded) {
            foreach ($property in $decoded.PSObject.Properties) {
                $hashState[$property.Name] = [string]$property.Value
            }
        }
    } catch {
        Write-Warning "Ignoring unreadable build-input hash state '$hashStatePath': $($_.Exception.Message)"
        $hashState = @{}
    }
}

$changedDirtyInputs = @()
foreach ($input in $dirtyBuildInputs) {
    $currentHash = (Get-FileHash -LiteralPath $input.FullPath -Algorithm SHA256).Hash.ToLowerInvariant()
    $previousHash = $hashState[$input.RelativePath]
    if ($previousHash -ne $currentHash) {
        $changedDirtyInputs += $input
        $hashState[$input.RelativePath] = $currentHash
    }
}

if ($changedDirtyInputs.Count -gt 0) {
    $touchTime = [DateTime]::UtcNow
    foreach ($input in $changedDirtyInputs) {
        (Get-Item -LiteralPath $input.FullPath).LastWriteTimeUtc = $touchTime
    }
    Write-Host "Refreshed timestamps for $($changedDirtyInputs.Count) content-changed dirty build input(s)."
} elseif ($dirtyBuildInputs.Count -gt 0) {
    Write-Host "Dirty build inputs unchanged by content; timestamps left intact."
}

# Persist atomically. Keep historical hashes: if a committed file later becomes
# dirty with exactly the already-built content, no artificial rebuild is needed.
$hashStateParent = Split-Path -Parent $hashStatePath
if (-not (Test-Path -LiteralPath $hashStateParent)) {
    New-Item -ItemType Directory -Path $hashStateParent -Force | Out-Null
}
$stateTempPath = "$hashStatePath.tmp"
$hashState | ConvertTo-Json -Compress | Set-Content -LiteralPath $stateTempPath -NoNewline -Encoding UTF8
Move-Item -LiteralPath $stateTempPath -Destination $hashStatePath -Force

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
