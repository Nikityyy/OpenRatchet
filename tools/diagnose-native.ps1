[CmdletBinding()]
param(
    [ValidateRange(1, 3600)]
    [int]$DurationSeconds = 10,
    [switch]$Build,
    [switch]$KeepProcess,
    [ValidateRange(1, 100)]
    [int]$TailLines = 12
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest

$repoRoot = Split-Path -Parent $PSScriptRoot
Set-Location -LiteralPath $repoRoot

$requiredPaths = @(
    'build\native\Release\openratchet.exe',
    'build\extracted\PS2_MAIN.ELF',
    'generated',
    'third_party\PS2Recomp',
    'tools\run-native-test.ps1'
)

$missing = @($requiredPaths | Where-Object { -not (Test-Path -LiteralPath $_) })
if ($missing.Count -gt 0 -and -not $Build) {
    Write-Error ("Missing required input(s): " + ($missing -join ', ') + ". Re-run with -Build only if the build is the intended next action.")
}

$harnessArgs = @(
    '-NoProfile',
    '-ExecutionPolicy', 'Bypass',
    '-File', (Join-Path $repoRoot 'tools\run-native-test.ps1'),
    '-DurationSeconds', $DurationSeconds
)
if ($Build) {
    $harnessArgs += '-Build'
}
if ($KeepProcess) {
    $harnessArgs += '-KeepProcess'
}

Write-Output "Working directory: $repoRoot"
Write-Output ("Diagnostic command: powershell.exe " + ($harnessArgs -join ' '))

$harnessOutput = @(
    & powershell.exe @harnessArgs 2>&1 | ForEach-Object { "$($_)" }
)
$harnessExit = $LASTEXITCODE
$harnessOutput | Write-Output
Write-Output "Harness exit code: $harnessExit"

function Get-LogPathFromSummary([string]$label, [string[]]$lines) {
    $match = $lines | Where-Object { $_ -match ("^" + [regex]::Escape($label) + ":\s+(.+)$") } | Select-Object -Last 1
    if ($null -eq $match) {
        return $null
    }
    return ($match -replace ("^" + [regex]::Escape($label) + ":\s+"), '').Trim()
}

$stdoutPath = Get-LogPathFromSummary 'Stdout log' $harnessOutput
$stderrPath = Get-LogPathFromSummary 'Stderr log' $harnessOutput
$logDirectory = Join-Path $repoRoot 'build\native\test-logs'

if (-not $stdoutPath -or -not (Test-Path -LiteralPath $stdoutPath)) {
    $stdoutPath = (Get-ChildItem -LiteralPath $logDirectory -Filter '*.stdout.log' -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}
if (-not $stderrPath -or -not (Test-Path -LiteralPath $stderrPath)) {
    $stderrPath = (Get-ChildItem -LiteralPath $logDirectory -Filter '*.stderr.log' -File |
        Sort-Object LastWriteTime -Descending | Select-Object -First 1).FullName
}

if (-not $stdoutPath -or -not (Test-Path -LiteralPath $stdoutPath)) {
    Write-Error 'The native harness did not produce a stdout log.'
}

$stdoutLines = @(Get-Content -LiteralPath $stdoutPath)
$stderrLines = @()
if ($stderrPath -and (Test-Path -LiteralPath $stderrPath)) {
    $stderrLines = @(Get-Content -LiteralPath $stderrPath)
}

Write-Output ''
Write-Output '=== Compact runtime evidence ==='
$summaryLines = @(
    'Launched:',
    'Alive at duration:',
    'Elapsed:',
    'SIF completions:',
    'Latest deferred SIF:',
    'Latest runtime tick:',
    'Graphics activity:'
)
foreach ($prefix in $summaryLines) {
    $harnessOutput |
        Where-Object { $_.TrimStart() -like "$prefix*" } |
        Select-Object -Last 1 |
        ForEach-Object { $_.Trim() } |
        Write-Output
}

Write-Output ("Stdout log: $stdoutPath")
Write-Output ("Stderr log: $stderrPath")

function Write-RecentMatches([string]$title, [string[]]$lines, [string]$pattern, [int]$count) {
    Write-Output ""
    Write-Output "--- $title ---"
    $matches = @($lines | Where-Object { $_ -match $pattern } | Select-Object -Last $count)
    if ($matches.Count -eq 0) {
        Write-Output '(none)'
    } else {
        $matches | Write-Output
    }
}

Write-RecentMatches 'Recent GS/GIF packets and registers' $stdoutLines '\[gs:(gif|reg|copy-reg)\]' $TailLines
Write-RecentMatches 'Recent frame uploads' $stdoutLines '\[frame:upload\]' $TailLines
Write-RecentMatches 'Recent runtime ticks' $stdoutLines '\[run:tick\]' $TailLines
Write-RecentMatches 'Recent SIF transport' ($stdoutLines + $stderrLines) '\[OpenRatchet:SIF\].*(injected completion|deferred data-bearing CALL)' $TailLines
Write-RecentMatches 'Diagnostics' ($stdoutLines + $stderrLines) 'missing-target|unimplemented|stub|error|failed' $TailLines

Write-Output ''
Write-Output '--- Repository state ---'
git status --short

$nestedPath = Join-Path $repoRoot 'third_party\PS2Recomp'
$nestedStatus = @(
    git -c "safe.directory=$nestedPath" -C $nestedPath status --short 2>$null
)
if ($nestedStatus.Count -eq 0) {
    Write-Output 'third_party\PS2Recomp: clean'
} else {
    Write-Output 'third_party\PS2Recomp: modified'
    $nestedStatus | Write-Output
}

exit $harnessExit
