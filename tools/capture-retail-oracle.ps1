[CmdletBinding()]
param(
    [string]$Python = "python",
    [string]$HostName = "127.0.0.1",
    [int]$DebugPort = 21512,
    [int]$PinePort = 28011,
    [int]$SavestateSlot = 9,
    [int]$ArmCountdownSeconds = 5,
    [int]$ScenarioCountdownSeconds = 2,
    [int]$ScenarioRunMs = 500,
    [int]$WriterPageLimit = 12,
    [int]$WriterEventLimit = 48,
    [double]$WriterTimeoutSeconds = 5.0,
    [switch]$NoSpeech,
    [switch]$NoBeep,
    [switch]$FullCapture,
    [switch]$AllowSlowFallback,
    [string]$Output
)

$ErrorActionPreference = "Stop"
$repoRoot = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $PSScriptRoot "pcsx2_level0_oracle.py"
$scenarios = if ($FullCapture) { Join-Path $PSScriptRoot "retail-oracle-level0.json" } else { Join-Path $PSScriptRoot "retail-oracle-level0-phase12.json" }

if (-not (Test-Path $tool)) { throw "Missing oracle tool: $tool" }
if (-not (Test-Path $scenarios)) { throw "Missing scenario manifest: $scenarios" }

Write-Host "OpenRatchet Retail Oracle - Level 0" -ForegroundColor Cyan
if ($FullCapture) { Write-Host "Mode: full 11-scenario archive" -ForegroundColor DarkGray } else { Write-Host "Mode: FAST 7-scenario Phase-12 oracle (idle + movement X/Y + camera X/Y + jump + attack)" -ForegroundColor Green }
Write-Host ""
Write-Host "Requirements:" -ForegroundColor Yellow
Write-Host "  1. Launch the PCSX2 build with DebugServer (TCP 21512)."
Write-Host "  2. Enable PCSX2 Pine IPC in Settings -> Advanced (TCP 28011)."
Write-Host "  3. Boot R&C1 NTSC-U (SCUS-97199) and stand in normal, controllable Veldin gameplay."
Write-Host "  4. Do not leave unrelated debugger breakpoints/watchpoints armed."
Write-Host ""
Write-Host ("The capture is read-only with respect to game memory. It will use savestate slot {0} " -f $SavestateSlot) -NoNewline -ForegroundColor DarkGray
Write-Host "to replay the same Retail frame for each requested physical input." -ForegroundColor DarkGray
Write-Host "The tool first verifies the LIVE Level-0 overlay fingerprint; it does not use the stale boot-ELF checkpoint." -ForegroundColor DarkGray
Write-Host "On Windows, the tool now reads PCSX2 EE RAM directly through its exported read-only EEmem debugger pointer; full snapshots should take seconds, not minutes. DebugServer is kept as a safe fallback." -ForegroundColor DarkGray
Write-Host ""
Write-Host "Hands-free controller mode:" -ForegroundColor Green
Write-Host "  - After launch, you never need to press ENTER or return to this terminal."
Write-Host "  - An ENGLISH Windows voice announces each scenario (if an English voice is installed)."
Write-Host "  - At the DOUBLE BEEP on 2, start holding the requested controller input."
Write-Host "  - Keep holding through CAPTURE; release immediately at the DONE sound."
Write-Host "  - For idle, keep every control released."
Write-Host ""

$pcsx2Pid = $null
try {
    $listen = Get-NetTCPConnection -State Listen -LocalPort $DebugPort -ErrorAction Stop | Select-Object -First 1
    if ($null -ne $listen -and $listen.OwningProcess -gt 0) {
        $pcsx2Pid = [int]$listen.OwningProcess
        Write-Host ("Fast RAM capture: PCSX2 process {0} owns DebugServer port {1}." -f $pcsx2Pid, $DebugPort) -ForegroundColor DarkGray
    }
}
catch {
    Write-Host "Fast RAM capture PID auto-detection unavailable; the Python tool will safely fall back to DebugServer RAM reads." -ForegroundColor DarkYellow
}

$argsList = @(
    $tool,
    "capture",
    "--host", $HostName,
    "--debug-port", "$DebugPort",
    "--pine-port", "$PinePort",
    "--savestate-slot", "$SavestateSlot",
    "--arm-countdown", "$ArmCountdownSeconds",
    "--scenario-countdown", "$ScenarioCountdownSeconds",
    "--scenario-run-ms", "$ScenarioRunMs",
    "--writer-page-limit", "$WriterPageLimit",
    "--writer-event-limit", "$WriterEventLimit",
    "--writer-timeout", "$WriterTimeoutSeconds",
    "--scenarios", $scenarios
)
if ($null -ne $pcsx2Pid) {
    $argsList += @("--pcsx2-pid", "$pcsx2Pid")
}
if (-not $AllowSlowFallback) {
    $argsList += "--require-fast-ram"
}
if ($NoSpeech) {
    $argsList += "--no-speech"
}
if ($NoBeep) {
    $argsList += "--no-beep"
}
if ($Output) {
    $argsList += @("--output", $Output)
}

Push-Location $repoRoot
try {
    & $Python @argsList
    if ($LASTEXITCODE -ne 0) {
        throw "Retail oracle capture failed with exit code $LASTEXITCODE."
    }
}
finally {
    Pop-Location
}
