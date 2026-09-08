[CmdletBinding()]
param(
    [ValidateSet('Debug', 'Release', 'RelWithDebInfo', 'MinSizeRel')]
    [string]$Configuration = 'Release',

    [ValidateRange(0, 1000)]
    [int]$LevelIndex = 0,

    [ValidateRange(1, 3600)]
    [int]$RuntimeSeconds = 20,

    [ValidateRange(1, 3600)]
    [double]$ViewerSmokeSeconds = 3
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$verificationRoot = Join-Path $repoRoot 'build\native\verification'
$runDirectory = Join-Path $verificationRoot $stamp
$latestReport = Join-Path $verificationRoot 'latest.md'
$reportPath = Join-Path $runDirectory 'report.md'
New-Item -ItemType Directory -Path $runDirectory -Force | Out-Null

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

function Invoke-CapturedCommand(
    [string]$filePath,
    [string[]]$arguments,
    [string]$logPath
) {
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $output = @()
    $exitCode = 1
    $stdoutPath = "$logPath.stdout.tmp"
    $stderrPath = "$logPath.stderr.tmp"

    Remove-Item -LiteralPath $stdoutPath, $stderrPath -Force -ErrorAction SilentlyContinue

    try {
        # Do not capture native stderr through PowerShell's 2>&1 pipeline. Windows
        # PowerShell turns redirected native stderr into ErrorRecord/RemoteException
        # objects, and that can both corrupt the evidence log and make a successful
        # .cmd/.exe invocation look failed. Start the process directly, redirect the
        # two OS streams to files, and use Process.ExitCode as the only pass/fail
        # authority.
        $argumentLine = (@($arguments) | ForEach-Object { ConvertTo-NativeArgument ([string]$_) }) -join ' '
        $process = Start-Process `
            -FilePath $filePath `
            -ArgumentList $argumentLine `
            -WorkingDirectory $repoRoot `
            -NoNewWindow `
            -Wait `
            -PassThru `
            -RedirectStandardOutput $stdoutPath `
            -RedirectStandardError $stderrPath

        $exitCode = [int]$process.ExitCode

        # Wrap the entire conditional in @(...), not only Get-Content. In
        # Windows PowerShell an if-statement writes its branch output to the
        # pipeline, which can collapse an empty file to $null (or a one-line
        # file to a scalar). Under StrictMode, reading .Count from that value
        # throws. Keeping the complete conditional inside the array
        # subexpression guarantees a real array for empty, one-line, and
        # multi-line streams.
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

        # Keep parsable stdout first. Stderr is preserved verbatim as evidence but
        # deliberately separated so warnings cannot masquerade as command failure.
        $output = @($stdoutLines)
        if ($stderrLines.Count -gt 0) {
            if ($output.Count -gt 0) { $output += '' }
            $output += '--- stderr ---'
            $output += @($stderrLines)
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

$buildLog = Join-Path $runDirectory 'build.log'
$ctestLog = Join-Path $runDirectory 'ctest.log'
$viewerLog = Join-Path $runDirectory 'viewer.log'
$runtimeHarnessLog = Join-Path $runDirectory 'runtime-harness.log'
$gitStatusLog = Join-Path $runDirectory 'git-status.log'

$overallPass = $true

Write-Stage "Native build ($Configuration)" 'RUN'
$buildScript = Join-Path $repoRoot 'tools\build-native.ps1'
$build = Invoke-CapturedCommand 'powershell.exe' @(
    '-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', $buildScript,
    '-Configuration', $Configuration
) $buildLog
$buildPass = $build.ExitCode -eq 0
if (-not $buildPass) { $overallPass = $false }
Write-Stage "Native build ($Configuration)" $(if ($buildPass) { 'PASS' } else { 'FAIL' }) ("{0}s" -f $build.ElapsedSeconds)

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
        ) $ctestLog
        $ctestPass = $ctest.ExitCode -eq 0
        if (-not $ctestPass) { $overallPass = $false }
        Write-Stage 'CTest' $(if ($ctestPass) { 'PASS' } else { 'FAIL' }) ("{0}s" -f $ctest.ElapsedSeconds)
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
    ) $viewerLog
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

    Write-Stage 'Native runtime harness' 'RUN'
    $runtime = Invoke-CapturedCommand 'powershell.exe' @(
        '-NoProfile', '-ExecutionPolicy', 'Bypass',
        '-File', (Join-Path $repoRoot 'tools\run-native-test.ps1'),
        '-Configuration', $Configuration,
        '-DurationSeconds', "$RuntimeSeconds"
    ) $runtimeHarnessLog

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
} else {
    Write-Stage 'CTest' 'SKIP' 'build failed'
    Write-Stage 'Level viewer smoke' 'SKIP' 'build failed'
    Write-Stage 'Native runtime harness' 'SKIP' 'build failed'
    $overallPass = $false
}

$runtimeStdoutSource = $null
$runtimeStderrSource = $null
$runtimeStdoutCopy = Join-Path $runDirectory 'runtime.stdout.log'
$runtimeStderrCopy = Join-Path $runDirectory 'runtime.stderr.log'
$runtimeLines = @()

if ($runtime) {
    $logsLine = @($runtime.Lines | Where-Object { $_ -match '^\s*Logs:\s+(.+?);\s*(.+?)\s*$' } | Select-Object -Last 1)
    if ($logsLine.Count -gt 0) {
        $m = [regex]::Match($logsLine[0], '^\s*Logs:\s+(.+?);\s*(.+?)\s*$')
        $runtimeStdoutSource = $m.Groups[1].Value.Trim()
        $runtimeStderrSource = $m.Groups[2].Value.Trim()
    }

    if ($runtimeStdoutSource -and (Test-Path -LiteralPath $runtimeStdoutSource)) {
        Copy-Item -LiteralPath $runtimeStdoutSource -Destination $runtimeStdoutCopy -Force
        $runtimeLines += @(Get-Content -LiteralPath $runtimeStdoutSource)
    }
    if ($runtimeStderrSource -and (Test-Path -LiteralPath $runtimeStderrSource)) {
        Copy-Item -LiteralPath $runtimeStderrSource -Destination $runtimeStderrCopy -Force
        $runtimeLines += @(Get-Content -LiteralPath $runtimeStderrSource)
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

if ($buildPass) {
    $runtimePass = (
        $runtimePass -and
        (Get-Field $runtimeOwnershipLine 'status') -eq 'ok' -and
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
$gitLogLines | Set-Content -LiteralPath $gitStatusLog -Encoding UTF8

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

$report = [System.Collections.Generic.List[string]]::new()
Add-ReportLine $report '# OpenRatchet Native Verification'
Add-ReportLine $report ''
Add-ReportLine $report ("Timestamp: {0}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss K'))
Add-ReportLine $report ("Commit: {0}" -f $(if ($head.Count -gt 0) { $head[0] } else { 'unknown' }))
Add-ReportLine $report ("Configuration: {0}" -f $Configuration)
Add-ReportLine $report ("Level: {0}" -f $LevelIndex)
Add-ReportLine $report ("Overall: **{0}**" -f $(if ($overallPass) { 'PASS' } else { 'FAIL' }))
Add-ReportLine $report ''

Add-ReportLine $report '## Build'
Add-ReportLine $report ''
Add-ReportLine $report ("- Status: **{0}**" -f $(if ($buildPass) { 'PASS' } else { 'FAIL' }))
Add-ReportLine $report ("- Exit code: {0}" -f $build.ExitCode)
Add-ReportLine $report ("- Elapsed: {0} s" -f $build.ElapsedSeconds)
Add-ReportLine $report ('- Raw log: `{0}`' -f (Get-RelativePath $buildLog))
if (-not $buildPass) {
    Add-ReportLine $report ''
    Add-ReportLine $report '### Failure excerpt'
    Add-ReportLine $report ''
    Add-ReportLine $report '```text'
    foreach ($line in (Get-FailureExcerpt $build.Lines)) {
        Add-ReportLine $report ([string]$line)
    }
    Add-ReportLine $report '```'
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
    Add-ReportLine $report ('- Raw log: `{0}`' -f (Get-RelativePath $ctestLog))
} elseif ($buildPass) {
    Add-ReportLine $report '- Status: **FAIL** (ctest was not found on PATH)'
    Add-ReportLine $report ('- Raw log: `{0}`' -f (Get-RelativePath $ctestLog))
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
    Add-ReportLine $report ('- Raw log: `{0}`' -f (Get-RelativePath $viewerLog))
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
    Add-ReportLine $report ('- Harness log: `{0}`' -f (Get-RelativePath $runtimeHarnessLog))
    if (Test-Path -LiteralPath $runtimeStdoutCopy) { Add-ReportLine $report ('- Runtime stdout: `{0}`' -f (Get-RelativePath $runtimeStdoutCopy)) }
    if (Test-Path -LiteralPath $runtimeStderrCopy) { Add-ReportLine $report ('- Runtime stderr: `{0}`' -f (Get-RelativePath $runtimeStderrCopy)) }
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
    Add-ReportLine $report '```text'
    foreach ($line in $repoStatus) { Add-ReportLine $report $line }
    Add-ReportLine $report '```'
}
Add-ReportLine $report ('- `third_party/PS2Recomp`: {0}' -f $(if (-not $gitAvailable) { 'unavailable' } elseif ($submoduleStatus.Count -eq 0) { 'clean' } else { 'modified' }))
Add-ReportLine $report ('- Raw status: `{0}`' -f (Get-RelativePath $gitStatusLog))
Add-ReportLine $report ''

Add-ReportLine $report '## Raw Evidence Directory'
Add-ReportLine $report ''
Add-ReportLine $report ('`{0}`' -f (Get-RelativePath $runDirectory))
Add-ReportLine $report ''
Add-ReportLine $report 'Send `build/native/verification/latest.md` for normal review. Send a raw log only when a specific failure requires deeper inspection.'

$report | Set-Content -LiteralPath $reportPath -Encoding UTF8
Copy-Item -LiteralPath $reportPath -Destination $latestReport -Force

Write-Host ''
Write-Host ("Verification: {0}" -f $(if ($overallPass) { 'PASS' } else { 'FAIL' }))
Write-Host ("Report:       {0}" -f (Get-RelativePath $latestReport))
Write-Host ("Raw evidence: {0}" -f (Get-RelativePath $runDirectory))

if (-not $overallPass) {
    exit 1
}
exit 0
