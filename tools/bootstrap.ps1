[CmdletBinding()]
param(
    [ValidateSet('Extract', 'Recompile', 'Build', 'All')]
    [string]$Stage = 'All',
    [string]$Iso,
    [string]$PS2RecompDir,
    [string]$TocParserDir,
    [string]$WadutilDir,
    [string]$GhidraDir = 'C:\ghidra_12.1.2_PUBLIC_20260605\ghidra_12.1.2_PUBLIC',
    [int]$Jobs = 0,
    [string]$Elf,
    [string]$Config,
    [switch]$FetchTools,
    [switch]$UnpackWads
)

$ErrorActionPreference = 'Stop'
$buildJobs = if ($Jobs -gt 0) { $Jobs } else { [Environment]::ProcessorCount }
$root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$ps2RecompRevisionFile = Join-Path $root 'patches\ps2recomp-base-revision.txt'
if (-not (Test-Path -LiteralPath $ps2RecompRevisionFile)) { throw "Missing PS2Recomp revision pin '$ps2RecompRevisionFile'." }
$ps2RecompRevision = (Get-Content -Raw -LiteralPath $ps2RecompRevisionFile).Trim()
if (-not $ps2RecompRevision) { throw "PS2Recomp revision pin is empty: '$ps2RecompRevisionFile'." }
if (-not $Iso) { $Iso = Join-Path $root 'games\Ratchet & Clank (USA) (En,Fr,De,Es,It).iso' }
if (-not $PS2RecompDir) { $PS2RecompDir = Join-Path $root 'third_party\PS2Recomp' }
if (-not $TocParserDir) { $TocParserDir = Join-Path $root 'third_party\rac-dvd-toc-parser' }
if (-not $WadutilDir) { $WadutilDir = Join-Path $root 'third_party\wadutil' }
if (-not $Elf) { $Elf = Join-Path $root 'build\extracted\PS2_MAIN.ELF' }
if (-not $Config) { $Config = Join-Path $root 'build\game.toml' }
$iso = (Resolve-Path $Iso -ErrorAction SilentlyContinue).Path
if (-not $iso) { throw "ISO not found. Supply -Iso <path>." }

$ghidraRun = Join-Path $GhidraDir 'ghidraRun.bat'
$analyzeHeadless = Join-Path $GhidraDir 'support\analyzeHeadless.bat'
$ghidraVersion = Split-Path $GhidraDir -Leaf
$emotionEngineExtension = Join-Path $env:APPDATA "ghidra\$ghidraVersion\Extensions\ghidra-emotionengine-reloaded\extension.properties"

function Require-Command([string]$Name) {
    if (-not (Get-Command $Name -ErrorAction SilentlyContinue)) {
        throw "Required command '$Name' is not on PATH. Install it or use -FetchTools where supported."
    }
}
function Fetch-Repo([string]$Url, [string]$Path) {
    if (-not (Test-Path -LiteralPath (Split-Path $Path -Parent))) { New-Item -ItemType Directory (Split-Path $Path -Parent) | Out-Null }
    if (-not (Test-Path -LiteralPath $Path)) { git clone --depth 1 $Url $Path }
}
function Assert-CleanPinnedPs2Recomp([string]$Path) {
    Require-Command git
    if (-not (Test-Path -LiteralPath (Join-Path $Path 'CMakeLists.txt'))) {
        throw "PS2Recomp checkout not found at '$Path'. Use -FetchTools or supply -PS2RecompDir."
    }
    $head = (& git -C $Path rev-parse HEAD).Trim()
    if ($LASTEXITCODE -ne 0) { throw "Could not read PS2Recomp revision from '$Path'." }
    if ($head -ne $ps2RecompRevision) {
        throw "Unsupported PS2Recomp revision '$head'. Expected pinned revision '$ps2RecompRevision'."
    }
    $status = @(& git -C $Path status --porcelain --untracked-files=all)
    if ($LASTEXITCODE -ne 0) { throw "Could not inspect PS2Recomp status in '$Path'." }
    if ($status.Count -gt 0) {
        throw "third_party/PS2Recomp must remain clean. Restore local changes before building/recompiling. OpenRatchet applies its compatibility patch only to build/native/_openratchet/PS2Recomp.`n$($status -join [Environment]::NewLine)"
    }
}

if ($FetchTools) {
    Require-Command git
    Fetch-Repo 'https://github.com/maikelwever/rac-dvd-toc-parser.git' $TocParserDir
    Fetch-Repo 'https://github.com/stiantoften/wadutil.git' $WadutilDir
    Fetch-Repo 'https://github.com/ran-j/PS2Recomp.git' $PS2RecompDir
    & git -C $PS2RecompDir fetch origin $ps2RecompRevision --depth 1
    if ($LASTEXITCODE -ne 0) { throw "Failed to fetch pinned PS2Recomp revision '$ps2RecompRevision'." }
    & git -C $PS2RecompDir checkout --detach $ps2RecompRevision
    if ($LASTEXITCODE -ne 0) { throw "Failed to checkout pinned PS2Recomp revision '$ps2RecompRevision'." }
}

if ($Stage -in @('Recompile', 'Build', 'All')) {
    Assert-CleanPinnedPs2Recomp $PS2RecompDir
}

if (-not (Test-Path -LiteralPath $ghidraRun) -or -not (Test-Path -LiteralPath $analyzeHeadless)) {
    Write-Warning "Ghidra was not found under '$GhidraDir'. Pass -GhidraDir <directory> before performing the Ghidra export."
}
if (-not (Test-Path -LiteralPath $emotionEngineExtension)) {
    Write-Warning "ghidra-emotionengine-reloaded was not found for '$ghidraVersion'. Install the matching extension before importing the ELF."
}

if ($Stage -in @('Extract', 'All')) {
    Require-Command python
    if (-not (Test-Path -LiteralPath (Split-Path $Elf -Parent))) { New-Item -ItemType Directory (Split-Path $Elf -Parent) | Out-Null }
    $image = Mount-DiskImage -ImagePath $iso -PassThru
    try {
        $volume = $image | Get-Volume
        $discRoot = $volume.DriveLetter + ':\'
        $discElf = Get-ChildItem -LiteralPath $discRoot -File | Where-Object {
            $_.Name -eq 'PS2_MAIN.ELF' -or $_.Name -match '^SCUS[_-]'
        } | Select-Object -First 1
        if (-not $discElf) { throw 'Could not find PS2_MAIN.ELF or an SCUS executable in the mounted ISO.' }
        Copy-Item -LiteralPath $discElf.FullName -Destination $Elf -Force
    } finally {
        Dismount-DiskImage -ImagePath $iso
    }
    $toc = Join-Path $root 'build\toc.json'
    & python (Join-Path $PSScriptRoot 'rac_toc_parser.py') (Join-Path $TocParserDir 'tocparser.py') $iso --dumptoc $toc --outdir (Split-Path $Elf -Parent)
    if ($LASTEXITCODE -ne 0) { throw 'TOC extraction failed.' }
    if ($UnpackWads) {
        Require-Command cmake
        cmake -S $WadutilDir -B (Join-Path $WadutilDir 'build')
        cmake --build (Join-Path $WadutilDir 'build') --config Release
        Get-ChildItem (Join-Path (Split-Path $Elf -Parent) 'wads') -Filter '*.wad' | ForEach-Object {
            & (Join-Path $WadutilDir 'build\Release\wadutil.exe') $_.FullName
        }
    }
}

if ($Stage -in @('Recompile', 'All')) {
    Require-Command cmake
    if (-not (Test-Path -LiteralPath $Elf)) { throw "Missing ELF '$Elf'. Run with -Stage Extract first." }
    if (-not (Test-Path -LiteralPath $Config)) { throw "Missing TOML '$Config'. Export it from Ghidra first." }
    $generatedPath = (Join-Path $root 'generated').Replace('\', '/')
    $configText = [System.IO.File]::ReadAllText($Config)
    $configText = [regex]::Replace($configText, '(?m)^output\s*=.*$', "output = `"$generatedPath`"")
    [System.IO.File]::WriteAllText($Config, $configText)
    $toolBuild = Join-Path $PS2RecompDir 'build'
    cmake -S $PS2RecompDir -B $toolBuild -DPS2X_BUILD_RUNTIME=OFF -DPS2X_BUILD_TEST=OFF -DPS2X_BUILD_STUDIO=OFF
    if ($LASTEXITCODE -ne 0) { throw 'PS2Recomp configuration failed.' }
    cmake --build $toolBuild --config Release --target ps2_recomp --parallel $buildJobs
    if ($LASTEXITCODE -ne 0) { throw 'PS2Recomp build failed.' }
    $recompiler = Join-Path $toolBuild 'ps2xRecomp\Release\ps2_recomp.exe'
    if (-not (Test-Path -LiteralPath $recompiler)) { throw "Recompiler not found at '$recompiler'." }
    & $recompiler $Config
    if ($LASTEXITCODE -ne 0) { throw 'Static recompilation failed.' }
}

if ($Stage -in @('Build', 'All')) {
    Require-Command cmake
    if (-not (Test-Path -LiteralPath $Elf)) { throw "Missing ELF '$Elf'. Run with -Stage Extract first." }
    $nativeBuild = Join-Path $root 'build\native'
    $cache = Join-Path $nativeBuild 'CMakeCache.txt'
    if (Test-Path -LiteralPath $cache) {
        $cachedRoot = (Select-String -LiteralPath $cache -Pattern '^CMAKE_HOME_DIRECTORY:INTERNAL=(.+)$').Matches.Groups[1].Value
        if ($cachedRoot -and ($cachedRoot.TrimEnd('\', '/') -ine $root.TrimEnd('\', '/'))) {
            Remove-Item -LiteralPath $nativeBuild -Recurse -Force
        }
    }
    cmake -S $root -B $nativeBuild "-DPS2RECOMP_SOURCE_DIR:PATH=$PS2RecompDir" "-DRATCHET_BOOT_ELF:FILEPATH=$Elf"
    if ($LASTEXITCODE -ne 0) { throw 'Native CMake configuration failed.' }
    cmake --build $nativeBuild --config Release --parallel $buildJobs
    if ($LASTEXITCODE -ne 0) { throw 'Native build failed.' }
}
