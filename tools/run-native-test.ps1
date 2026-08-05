[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [ValidateRange(1, 3600)]
    [int]$DurationSeconds = 10,

    [switch]$Build,
    [switch]$KeepProcess,
    [string]$ExecutablePath,
    [string]$ElfPath
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if ([string]::IsNullOrWhiteSpace($ExecutablePath)) {
    $ExecutablePath = Join-Path $root "build\native\$Configuration\openratchet.exe"
} else {
    if (-not [IO.Path]::IsPathRooted($ExecutablePath)) {
        $ExecutablePath = Join-Path $root $ExecutablePath
    }
    $ExecutablePath = [IO.Path]::GetFullPath($ExecutablePath)
}

if ([string]::IsNullOrWhiteSpace($ElfPath)) {
    $ElfPath = Join-Path $root 'build\extracted\PS2_MAIN.ELF'
} else {
    if (-not [IO.Path]::IsPathRooted($ElfPath)) {
        $ElfPath = Join-Path $root $ElfPath
    }
    $ElfPath = [IO.Path]::GetFullPath($ElfPath)
}

$requiredFiles = @(
    $ExecutablePath,
    $ElfPath
)
$requiredDirectories = @(
    (Join-Path $root 'generated'),
    (Join-Path $root 'third_party\PS2Recomp'),
    (Join-Path $root 'build\extracted\mc0'),
    (Join-Path $root 'build\extracted\mc1'),
    (Join-Path $root 'build\extracted\vags'),
    (Join-Path $root 'build\extracted\vags2'),
    (Join-Path $root 'build\extracted\video'),
    (Join-Path $root 'build\extracted\wads'),
    (Join-Path $root 'build\extracted\wads2')
)

if ($Build) {
    $buildScript = Join-Path $root 'tools\build-native.cmd'
    if (-not (Test-Path -LiteralPath $buildScript -PathType Leaf)) {
        throw "Missing native build helper: $buildScript"
    }

    Write-Host "Building configuration: $Configuration"
    & $buildScript '-Configuration' $Configuration
    if ($LASTEXITCODE -ne 0) {
        throw "Native build failed with exit code $LASTEXITCODE."
    }
}

foreach ($required in $requiredFiles) {
    if (-not (Test-Path -LiteralPath $required -PathType Leaf)) {
        throw "Missing required native test file: $required"
    }
}

foreach ($required in $requiredDirectories) {
    if (-not (Test-Path -LiteralPath $required -PathType Container)) {
        throw "Missing required native runtime directory: $required"
    }
}

$logDirectory = Join-Path $root 'build\native\test-logs'
New-Item -ItemType Directory -Path $logDirectory -Force | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stdoutLog = Join-Path $logDirectory "native-$stamp.stdout.log"
$stderrLog = Join-Path $logDirectory "native-$stamp.stderr.log"

$relativeExecutable = $ExecutablePath.Substring($root.Length).TrimStart('\')
$relativeElf = $ElfPath.Substring($root.Length).TrimStart('\')
$commandText = ".\$relativeExecutable .\$relativeElf"

$process = $null
$launched = $false
$stoppedByHarness = $false
$processWasAlive = $false
$processExitCode = $null
$timer = [Diagnostics.Stopwatch]::StartNew()
$originalPath = $env:Path
$originalUpperPath = $env:PATH

try {
    Write-Host "Working directory: $root"
    Write-Host "Command:           $commandText"
    Write-Host "Duration:          $DurationSeconds seconds"
    Write-Host "Stdout log:        $stdoutLog"
    Write-Host "Stderr log:        $stderrLog"

    # The Codex shell can expose both Path and PATH. Remove the duplicate key
    # only for the child process, matching the verified build helper behavior.
    $env:PATH = $null
    $env:Path = $originalPath

    $process = Start-Process `
        -FilePath $ExecutablePath `
        -ArgumentList @('"' + $ElfPath + '"') `
        -WorkingDirectory $root `
        -RedirectStandardOutput $stdoutLog `
        -RedirectStandardError $stderrLog `
        -PassThru
    $launched = $true

    Start-Sleep -Seconds $DurationSeconds
    $process.Refresh()
    $processWasAlive = -not $process.HasExited
    if (-not $processWasAlive) {
        $processExitCode = $process.ExitCode
    } elseif (-not $KeepProcess) {
        Stop-Process -Id $process.Id -Force
        $stoppedByHarness = $true
        $process.WaitForExit(5000) | Out-Null
    }
} finally {
    $env:Path = $originalPath
    $env:PATH = $originalUpperPath

    if ($process -and -not $KeepProcess) {
        $process.Refresh()
        if (-not $process.HasExited) {
            Stop-Process -Id $process.Id -Force
            $stoppedByHarness = $true
        }
    }
    $timer.Stop()
}

Start-Sleep -Milliseconds 250
$stdoutText = if (Test-Path -LiteralPath $stdoutLog) {
    Get-Content -Raw -LiteralPath $stdoutLog
} else {
    ''
}
$stderrText = if (Test-Path -LiteralPath $stderrLog) {
    Get-Content -Raw -LiteralPath $stderrLog
} else {
    ''
}
$combinedText = "$stdoutText`n$stderrText"

$tickMatches = [regex]::Matches(
    $combinedText,
    '\[run:tick\]\s+tick=(\d+)\s+pc=0x([0-9a-fA-F]+).*?dma=(\d+)\s+gif=(\d+)\s+gsw=(\d+)\s+vif=(\d+)'
)
$sifMatches = [regex]::Matches(
    $combinedText,
    'injected completion for 0x([0-9a-fA-F]+)\s+request=0x([0-9a-fA-F]+)\s+service=0x([0-9a-fA-F]+)\s+payload=0x([0-9a-fA-F]+)\s+result0=0x([0-9a-fA-F]+)\s+result1=0x([0-9a-fA-F]+)'
)
$sifDeferredMatches = [regex]::Matches(
    $combinedText,
    'deferred data-bearing CALL\s+packet=0x([0-9a-fA-F]+)\s+client=0x([0-9a-fA-F]+)\s+request=0x([0-9a-fA-F]+)\s+receive=0x([0-9a-fA-F]+)\s+size=0x([0-9a-fA-F]+)\s+status=0x([0-9a-fA-F]+)\s+sequence=0x([0-9a-fA-F]+)'
)
$diagnostics = @(
    ($combinedText -split '\r?\n') |
        Where-Object { $_ -match '(?i)\b(unimplemented|stub|failed|error)\b' } |
        Select-Object -Unique -First 10
)

$latestTick = $null
$graphicsActivity = $false
foreach ($match in $tickMatches) {
    $latestTick = [pscustomobject]@{
        Tick = [int]$match.Groups[1].Value
        Pc = '0x' + $match.Groups[2].Value.ToLowerInvariant()
        Dma = [int]$match.Groups[3].Value
        Gif = [int]$match.Groups[4].Value
        Gsw = [int]$match.Groups[5].Value
        Vif = [int]$match.Groups[6].Value
    }
    if ($latestTick.Dma -gt 0 -or $latestTick.Gif -gt 0 -or $latestTick.Gsw -gt 0) {
        $graphicsActivity = $true
    }
}

Write-Output ''
Write-Output 'Native test summary'
Write-Output "  Launched:             $launched"
Write-Output "  Alive at duration:    $processWasAlive"
Write-Output "  Stopped by harness:   $stoppedByHarness"
if ($null -ne $processExitCode) {
    Write-Output "  Exit code:            $processExitCode"
}
Write-Output "  Elapsed:              $([math]::Round($timer.Elapsed.TotalSeconds, 2)) seconds"
Write-Output "  Stdout bytes:         $([Text.Encoding]::UTF8.GetByteCount($stdoutText))"
Write-Output "  Stderr bytes:         $([Text.Encoding]::UTF8.GetByteCount($stderrText))"
Write-Output "  SIF completions:      $($sifMatches.Count)"

if ($latestTick) {
    Write-Output "  Latest runtime tick:  tick=$($latestTick.Tick) pc=$($latestTick.Pc) dma=$($latestTick.Dma) gif=$($latestTick.Gif) gsw=$($latestTick.Gsw) vif=$($latestTick.Vif)"
} else {
    Write-Output '  Latest runtime tick:  none captured'
}

if ($graphicsActivity) {
    Write-Output '  Graphics activity:    observed'
} else {
    Write-Output '  Graphics activity:    none observed'
}

if ($sifMatches.Count -gt 0) {
    Write-Output '  Recent SIF completions:'
    $sifMatches | Select-Object -Last 8 | ForEach-Object {
        Write-Output ("    packet=0x{0} request=0x{1} service=0x{2} payload=0x{3} result0=0x{4} result1=0x{5}" -f `
            $_.Groups[1].Value, $_.Groups[2].Value, $_.Groups[3].Value, `
            $_.Groups[4].Value, $_.Groups[5].Value, $_.Groups[6].Value)
    }
}

if ($sifDeferredMatches.Count -gt 0) {
    $deferred = $sifDeferredMatches[$sifDeferredMatches.Count - 1]
    Write-Output ("  Latest deferred SIF: packet=0x{0} client=0x{1} request=0x{2} receive=0x{3} size=0x{4} status=0x{5} sequence=0x{6}" -f `
        $deferred.Groups[1].Value, $deferred.Groups[2].Value, $deferred.Groups[3].Value, `
        $deferred.Groups[4].Value, $deferred.Groups[5].Value, $deferred.Groups[6].Value, `
        $deferred.Groups[7].Value)
}

if ($diagnostics.Count -gt 0) {
    Write-Output '  Diagnostics:'
    $diagnostics | ForEach-Object { Write-Output "    $_" }
}

Write-Output "  Logs: $stdoutLog; $stderrLog"
