[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [ValidateRange(0, 1000)]
    [int]$LevelIndex = 0,

    [ValidateRange(1, 3600)]
    [int]$RuntimeSeconds = 20,

    [ValidateRange(1, 3600)]
    [double]$ViewerSmokeSeconds = 3,

    [ValidateRange(15, 600)]
    [int]$BuildHangSeconds = 30,

    [ValidateRange(0, 3)]
    [int]$BuildRetries = 1,

    [ValidateRange(1, 30)]
    [int]$ProgressHeartbeatSeconds = 2
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
$verificationRoot = Join-Path $repoRoot 'build\native\verification'
$reportPath = Join-Path $verificationRoot ("{0}.md" -f $stamp)
$tempDirectory = Join-Path ([IO.Path]::GetTempPath()) ("OpenRatchetVerify-{0}-{1}" -f $stamp, $PID)
New-Item -ItemType Directory -Path $verificationRoot -Force | Out-Null
New-Item -ItemType Directory -Path $tempDirectory -Force | Out-Null

function Get-RelativePath([string]$path) {
    if ([string]::IsNullOrWhiteSpace($path)) { return '(none)' }
    $full = [IO.Path]::GetFullPath($path)
    if ($full.StartsWith($repoRoot, [StringComparison]::OrdinalIgnoreCase)) {
        return $full.Substring($repoRoot.Length).TrimStart([char[]]@('\', '/')) -replace '\\', '/'
    }
    return $full
}

function Write-Stage([string]$name, [string]$state, [string]$detail = '') {
    if ([string]::IsNullOrWhiteSpace($detail)) {
        Write-Host ("[{0}] {1}" -f $state, $name)
    } else {
        Write-Host ("[{0}] {1} - {2}" -f $state, $name, $detail)
    }
}

function Update-VerificationProgress([string]$stage, [int]$percent, [string]$detail = '') {
    $status = $stage
    if (-not [string]::IsNullOrWhiteSpace($detail)) {
        $status = "{0} - {1}" -f $stage, $detail
    }
    Write-Progress -Id 1 -Activity 'OpenRatchet native verification' -Status $status -PercentComplete $percent
}

function Get-OutputByteCount([string]$stdoutPath, [string]$stderrPath) {
    [long]$total = 0
    if (Test-Path -LiteralPath $stdoutPath) {
        $total += [long](Get-Item -LiteralPath $stdoutPath).Length
    }
    if (Test-Path -LiteralPath $stderrPath) {
        $total += [long](Get-Item -LiteralPath $stderrPath).Length
    }
    return $total
}

function Get-LatestOutputPreview([string]$stdoutPath, [string]$stderrPath) {
    $line = $null
    if (Test-Path -LiteralPath $stderrPath) {
        $line = @(Get-Content -LiteralPath $stderrPath -Tail 1 -ErrorAction SilentlyContinue | Select-Object -Last 1)
        if ($line.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace([string]$line[0])) {
            $line = [string]$line[0]
        } else {
            $line = $null
        }
    }
    if (-not $line -and (Test-Path -LiteralPath $stdoutPath)) {
        $tail = @(Get-Content -LiteralPath $stdoutPath -Tail 1 -ErrorAction SilentlyContinue | Select-Object -Last 1)
        if ($tail.Count -gt 0 -and -not [string]::IsNullOrWhiteSpace([string]$tail[0])) {
            $line = [string]$tail[0]
        }
    }
    if (-not $line) { return 'waiting for output' }
    $line = ($line -replace '\s+', ' ').Trim()
    if ($line.Length -gt 120) { $line = $line.Substring(0, 117) + '...' }
    return $line
}

function Stop-NativeProcessTree([int]$processId) {
    $taskkill = Get-Command taskkill.exe -ErrorAction SilentlyContinue
    if ($taskkill) {
        & $taskkill.Source /PID "$processId" /T /F 2>$null | Out-Null
        return
    }
    Stop-Process -Id $processId -Force -ErrorAction SilentlyContinue
}

function ConvertTo-NativeArgument([string]$argument) {
    if ($null -eq $argument -or $argument.Length -eq 0) {
        return '""'
    }

    if ($argument -notmatch '[\s"]') {
        return $argument
    }

    # Build a Windows command-line argument using the quoting rules consumed by
    # CommandLineToArgvW-style parsers. This keeps repository paths with spaces,
    # embedded quotes, and trailing backslashes intact when Start-Process receives
    # the final argument string.
    $builder = [Text.StringBuilder]::new()
    [void]$builder.Append('"')
    $backslashes = 0

    foreach ($character in $argument.ToCharArray()) {
        if ($character -eq '\') {
            $backslashes++
            continue
        }

        if ($character -eq '"') {
            if ($backslashes -gt 0) {
                [void]$builder.Append(('\' * ($backslashes * 2)))
            }
            [void]$builder.Append('\')
            [void]$builder.Append('"')
            $backslashes = 0
            continue
        }

        if ($backslashes -gt 0) {
            [void]$builder.Append(('\' * $backslashes))
            $backslashes = 0
        }
        [void]$builder.Append($character)
    }

    if ($backslashes -gt 0) {
        [void]$builder.Append(('\' * ($backslashes * 2)))
    }
    [void]$builder.Append('"')
    return $builder.ToString()
}

function Invoke-CapturedCommand {
    param(
        [string]$filePath,
        [string[]]$arguments,
        [string]$logPath,
        [string]$displayName = 'Command',
        [ValidateRange(0, 100)]
        [int]$progressPercent = 0,
        [ValidateRange(0, 3600)]
        [int]$inactivityTimeoutSeconds = 0
    )

    $timer = [Diagnostics.Stopwatch]::StartNew()
    $output = @()
    $exitCode = 1
    $timedOut = $false
    $stdoutPath = "$logPath.stdout.tmp"
    $stderrPath = "$logPath.stderr.tmp"

    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    try {
        # Keep native stdout/stderr as raw OS streams. The process is intentionally
        # NOT started with -Wait: polling keeps verify-native visibly alive and lets
        # us detect a genuinely stuck build instead of looking frozen forever.
        $argumentLine = (@($arguments) | ForEach-Object { ConvertTo-NativeArgument ([string]$_) }) -join ' '
        $process = Start-Process `
            -FilePath $filePath `
            -ArgumentList $argumentLine `
            -WorkingDirectory $repoRoot `
            -NoNewWindow `
            -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath

        $lastOutputBytes = Get-OutputByteCount $stdoutPath $stderrPath
        $lastActivityUtc = [DateTime]::UtcNow
        $lastHeartbeatUtc = [DateTime]::MinValue

        while (-not $process.HasExited) {
            Start-Sleep -Milliseconds 250
            $process.Refresh()

            $nowUtc = [DateTime]::UtcNow
            $outputBytes = Get-OutputByteCount $stdoutPath $stderrPath
            if ($outputBytes -ne $lastOutputBytes) {
                $lastOutputBytes = $outputBytes
                $lastActivityUtc = $nowUtc
            }

            $idleSeconds = [math]::Floor(($nowUtc - $lastActivityUtc).TotalSeconds)
            if (($nowUtc - $lastHeartbeatUtc).TotalSeconds -ge $ProgressHeartbeatSeconds) {
                $preview = Get-LatestOutputPreview $stdoutPath $stderrPath
                $detail = "elapsed={0}s, idle={1}s, {2}" -f [math]::Floor($timer.Elapsed.TotalSeconds), $idleSeconds, $preview
                Update-VerificationProgress $displayName $progressPercent $detail
                $lastHeartbeatUtc = $nowUtc
            }

            if ($inactivityTimeoutSeconds -gt 0 -and $idleSeconds -ge $inactivityTimeoutSeconds) {
                $timedOut = $true
                Write-Host ("[HANG] {0} - no output activity for {1}s; terminating process tree" -f $displayName, $inactivityTimeoutSeconds)
                Stop-NativeProcessTree $process.Id
                break
            }
        }

        try { [void]$process.WaitForExit(5000) } catch {}
        if ($timedOut) {
            $exitCode = 124
        } else {
            $exitCode = [int]$process.ExitCode
        }

        $stdoutLines = @(
            if (Test-Path -LiteralPath $stdoutPath) {
                Get-Content -LiteralPath $stdoutPath
            }
        )
        $stderrLines = @(
            if (Test-Path -LiteralPath $stderrPath) {
                Get-Content -LiteralPath $stderrPath
            }
        )

        $output = @($stdoutLines)
        if ($stderrLines.Count -gt 0) {
            if ($output.Count -gt 0) { $output += '' }
            $output += '--- stderr ---'
            $output += @($stderrLines)
        }
        if ($timedOut) {
            if ($output.Count -gt 0) { $output += '' }
            $output += ("[verify-native] process terminated after {0}s without output activity" -f $inactivityTimeoutSeconds)
        }
    } catch {
        $output = @("Process launch exception: $($_.Exception.Message)")
        $exitCode = 1
    } finally {
        $timer.Stop()
        Set-Content -LiteralPath $logPath -Value $output -Encoding UTF8
        Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue
    }

    return [pscustomobject]@{
        ExitCode = [int]$exitCode
        ElapsedSeconds = [math]::Round($timer.Elapsed.TotalSeconds, 2)
        Lines = @($output)
        LogPath = $logPath
        TimedOut = [bool]$timedOut
    }
}

function Invoke-BuildWithRetry {
    param(
        [string]$buildScript,
        [string]$configuration,
        [string]$combinedLogPath
    )

    $allLines = [System.Collections.Generic.List[string]]::new()
    $attemptResults = [System.Collections.Generic.List[object]]::new()
    $maxAttempts = 1 + $BuildRetries
    $totalTimer = [Diagnostics.Stopwatch]::StartNew()

    for ($attempt = 1; $attempt -le $maxAttempts; $attempt++) {
        $attemptLog = Join-Path $tempDirectory ("build-attempt-{0}.log" -f $attempt)
        Write-Stage "Native build ($configuration)" 'RUN' ("attempt {0}/{1}" -f $attempt, $maxAttempts)
        $result = Invoke-CapturedCommand `
            'powershell.exe' `
            @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', $buildScript, '-Configuration', $configuration) `
            $attemptLog `
            ("Native build ($configuration), attempt $attempt/$maxAttempts") `
            10 `
            $BuildHangSeconds
        $attemptResults.Add($result)

        $allLines.Add(("===== BUILD ATTEMPT {0}/{1} | exit={2} | elapsed={3}s | timedOut={4} =====" -f $attempt, $maxAttempts, $result.ExitCode, $result.ElapsedSeconds, $result.TimedOut))
        foreach ($line in $result.Lines) { $allLines.Add([string]$line) }
        $allLines.Add('')

        if (-not $result.TimedOut) { break }
        if ($attempt -lt $maxAttempts) {
            Write-Stage "Native build ($configuration)" 'RETRY' ("attempt {0} hung; restarting automatically" -f $attempt)
            Start-Sleep -Seconds 1
        }
    }

    $totalTimer.Stop()
    Set-Content -LiteralPath $combinedLogPath -Value @($allLines) -Encoding UTF8
    $final = $attemptResults[$attemptResults.Count - 1]

    return [pscustomobject]@{
        ExitCode = [int]$final.ExitCode
        ElapsedSeconds = [math]::Round($totalTimer.Elapsed.TotalSeconds, 2)
        Lines = @($allLines)
        LogPath = $combinedLogPath
        TimedOut = [bool]$final.TimedOut
        Attempts = [int]$attemptResults.Count
        TimedOutAttempts = [int](@($attemptResults | Where-Object { $_.TimedOut }).Count)
    }
}

function Get-FailureExcerpt([string[]]$lines, [int]$maxLines = 16) {
    if ($null -eq $lines -or $lines.Count -eq 0) { return @('(no captured output)') }

    $interesting = @(
        $lines | Where-Object {
            $_ -match '(?i)(Process launch exception:|PowerShell exception:|CMake Error|fatal error|FAILED:|Build FAILED|error [A-Z]+[0-9]+:|: error [A-Z]*[0-9]*:|MSB[0-9]+:|ninja: build stopped)'
        }
    )

    if ($interesting.Count -gt 0) {
        return @($interesting | Select-Object -Last $maxLines)
    }

    return @(
        $lines |
            Where-Object { -not [string]::IsNullOrWhiteSpace([string]$_) } |
            Select-Object -Last $maxLines
    )
}

function Expand-TraceLines([string[]]$lines) {
    $expanded = [System.Collections.Generic.List[string]]::new()
    foreach ($line in $lines) {
        if ($null -eq $line) { continue }
        # Some PS2Recomp runtime diagnostics do not terminate their log message.
        # Split before known trace prefixes so one malformed line cannot hide later
        # OpenRatchet evidence from the compact report parser.
        $parts = [regex]::Split(
            [string]$line,
            '(?=\[(?:OpenRatchet:|frame:|gs:|run:|SetupHeap|Syscall|FindAddress))'
        )
        foreach ($part in $parts) {
            if (-not [string]::IsNullOrWhiteSpace($part)) {
                $expanded.Add($part.Trim())
            }
        }
    }
    return @($expanded)
}

function Get-LastLine([string[]]$lines, [string]$pattern) {
    $matches = @($lines | Where-Object { $_ -match $pattern } | Select-Object -Last 1)
    if ($matches.Count -eq 0) { return $null }
    return $matches[0]
}

function Get-Field([string]$line, [string]$name) {
    if ([string]::IsNullOrWhiteSpace($line)) { return $null }
    $match = [regex]::Match($line, '(?:^|\s)' + [regex]::Escape($name) + '=([^\s]+)')
    if (-not $match.Success) { return $null }
    return $match.Groups[1].Value
}

function Get-SummaryValue([string[]]$lines, [string]$label) {
    $pattern = '^\s*' + [regex]::Escape($label) + ':\s*(.+?)\s*$'
    $line = @($lines | Where-Object { $_ -match $pattern } | Select-Object -Last 1)
    if ($line.Count -eq 0) { return $null }
    return ([regex]::Match($line[0], $pattern)).Groups[1].Value.Trim()
}

function Add-ReportLine([System.Collections.Generic.List[string]]$report, [string]$line = '') {
    $report.Add($line)
}

function Add-EmbeddedLogSection([System.Collections.Generic.List[string]]$report, [string]$title, [string[]]$lines) {
    Add-ReportLine $report ("### {0}" -f $title)
    Add-ReportLine $report ''
    Add-ReportLine $report '<details>'
    Add-ReportLine $report ("<summary>Full {0}</summary>" -f $title)
    Add-ReportLine $report ''
    Add-ReportLine $report '````text'
    if ($null -eq $lines -or $lines.Count -eq 0) {
        Add-ReportLine $report '(no captured output)'
    } else {
        foreach ($line in $lines) { Add-ReportLine $report ([string]$line) }
    }
    Add-ReportLine $report '````'
    Add-ReportLine $report ''
    Add-ReportLine $report '</details>'
    Add-ReportLine $report ''
}

$buildLog = Join-Path $tempDirectory 'build.log'
$ctestLog = Join-Path $tempDirectory 'ctest.log'
$viewerLog = Join-Path $tempDirectory 'viewer.log'
$runtimeHarnessLog = Join-Path $tempDirectory 'runtime-harness.log'

$overallPass = $true

Update-VerificationProgress 'Starting' 1 'preparing verification'
$buildScript = Join-Path $repoRoot 'tools\build-native.ps1'
$build = Invoke-BuildWithRetry $buildScript $Configuration $buildLog
$buildPass = $build.ExitCode -eq 0
if (-not $buildPass) { $overallPass = $false }
Write-Stage "Native build ($Configuration)" $(if ($buildPass) { 'PASS' } else { 'FAIL' }) ("{0}s, attempts={1}, timedOutAttempts={2}" -f $build.ElapsedSeconds, $build.Attempts, $build.TimedOutAttempts)
Update-VerificationProgress 'Native build complete' 25 ("{0}s" -f $build.ElapsedSeconds)

$ctest = $null
$viewer = $null
$runtime = $null
$ctestPass = $false
$viewerPass = $false
$runtimePass = $false

if ($buildPass) {
    $ctestCommand = Get-Command ctest.exe -ErrorAction SilentlyContinue
    if (-not $ctestCommand) { $ctestCommand = Get-Command ctest -ErrorAction SilentlyContinue }

    if ($ctestCommand) {
        Write-Stage 'CTest' 'RUN'
        $ctest = Invoke-CapturedCommand $ctestCommand.Source @(
            '--test-dir', (Join-Path $repoRoot 'build\native'),
            '-C', $Configuration,
            '--output-on-failure'
        ) $ctestLog 'CTest' 40 0
        $ctestPass = $ctest.ExitCode -eq 0
        if (-not $ctestPass) { $overallPass = $false }
        Write-Stage 'CTest' $(if ($ctestPass) { 'PASS' } else { 'FAIL' }) ("{0}s" -f $ctest.ElapsedSeconds)
        Update-VerificationProgress 'CTest complete' 45 ("{0}s" -f $ctest.ElapsedSeconds)
    } else {
        Set-Content -LiteralPath $ctestLog -Value 'ctest was not found on PATH.' -Encoding UTF8
        $ctestPass = $false
        $overallPass = $false
        Write-Stage 'CTest' 'FAIL' 'ctest was not found on PATH'
    }

    Write-Stage 'Level viewer smoke' 'RUN'
    $viewer = Invoke-CapturedCommand 'powershell.exe' @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $repoRoot 'tools\run-native-level-viewer.ps1'),
        '-Configuration', $Configuration,
        '-LevelIndex', "$LevelIndex",
        '-SmokeSeconds', $ViewerSmokeSeconds.ToString([Globalization.CultureInfo]::InvariantCulture)
    ) $viewerLog 'Level viewer smoke' 60 0
    $viewerSmokeLine = Get-LastLine $viewer.Lines '^\[OpenRatchet:viewer:smoke\] '
    $viewerParityLine = Get-LastLine $viewer.Lines '^\[OpenRatchet:render:parity\] frontend=viewer '
    $viewerSceneLine = Get-LastLine $viewer.Lines '^\[OpenRatchet:scene\] '
    $viewerMobyLine = Get-LastLine $viewer.Lines '^\[OpenRatchet:moby\] '
    $viewerPoseLine = Get-LastLine $viewer.Lines '^\[OpenRatchet:moby:pose\] '
    $viewerSkinLine = Get-LastLine $viewer.Lines '^\[OpenRatchet:moby:skinexec\] '
    $viewerRatchetAnimLine = Get-LastLine $viewer.Lines '^\[OpenRatchet:ratchet:anim\] '
    $viewerRatchetSkinLine = Get-LastLine $viewer.Lines '^\[OpenRatchet:ratchet:skinexec\] '
    $viewerPass = (
        $viewer.ExitCode -eq 0 -and
        (Get-Field $viewerSmokeLine 'status') -eq 'ok' -and
        (Get-Field $viewerParityLine 'status') -eq 'ok' -and
        (Get-Field $viewerSceneLine 'status') -eq 'ok' -and
        (Get-Field $viewerMobyLine 'status') -eq 'ok' -and
        (Get-Field $viewerPoseLine 'status') -eq 'ok' -and
        (Get-Field $viewerSkinLine 'status') -eq 'ok' -and
        (Get-Field $viewerRatchetAnimLine 'status') -eq 'ok' -and
        (Get-Field $viewerRatchetSkinLine 'status') -eq 'ok'
    )
    if (-not $viewerPass) { $overallPass = $false }
    Write-Stage 'Level viewer smoke' $(if ($viewerPass) { 'PASS' } else { 'FAIL' }) ("{0}s" -f $viewer.ElapsedSeconds)
    Update-VerificationProgress 'Level viewer complete' 65 ("{0}s" -f $viewer.ElapsedSeconds)

    Write-Stage 'Native runtime harness' 'RUN'
    $runtime = Invoke-CapturedCommand 'powershell.exe' @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $repoRoot 'tools\run-native-test.ps1'),
        '-Configuration', $Configuration,
        '-DurationSeconds', "$RuntimeSeconds"
    ) $runtimeHarnessLog 'Native runtime harness' 80 0

    $runtimeLaunched = Get-SummaryValue $runtime.Lines 'Launched'
    $runtimeAlive = Get-SummaryValue $runtime.Lines 'Alive at duration'
    $runtimeGraphics = Get-SummaryValue $runtime.Lines 'Graphics activity'
    $runtimePass = (
        $runtime.ExitCode -eq 0 -and
        $runtimeLaunched -eq 'True' -and
        $runtimeAlive -eq 'True' -and
        $runtimeGraphics -eq 'observed'
    )
    if (-not $runtimePass) { $overallPass = $false }
    Write-Stage 'Native runtime harness' $(if ($runtimePass) { 'PASS' } else { 'FAIL' }) ("{0}s" -f $runtime.ElapsedSeconds)
    Update-VerificationProgress 'Native runtime complete' 90 ("{0}s" -f $runtime.ElapsedSeconds)
} else {
    Write-Stage 'CTest' 'SKIP' 'build failed'
    Write-Stage 'Level viewer smoke' 'SKIP' 'build failed'
    Write-Stage 'Native runtime harness' 'SKIP' 'build failed'
    $overallPass = $false
}

$runtimeStdoutSource = $null
$runtimeStderrSource = $null
$runtimeStdoutLines = @()
$runtimeStderrLines = @()
$runtimeLines = @()

if ($runtime) {
    $logsLine = @($runtime.Lines | Where-Object { $_ -match '^\s*Logs:\s+(.+?);\s*(.+?)\s*$' } | Select-Object -Last 1)
    if ($logsLine.Count -gt 0) {
        $m = [regex]::Match($logsLine[0], '^\s*Logs:\s+(.+?);\s*(.+?)\s*$')
        $runtimeStdoutSource = $m.Groups[1].Value.Trim()
        $runtimeStderrSource = $m.Groups[2].Value.Trim()
    }

    if ($runtimeStdoutSource -and (Test-Path -LiteralPath $runtimeStdoutSource)) {
        $runtimeStdoutLines = @(Get-Content -LiteralPath $runtimeStdoutSource)
        $runtimeLines += @($runtimeStdoutLines)
    }
    if ($runtimeStderrSource -and (Test-Path -LiteralPath $runtimeStderrSource)) {
        $runtimeStderrLines = @(Get-Content -LiteralPath $runtimeStderrSource)
        $runtimeLines += @($runtimeStderrLines)
    }
    $runtimeLines = @(Expand-TraceLines $runtimeLines)
}

$runtimeOwnershipLine = Get-LastLine $runtimeLines '^\[OpenRatchet:render:ownership\] '
$runtimeFrameLine = Get-LastLine $runtimeLines '^\[OpenRatchet:render:frame\].*\bnativeLevel='
$runtimeParityLine = Get-LastLine $runtimeLines '^\[OpenRatchet:render:parity\] frontend=runtime '
$runtimeCameraLine = Get-LastLine $runtimeLines '^\[OpenRatchet:live:camera\] '
$runtimeSkyLine = Get-LastLine $runtimeLines '^\[OpenRatchet:live:sky\] '
$runtimeRatchetAnimationLine = Get-LastLine $runtimeLines '^\[OpenRatchet:live:ratchet-animation\] '
$runtimeRatchetTransformLine = Get-LastLine $runtimeLines '^\[OpenRatchet:live:ratchet-transform\] '
$runtimeReplacementBootstrapLine = Get-LastLine $runtimeLines '^\[OpenRatchet:native\] replacements stage=bootstrap '
$runtimeReplacementRuntimeLine = Get-LastLine $runtimeLines '^\[OpenRatchet:native\] replacements stage=runtime '
$runtimeOverlayLine = Get-LastLine $runtimeLines '^\[OpenRatchet:gameplay-overlay\] '
$runtimeWad158OverlayAotLine = Get-LastLine $runtimeLines '^\[OpenRatchet:overlay-aot\] source=wad_158\.wad '
$runtimeLevel0OverlayAotLine = Get-LastLine $runtimeLines '^\[OpenRatchet:overlay-aot\] source=level_00\.wad '
$runtimeWad158OverlayAotIndex = -1
$runtimeLevel0OverlayAotIndex = -1
for ($i = 0; $i -lt $runtimeLines.Count; ++$i) {
    if ($runtimeWad158OverlayAotIndex -lt 0 -and [string]$runtimeLines[$i] -match '^\[OpenRatchet:overlay-aot\] source=wad_158\.wad ') {
        $runtimeWad158OverlayAotIndex = $i
    }
    if ($runtimeLevel0OverlayAotIndex -lt 0 -and [string]$runtimeLines[$i] -match '^\[OpenRatchet:overlay-aot\] source=level_00\.wad ') {
        $runtimeLevel0OverlayAotIndex = $i
    }
}
$runtimeOverlayGenerationOrderPass = (
    $runtimeLevel0OverlayAotIndex -ge 0 -and
    ($runtimeWad158OverlayAotIndex -lt 0 -or
     $runtimeWad158OverlayAotIndex -lt $runtimeLevel0OverlayAotIndex)
)
$runtimeRatchetControlStateLine = Get-LastLine $runtimeLines '^\[OpenRatchet:live:ratchet-control-state\] '

$viewerParityLine = if ($viewer) { Get-LastLine $viewer.Lines '^\[OpenRatchet:render:parity\] frontend=viewer ' } else { $null }
$viewerSceneLine = if ($viewer) { Get-LastLine $viewer.Lines '^\[OpenRatchet:scene\] ' } else { $null }
$viewerMobyLine = if ($viewer) { Get-LastLine $viewer.Lines '^\[OpenRatchet:moby\] ' } else { $null }
$viewerPoseLine = if ($viewer) { Get-LastLine $viewer.Lines '^\[OpenRatchet:moby:pose\] ' } else { $null }
$viewerSkinLine = if ($viewer) { Get-LastLine $viewer.Lines '^\[OpenRatchet:moby:skinexec\] ' } else { $null }
$viewerRatchetAnimLine = if ($viewer) { Get-LastLine $viewer.Lines '^\[OpenRatchet:ratchet:anim\] ' } else { $null }
$viewerRatchetSkinLine = if ($viewer) { Get-LastLine $viewer.Lines '^\[OpenRatchet:ratchet:skinexec\] ' } else { $null }
$viewerSmokeLine = if ($viewer) { Get-LastLine $viewer.Lines '^\[OpenRatchet:viewer:smoke\] ' } else { $null }

$viewerHash = Get-Field $viewerParityLine 'combinedHash'
$runtimeHash = Get-Field $runtimeParityLine 'combinedHash'
$parityComparable = -not [string]::IsNullOrWhiteSpace($viewerHash) -and -not [string]::IsNullOrWhiteSpace($runtimeHash)
$parityMatch = $parityComparable -and ($viewerHash -eq $runtimeHash)
if ($buildPass -and -not $parityMatch) { $overallPass = $false }

$runtimeOverlayStatus = Get-Field $runtimeOverlayLine 'status'
$runtimeOverlaySegments = Get-Field $runtimeOverlayLine 'segments'
$runtimeOverlayPayloadBytes = Get-Field $runtimeOverlayLine 'payloadBytes'
$runtimeOverlayMaterializedSegments = Get-Field $runtimeOverlayLine 'materializedSegments'
$runtimeOverlayMaterializedBytes = Get-Field $runtimeOverlayLine 'materializedBytes'
$runtimeOverlayFallbackEntries = Get-Field $runtimeOverlayLine 'fallbackEntries'
$runtimeOverlayComparableEntries = Get-Field $runtimeOverlayLine 'comparableEntries'
$runtimeOverlayConflictingEntries = Get-Field $runtimeOverlayLine 'conflictingEntries'
$runtimeOverlayPass = (
    -not [string]::IsNullOrWhiteSpace($runtimeOverlayLine) -and
    $runtimeOverlaySegments -eq '7' -and
    $runtimeOverlayPayloadBytes -eq '1645532' -and
    $runtimeOverlayFallbackEntries -eq '17300' -and
    $runtimeOverlayComparableEntries -eq '17300' -and
    $runtimeOverlayConflictingEntries -eq '17300' -and
    $runtimeOverlayStatus -eq 'stale-fallback-conflict-proved' -and
    $runtimeOverlayMaterializedSegments -eq '7' -and
    $runtimeOverlayMaterializedBytes -eq '1645532'
)

function Test-OverlayAotEvidence {
    param(
        [string]$Line,
        [string]$ExpectedGenerationEntry
    )
    if ([string]::IsNullOrWhiteSpace($Line)) { return $false }
    $status = Get-Field $Line 'status'
    $functions = Get-Field $Line 'functions'
    $installed = Get-Field $Line 'installed'
    return (
        (Get-Field $Line 'generationEntry') -eq $ExpectedGenerationEntry -and
        -not [string]::IsNullOrWhiteSpace($functions) -and
        $functions -ne '0' -and
        $functions -eq $installed -and
        (Get-Field $Line 'tableBase') -eq '0x112380' -and
        (Get-Field $Line 'tableEnd') -eq '0x2f0cd0' -and
        $status -in @('activated', 'already-active')
    )
}

$runtimeWad158OverlayAotPass = Test-OverlayAotEvidence $runtimeWad158OverlayAotLine '0x1e9658'
$runtimeLevel0OverlayAotPass = Test-OverlayAotEvidence $runtimeLevel0OverlayAotLine '0x245c28'
# Accepted Retail New Game -> Veldin evidence reaches the stable generation-call
# boundary as Boot -> Level 0 directly. WAD158 remains a supported optional AOT
# generation if Retail returns it on another path, but it is no longer a mandatory
# outer-dispatch gate for Phase 12.
$runtimeOverlayAotPass = $runtimeLevel0OverlayAotPass -and $runtimeOverlayGenerationOrderPass

$runtimeWad158OverlayAotStatus = Get-Field $runtimeWad158OverlayAotLine 'status'
$runtimeWad158OverlayAotFunctions = Get-Field $runtimeWad158OverlayAotLine 'functions'
$runtimeWad158OverlayAotInstalled = Get-Field $runtimeWad158OverlayAotLine 'installed'
$runtimeLevel0OverlayAotStatus = Get-Field $runtimeLevel0OverlayAotLine 'status'
$runtimeLevel0OverlayAotGenerationEntry = Get-Field $runtimeLevel0OverlayAotLine 'generationEntry'
$runtimeLevel0OverlayAotFunctions = Get-Field $runtimeLevel0OverlayAotLine 'functions'
$runtimeLevel0OverlayAotInstalled = Get-Field $runtimeLevel0OverlayAotLine 'installed'
$runtimeLevel0OverlayAotTableBase = Get-Field $runtimeLevel0OverlayAotLine 'tableBase'
$runtimeLevel0OverlayAotTableEnd = Get-Field $runtimeLevel0OverlayAotLine 'tableEnd'

$runtimeRatchetControlStateStatus = Get-Field $runtimeRatchetControlStateLine 'status'
$runtimeRatchetControlStateCandidates = Get-Field $runtimeRatchetControlStateLine 'ratchetCandidates'
$runtimeRatchetControlRatchet = Get-Field $runtimeRatchetControlStateLine 'ratchetMoby'
$runtimeRatchetControlPVar = Get-Field $runtimeRatchetControlStateLine 'ratchetPVar'
$runtimeRatchetControlState = Get-Field $runtimeRatchetControlStateLine 'state'
$runtimeRatchetControlStateRatchet = Get-Field $runtimeRatchetControlStateLine 'stateRatchetMoby'
$runtimeRatchetControlCompanion = Get-Field $runtimeRatchetControlStateLine 'companionMoby'
$runtimeRatchetControlCompanionOClass = Get-Field $runtimeRatchetControlStateLine 'companionOClass'
$runtimeRatchetControlCompanionLive = Get-Field $runtimeRatchetControlStateLine 'companionLive'
$runtimeRatchetUpdateCallback = Get-Field $runtimeRatchetControlStateLine 'updateCallback'
$runtimeRatchetControlBacklinkMatch = (
    -not [string]::IsNullOrWhiteSpace($runtimeRatchetControlStateRatchet) -and
    -not [string]::IsNullOrWhiteSpace($runtimeRatchetControlRatchet) -and
    $runtimeRatchetControlStateRatchet -eq $runtimeRatchetControlRatchet
)
$runtimeRatchetControlStatePass = (
    -not [string]::IsNullOrWhiteSpace($runtimeRatchetControlStateLine) -and
    $runtimeRatchetControlStateStatus -eq 'ok' -and
    $runtimeRatchetControlStateCandidates -eq '1' -and
    -not [string]::IsNullOrWhiteSpace($runtimeRatchetControlRatchet) -and
    $runtimeRatchetControlRatchet -ne '0x0' -and
    -not [string]::IsNullOrWhiteSpace($runtimeRatchetControlPVar) -and
    $runtimeRatchetControlPVar -ne '0x0' -and
    -not [string]::IsNullOrWhiteSpace($runtimeRatchetControlState) -and
    $runtimeRatchetControlState -ne '0x0' -and
    $runtimeRatchetControlBacklinkMatch
)

if ($buildPass) {
    $runtimePass = (
        $runtimePass -and
        $runtimeOverlayPass -and
        $runtimeOverlayAotPass -and
        (Get-Field $runtimeOwnershipLine 'status') -eq 'ok' -and
        (Get-Field $runtimeOwnershipLine 'owner') -eq 'native-level0' -and
        (Get-Field $runtimeFrameLine 'rendered') -eq '1' -and
        (Get-Field $runtimeFrameLine 'renderer') -eq 'ok' -and
        (Get-Field $runtimeFrameLine 'camera') -eq 'ok' -and
        (Get-Field $runtimeFrameLine 'mobyPool') -eq 'ok' -and
        (Get-Field $runtimeFrameLine 'liveMobyUnaccounted') -eq '0' -and
        (Get-Field $runtimeFrameLine 'poolUnaccounted') -eq '0' -and
        (Get-Field $runtimeFrameLine 'ratchetIdentity') -eq 'ok' -and
        (Get-Field $runtimeFrameLine 'ratchetFrame') -eq 'ok' -and
        (Get-Field $runtimeFrameLine 'ratchetGpu') -eq 'ok' -and
        (Get-Field $runtimeFrameLine 'animation') -eq 'ok' -and
        (Get-Field $runtimeFrameLine 'transform') -eq 'ok' -and
        (Get-Field $runtimeFrameLine 'status') -eq 'ok' -and
        (Get-Field $runtimeParityLine 'status') -eq 'ok' -and
        (Get-Field $runtimeReplacementBootstrapLine 'install_errors') -eq '0' -and
        (Get-Field $runtimeReplacementRuntimeLine 'install_errors') -eq '0'
    )

    if (-not $viewerPass -or -not $runtimePass) {
        $overallPass = $false
    }
    Write-Stage 'Native runtime evidence' $(if ($runtimePass) { 'PASS' } else { 'FAIL' })
    Write-Stage 'Presentation ownership' $(if ((Get-Field $runtimeOwnershipLine 'owner') -eq 'native-level0') { 'PASS' } else { 'FAIL' }) $(if ($runtimeOwnershipLine) { $runtimeOwnershipLine } else { 'missing evidence' })
    Write-Stage 'Retail overlay materialization' $(if ($runtimeOverlayPass) { 'PASS' } else { 'FAIL' }) $(if ($runtimeOverlayStatus) { $runtimeOverlayStatus } else { 'missing evidence' })
    Write-Stage 'wad_158 overlay AOT dispatch' 'INFO' $(if ($runtimeWad158OverlayAotStatus) { $runtimeWad158OverlayAotStatus } else { 'not observed (allowed)' })
    Write-Stage 'Level-0 overlay AOT dispatch' $(if ($runtimeLevel0OverlayAotPass) { 'PASS' } else { 'FAIL' }) $(if ($runtimeLevel0OverlayAotStatus) { $runtimeLevel0OverlayAotStatus } else { 'missing evidence' })
    Write-Stage 'Overlay generation order' $(if ($runtimeOverlayGenerationOrderPass) { 'PASS' } else { 'FAIL' }) 'ELF -> [optional wad_158] -> level_00'
    Write-Stage 'Ratchet lifecycle diagnostic' 'INFO' $(if ($runtimeRatchetControlStateStatus) { $runtimeRatchetControlStateStatus } else { 'missing evidence' })
    Write-Stage 'Viewer/runtime parity' $(if ($parityMatch) { 'PASS' } else { 'FAIL' }) $(if ($parityMatch) { $viewerHash } else { 'hash mismatch or missing evidence' })
}

$git = Get-Command git.exe -ErrorAction SilentlyContinue
if (-not $git) { $git = Get-Command git -ErrorAction SilentlyContinue }

$repoStatus = @()
$head = @()
$submoduleStatus = @()
$gitAvailable = $null -ne $git
if ($gitAvailable) {
    $repoStatus = @(& $git.Source -C $repoRoot status --short 2>&1 | ForEach-Object { "$($_)" })
    $head = @(& $git.Source -C $repoRoot rev-parse --short HEAD 2>$null | ForEach-Object { "$($_)" } | Select-Object -First 1)
    $submodulePath = Join-Path $repoRoot 'third_party\PS2Recomp'
    if (Test-Path -LiteralPath $submodulePath -PathType Container) {
        $submoduleStatus = @(
            & $git.Source -c "safe.directory=$submodulePath" -C $submodulePath status --short 2>$null |
                ForEach-Object { "$($_)" }
        )
    }
    if ($submoduleStatus.Count -gt 0) {
        $overallPass = $false
    }
} else {
    $overallPass = $false
}

$gitLogLines = @()
$gitLogLines += 'Repository:'
if (-not $gitAvailable) {
    $gitLogLines += '  unavailable (git was not found on PATH)'
} elseif ($repoStatus.Count -eq 0) {
    $gitLogLines += '  clean'
} else {
    $gitLogLines += @($repoStatus | ForEach-Object { "  $_" })
}
$gitLogLines += ''
$gitLogLines += 'third_party/PS2Recomp:'
if (-not $gitAvailable) {
    $gitLogLines += '  unavailable'
} elseif ($submoduleStatus.Count -eq 0) {
    $gitLogLines += '  clean'
} else {
    $gitLogLines += @($submoduleStatus | ForEach-Object { "  $_" })
}
$ctestPassed = $null
$ctestFailed = $null
$ctestTotal = $null
$ctestTotalTime = $null
if ($ctest) {
    $ctestSummaryLine = Get-LastLine $ctest.Lines '(\d+)% tests passed,\s*(\d+) tests failed out of\s*(\d+)'
    if ($ctestSummaryLine) {
        $m = [regex]::Match($ctestSummaryLine, '(\d+)% tests passed,\s*(\d+) tests failed out of\s*(\d+)')
        $ctestFailed = [int]$m.Groups[2].Value
        $ctestTotal = [int]$m.Groups[3].Value
        $ctestPassed = $ctestTotal - $ctestFailed
    }
    $ctestTimeLine = Get-LastLine $ctest.Lines '^Total Test time \(real\) =\s*(.+)$'
    if ($ctestTimeLine) {
        $ctestTotalTime = ([regex]::Match($ctestTimeLine, '^Total Test time \(real\) =\s*(.+)$')).Groups[1].Value.Trim()
    }
}

Update-VerificationProgress 'Writing self-contained report' 95 'embedding all evidence into one Markdown file'

$report = [System.Collections.Generic.List[string]]::new()
Add-ReportLine $report '# OpenRatchet Native Verification'
Add-ReportLine $report ''
Add-ReportLine $report ("Timestamp: {0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss K'))
Add-ReportLine $report ("Commit: {0}" -f $(if ($head.Count -gt 0) { $head[0] } else { 'unknown' }))
Add-ReportLine $report ("Configuration: {0}" -f $Configuration)
Add-ReportLine $report ("Level: {0}" -f $LevelIndex)
Add-ReportLine $report ("Overall: **{0}**" -f $(if ($overallPass) { 'PASS' } else { 'FAIL' }))
Add-ReportLine $report ''
Add-ReportLine $report '> This report is self-contained. Build, test, viewer, runtime, runtime stdout/stderr, and repository evidence are embedded below; upload only this `.md` file for review.'
Add-ReportLine $report ''

Add-ReportLine $report '## Build'
Add-ReportLine $report ''
Add-ReportLine $report ("- Status: **{0}**" -f $(if ($buildPass) { 'PASS' } else { 'FAIL' }))
Add-ReportLine $report ("- Exit code: {0}" -f $build.ExitCode)
Add-ReportLine $report ("- Elapsed: {0} s" -f $build.ElapsedSeconds)
Add-ReportLine $report ("- Attempts: {0}" -f $build.Attempts)
Add-ReportLine $report ("- Timed-out attempts automatically restarted: {0}" -f $build.TimedOutAttempts)
Add-ReportLine $report ("- Build inactivity timeout: {0} s" -f $BuildHangSeconds)
if (-not $buildPass) {
    Add-ReportLine $report ''
    Add-ReportLine $report '### Failure excerpt'
    Add-ReportLine $report ''
    Add-ReportLine $report '````text'
    foreach ($line in (Get-FailureExcerpt $build.Lines)) { Add-ReportLine $report ([string]$line) }
    Add-ReportLine $report '````'
}
Add-ReportLine $report ''

Add-ReportLine $report '## Tests'
Add-ReportLine $report ''
if ($ctest) {
    Add-ReportLine $report ("- Status: **{0}**" -f $(if ($ctest.ExitCode -eq 0) { 'PASS' } else { 'FAIL' }))
    if ($null -ne $ctestTotal) {
        Add-ReportLine $report ("- Passed: {0}/{1}" -f $ctestPassed, $ctestTotal)
        Add-ReportLine $report ("- Failed: {0}" -f $ctestFailed)
    }
    if ($ctestTotalTime) { Add-ReportLine $report ("- CTest time: {0}" -f $ctestTotalTime) }
} elseif ($buildPass) {
    Add-ReportLine $report '- Status: **FAIL** (ctest was not found on PATH)'
} else {
    Add-ReportLine $report '- Status: **SKIPPED** (build failed)'
}
Add-ReportLine $report ''

Add-ReportLine $report '## Level Viewer Smoke'
Add-ReportLine $report ''
if ($viewer) {
    Add-ReportLine $report ("- Status: **{0}**" -f $(if ($viewerPass) { 'PASS' } else { 'FAIL' }))
    Add-ReportLine $report ("- Requested smoke duration: {0} s" -f $ViewerSmokeSeconds)
    Add-ReportLine $report ("- Actual smoke duration: {0} s" -f $(Get-Field $viewerSmokeLine 'elapsed'))
    Add-ReportLine $report ("- Frames rendered: {0}" -f $(Get-Field $viewerSmokeLine 'frames'))
    Add-ReportLine $report ("- Static batches: {0}" -f $(Get-Field $viewerParityLine 'batches'))
    Add-ReportLine $report ("- Static triangles: {0}" -f $(Get-Field $viewerParityLine 'triangles'))
    Add-ReportLine $report ("- Vertex hash: {0}" -f $(Get-Field $viewerParityLine 'vertexHash'))
    Add-ReportLine $report ("- Combined hash: {0}" -f $viewerHash)
    Add-ReportLine $report ("- Parity status: {0}" -f $(Get-Field $viewerParityLine 'status'))
    Add-ReportLine $report ("- TIE instances: {0}" -f $(Get-Field $viewerSceneLine 'tieInstances'))
    Add-ReportLine $report ("- Shrub instances: {0}" -f $(Get-Field $viewerSceneLine 'shrubInstances'))
    Add-ReportLine $report ("- Moby instances: {0}" -f $(Get-Field $viewerMobyLine 'instances'))
    Add-ReportLine $report ("- Moby missing: {0}" -f $(Get-Field $viewerMobyLine 'missing'))
    Add-ReportLine $report ("- Moby pose: {0}" -f $(Get-Field $viewerPoseLine 'status'))
    Add-ReportLine $report ("- Moby skin execution: {0}" -f $(Get-Field $viewerSkinLine 'status'))
    Add-ReportLine $report ("- Ratchet animation: {0}" -f $(Get-Field $viewerRatchetAnimLine 'status'))
    Add-ReportLine $report ("- Ratchet skin execution: {0}" -f $(Get-Field $viewerRatchetSkinLine 'status'))
} else {
    Add-ReportLine $report '- Status: **SKIPPED** (build failed)'
}
Add-ReportLine $report ''

Add-ReportLine $report '## Native Runtime'
Add-ReportLine $report ''
if ($runtime) {
    Add-ReportLine $report ("- Status: **{0}**" -f $(if ($runtimePass) { 'PASS' } else { 'FAIL' }))
    Add-ReportLine $report ("- Launched: {0}" -f $(Get-SummaryValue $runtime.Lines 'Launched'))
    Add-ReportLine $report ("- Alive at duration: {0}" -f $(Get-SummaryValue $runtime.Lines 'Alive at duration'))
    Add-ReportLine $report ("- Duration: {0} s" -f $RuntimeSeconds)
    Add-ReportLine $report ("- Graphics activity: {0}" -f $(Get-SummaryValue $runtime.Lines 'Graphics activity'))
    Add-ReportLine $report ("- SIF completions: {0}" -f $(Get-SummaryValue $runtime.Lines 'SIF completions'))
    Add-ReportLine $report ("- Replacements bootstrap install errors: {0}" -f $(Get-Field $runtimeReplacementBootstrapLine 'install_errors'))
    Add-ReportLine $report ("- Replacements runtime install errors: {0}" -f $(Get-Field $runtimeReplacementRuntimeLine 'install_errors'))
    Add-ReportLine $report ("- Renderer owner: {0}" -f $(Get-Field $runtimeOwnershipLine 'owner'))
    Add-ReportLine $report ("- Native level: {0}" -f $(Get-Field $runtimeFrameLine 'nativeLevel'))
    Add-ReportLine $report ("- Level mapped/materialized/rendered: {0}/{1}/{2}" -f $(Get-Field $runtimeFrameLine 'mapped'), $(Get-Field $runtimeFrameLine 'materialized'), $(Get-Field $runtimeFrameLine 'rendered'))
    Add-ReportLine $report ("- Renderer: {0}" -f $(Get-Field $runtimeFrameLine 'renderer'))
    Add-ReportLine $report ("- Camera: {0}" -f $(Get-Field $runtimeFrameLine 'camera'))
    Add-ReportLine $report ("- Sky: {0}; mapped={1}; rendered={2}; deferred={3}" -f $(Get-Field $runtimeFrameLine 'sky'), $(Get-Field $runtimeFrameLine 'skyMapped'), $(Get-Field $runtimeFrameLine 'skyRendered'), $(Get-Field $runtimeFrameLine 'skyDeferred'))
    Add-ReportLine $report ("- Moby pool: {0}; active={1}; rendered={2}; deferred={3}; unaccounted={4}" -f $(Get-Field $runtimeFrameLine 'mobyPool'), $(Get-Field $runtimeFrameLine 'liveMobyActive'), $(Get-Field $runtimeFrameLine 'liveMobyRendered'), $(Get-Field $runtimeFrameLine 'liveMobyDeferred'), $(Get-Field $runtimeFrameLine 'liveMobyUnaccounted'))
    Add-ReportLine $report ("- Ratchet identity/frame/GPU: {0}/{1}/{2}" -f $(Get-Field $runtimeFrameLine 'ratchetIdentity'), $(Get-Field $runtimeFrameLine 'ratchetFrame'), $(Get-Field $runtimeFrameLine 'ratchetGpu'))
    Add-ReportLine $report ("- Animation/transform: {0}/{1}" -f $(Get-Field $runtimeFrameLine 'animation'), $(Get-Field $runtimeFrameLine 'transform'))
    Add-ReportLine $report ("- Runtime render status: {0}" -f $(Get-Field $runtimeFrameLine 'status'))
    Add-ReportLine $report ("- Runtime parity hash: {0}" -f $runtimeHash)
    Add-ReportLine $report ("- Runtime parity status: {0}" -f $(Get-Field $runtimeParityLine 'status'))
    Add-ReportLine $report ("- Latest camera status: {0}" -f $(Get-Field $runtimeCameraLine 'status'))
    Add-ReportLine $report ("- Latest sky status: {0}" -f $(Get-Field $runtimeSkyLine 'status'))
    Add-ReportLine $report ("- Latest Ratchet animation status: {0}" -f $(Get-Field $runtimeRatchetAnimationLine 'status'))
    Add-ReportLine $report ("- Latest Ratchet transform status: {0}" -f $(Get-Field $runtimeRatchetTransformLine 'status'))
    Add-ReportLine $report ("- Retail overlay materialization evidence: {0}" -f $(if ($runtimeOverlayPass) { 'ok' } else { 'invalid-or-missing' }))
    Add-ReportLine $report ("- wad_158 overlay AOT dispatch: {0}; functions={1}; installed={2}" -f $(if ($runtimeWad158OverlayAotStatus) { $runtimeWad158OverlayAotStatus } else { '(missing)' }), $(if ($runtimeWad158OverlayAotFunctions) { $runtimeWad158OverlayAotFunctions } else { '(missing)' }), $(if ($runtimeWad158OverlayAotInstalled) { $runtimeWad158OverlayAotInstalled } else { '(missing)' }))
    Add-ReportLine $report ("- Level-0 overlay AOT dispatch: {0}; generation={1}; functions={2}; installed={3}; table={4}..{5}" -f $(if ($runtimeLevel0OverlayAotStatus) { $runtimeLevel0OverlayAotStatus } else { '(missing)' }), $(if ($runtimeLevel0OverlayAotGenerationEntry) { $runtimeLevel0OverlayAotGenerationEntry } else { '(missing)' }), $(if ($runtimeLevel0OverlayAotFunctions) { $runtimeLevel0OverlayAotFunctions } else { '(missing)' }), $(if ($runtimeLevel0OverlayAotInstalled) { $runtimeLevel0OverlayAotInstalled } else { '(missing)' }), $(if ($runtimeLevel0OverlayAotTableBase) { $runtimeLevel0OverlayAotTableBase } else { '(missing)' }), $(if ($runtimeLevel0OverlayAotTableEnd) { $runtimeLevel0OverlayAotTableEnd } else { '(missing)' }))
    Add-ReportLine $report ("- Overlay generation order ELF -> [optional wad_158] -> level_00: {0}" -f $(if ($runtimeOverlayGenerationOrderPass) { 'ok' } else { 'FAIL' }))
    Add-ReportLine $report ("- Ratchet lifecycle-state diagnostic: {0}" -f $(if ($runtimeRatchetControlStateStatus) { $runtimeRatchetControlStateStatus } else { '(missing)' }))
    Add-ReportLine $report ("- Ratchet lifecycle-state ratchet/pvar/state: {0}/{1}/{2}" -f $(if ($runtimeRatchetControlRatchet) { $runtimeRatchetControlRatchet } else { '(missing)' }), $(if ($runtimeRatchetControlPVar) { $runtimeRatchetControlPVar } else { '(missing)' }), $(if ($runtimeRatchetControlState) { $runtimeRatchetControlState } else { '(missing)' }))
    Add-ReportLine $report ("- Ratchet lifecycle-state backlink: stateRatchetMoby={0}; match={1}" -f $(if ($runtimeRatchetControlStateRatchet) { $runtimeRatchetControlStateRatchet } else { '(missing)' }), $(if ($runtimeRatchetControlBacklinkMatch) { 'YES' } else { 'NO' }))
    Add-ReportLine $report ("- Ratchet lifecycle-state companion/oClass/live/callback: {0}/{1}/{2}/{3}" -f $(if ($runtimeRatchetControlCompanion) { $runtimeRatchetControlCompanion } else { '(missing)' }), $(if ($runtimeRatchetControlCompanionOClass) { $runtimeRatchetControlCompanionOClass } else { '(missing)' }), $(if ($runtimeRatchetControlCompanionLive) { $runtimeRatchetControlCompanionLive } else { '(missing)' }), $(if ($runtimeRatchetUpdateCallback) { $runtimeRatchetUpdateCallback } else { '(missing)' }))
    Add-ReportLine $report ("- Gameplay overlay status: {0}" -f $(if ($runtimeOverlayStatus) { $runtimeOverlayStatus } else { '(missing)' }))
    Add-ReportLine $report ("- Gameplay overlay segments/payload: {0}/{1}" -f $(if ($runtimeOverlaySegments) { $runtimeOverlaySegments } else { '(missing)' }), $(if ($runtimeOverlayPayloadBytes) { $runtimeOverlayPayloadBytes } else { '(missing)' }))
    Add-ReportLine $report ("- Gameplay overlay materialized: {0}/{1} segments; {2}/{3} bytes" -f $(if ($runtimeOverlayMaterializedSegments) { $runtimeOverlayMaterializedSegments } else { '(missing)' }), $(if ($runtimeOverlaySegments) { $runtimeOverlaySegments } else { '(missing)' }), $(if ($runtimeOverlayMaterializedBytes) { $runtimeOverlayMaterializedBytes } else { '(missing)' }), $(if ($runtimeOverlayPayloadBytes) { $runtimeOverlayPayloadBytes } else { '(missing)' }))
    Add-ReportLine $report ("- Gameplay overlay static conflicts: {0}/{1}; fallback entries={2}" -f $(if ($runtimeOverlayConflictingEntries) { $runtimeOverlayConflictingEntries } else { '(missing)' }), $(if ($runtimeOverlayComparableEntries) { $runtimeOverlayComparableEntries } else { '(missing)' }), $(if ($runtimeOverlayFallbackEntries) { $runtimeOverlayFallbackEntries } else { '(missing)' }))
} else {
    Add-ReportLine $report '- Status: **SKIPPED** (build failed)'
}
Add-ReportLine $report ''

Add-ReportLine $report '## Viewer / Runtime Render Parity'
Add-ReportLine $report ''
Add-ReportLine $report ("- Viewer: {0}" -f $(if ($viewerHash) { $viewerHash } else { '(missing)' }))
Add-ReportLine $report ("- Runtime: {0}" -f $(if ($runtimeHash) { $runtimeHash } else { '(missing)' }))
Add-ReportLine $report ("- Match: **{0}**" -f $(if ($parityMatch) { 'YES' } else { 'NO' }))
Add-ReportLine $report ''

Add-ReportLine $report '## Repository State'
Add-ReportLine $report ''
if (-not $gitAvailable) {
    Add-ReportLine $report '- Repository: **UNAVAILABLE** (git was not found on PATH)'
} elseif ($repoStatus.Count -eq 0) {
    Add-ReportLine $report '- Repository: clean'
} else {
    Add-ReportLine $report '- Repository: dirty'
    Add-ReportLine $report '````text'
    foreach ($line in $repoStatus) { Add-ReportLine $report $line }
    Add-ReportLine $report '````'
}
Add-ReportLine $report ('- `third_party/PS2Recomp`: {0}' -f $(if (-not $gitAvailable) { 'unavailable' } elseif ($submoduleStatus.Count -eq 0) { 'clean' } else { 'modified' }))
Add-ReportLine $report ''

Add-ReportLine $report '## Full Embedded Evidence'
Add-ReportLine $report ''
Add-ReportLine $report 'Every raw stream used by this verification run is embedded here so the report can be reviewed and searched without any companion files.'
Add-ReportLine $report ''
Add-EmbeddedLogSection $report 'Build log' @($build.Lines)
Add-EmbeddedLogSection $report 'CTest log' $(if ($ctest) { @($ctest.Lines) } elseif (Test-Path -LiteralPath $ctestLog) { @(Get-Content -LiteralPath $ctestLog) } else { @('(not run)') })
Add-EmbeddedLogSection $report 'Level viewer log' $(if ($viewer) { @($viewer.Lines) } else { @('(not run)') })
Add-EmbeddedLogSection $report 'Runtime harness log' $(if ($runtime) { @($runtime.Lines) } else { @('(not run)') })
Add-EmbeddedLogSection $report 'Runtime stdout' @($runtimeStdoutLines)
Add-EmbeddedLogSection $report 'Runtime stderr' @($runtimeStderrLines)
Add-EmbeddedLogSection $report 'Git status' @($gitLogLines)

$report | Set-Content -LiteralPath $reportPath -Encoding UTF8
Remove-Item -LiteralPath $tempDirectory -Recurse -Force -ErrorAction SilentlyContinue
Update-VerificationProgress 'Complete' 100 $(if ($overallPass) { 'PASS' } else { 'FAIL' })
Write-Progress -Id 1 -Activity 'OpenRatchet native verification' -Completed

Write-Host ''
Write-Host ("Verification: {0}" -f $(if ($overallPass) { 'PASS' } else { 'FAIL' }))
Write-Host ("Report:       {0}" -f (Get-RelativePath $reportPath))
Write-Host 'Upload this single Markdown file for review.'

if (-not $overallPass) {
    exit 1
}
exit 0
