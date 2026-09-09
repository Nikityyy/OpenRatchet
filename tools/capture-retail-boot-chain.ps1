param(
    [Parameter(Mandatory = $true)]
    [string]$OracleZip,
    [string]$Output,
    [int]$DebugPort = 21512,
    [int]$TimeoutSeconds = 300,
    [int]$MaxEvents = 16
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $PSScriptRoot 'pcsx2_boot_chain_trace.py'
$wad158 = Join-Path $root 'build\extracted\wads\wad_158.wad'

if (-not (Test-Path -LiteralPath $OracleZip -PathType Leaf)) {
    throw "Retail oracle ZIP not found: $OracleZip"
}
if (-not (Test-Path -LiteralPath $wad158 -PathType Leaf)) {
    throw "Extracted WAD158 not found: $wad158"
}
if (-not $Output) {
    $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
    $Output = Join-Path $root ("RAC1_RETAIL_BOOT_CHAIN_TRACE_{0}.zip" -f $stamp)
}

Write-Host 'Retail Boot-chain boundary capture' -ForegroundColor Cyan
Write-Host '  1. Launch the same PCSX2 DebugServer build used by the Level-0 oracle.'
Write-Host '  2. Start Ratchet & Clank from a fresh Retail boot and PAUSE it while Boot code is still resident.'
Write-Host '  3. Remove unrelated debugger breakpoints/watchpoints.'
Write-Host '  4. Run this command. The tool resumes Retail automatically.'
Write-Host '  5. Navigate normally through the menu and continue until Veldin/Level 0 loads.'
Write-Host '  6. Do not manually pause PCSX2 while capture is active.'
Write-Host ''
Write-Host 'This version uses NO memory watchpoints. Only the stable generation-call breakpoint is armed,' -ForegroundColor Yellow
Write-Host 'so Retail should remain normally playable apart from very brief generation-handoff captures.' -ForegroundColor Yellow
Write-Host 'No native rebuild is required. The tool is read-only and never loads a runtime snapshot.' -ForegroundColor DarkGray

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command py -ErrorAction SilentlyContinue
}
if (-not $python) {
    throw 'Python was not found on PATH.'
}

& $python.Source $tool capture `
    --oracle $OracleZip `
    --wad158 $wad158 `
    --output $Output `
    --debug-port $DebugPort `
    --timeout $TimeoutSeconds `
    --max-events $MaxEvents
if ($LASTEXITCODE -ne 0) {
    throw "Retail Boot-chain capture failed with exit code $LASTEXITCODE"
}

Write-Host "Trace: $Output" -ForegroundColor Green
