param(
    [Parameter(Mandatory = $true)]
    [string]$OracleZip,
    [string]$Output,
    [int]$DebugPort = 21512,
    [int]$TimeoutSeconds = 300
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tool = Join-Path $PSScriptRoot 'pcsx2_boot_exit_writer.py'

if (-not (Test-Path -LiteralPath $OracleZip -PathType Leaf)) {
    throw "Retail oracle ZIP not found: $OracleZip"
}
if (-not $Output) {
    $stamp = Get-Date -Format 'yyyy-MM-dd_HH-mm-ss'
    $Output = Join-Path $root ("RAC1_RETAIL_BOOT_EXIT_WRITER_{0}.zip" -f $stamp)
}

Write-Host 'Retail Boot-exit writer proof v4 - breakpoint only' -ForegroundColor Cyan
Write-Host '  1. Launch the same PCSX2 DebugServer build used by the Level-0 oracle.'
Write-Host '  2. Start Ratchet & Clank from a fresh Retail boot and PAUSE it once while Boot code is resident.'
Write-Host '  3. Remove unrelated debugger breakpoints/watchpoints.'
Write-Host '  4. Run this command. The tool resumes Retail automatically.'
Write-Host '  5. Navigate normally until the capture finishes. Do not manually pause PCSX2.'
Write-Host ''
Write-Host 'NO memory watchpoints are used.' -ForegroundColor Yellow
Write-Host 'Only three rare executable breakpoints are armed:'
Write-Host '  0x0022E19C  JR $ra immediately before the gp-relative delay-slot writer'
Write-Host '  0x001EBC2C  Boot-loop escape after 0x15F5B0 != 0'
Write-Host '  0x0012DA00  outer generation-dispatcher return'
Write-Host 'Source audit correction:' -ForegroundColor DarkYellow
Write-Host '  0x0022E190 writes 0x15F600, NOT 0x15F5B0.'
Write-Host '  0x0022E1A0 is the JR delay-slot: sw $v0,-0x7650($gp).'
Write-Host '  The capture proves the effective address from the live Retail $gp value.'
Write-Host 'The tool never writes guest RAM and requires no OpenRatchet native rebuild.' -ForegroundColor DarkGray

$python = Get-Command python -ErrorAction SilentlyContinue
if (-not $python) {
    $python = Get-Command py -ErrorAction SilentlyContinue
}
if (-not $python) {
    throw 'Python was not found on PATH.'
}

& $python.Source $tool capture `
    --oracle $OracleZip `
    --output $Output `
    --debug-port $DebugPort `
    --timeout $TimeoutSeconds
if ($LASTEXITCODE -ne 0) {
    throw "Retail Boot-exit writer proof v4 failed with exit code $LASTEXITCODE"
}

Write-Host "Trace: $Output" -ForegroundColor Green
