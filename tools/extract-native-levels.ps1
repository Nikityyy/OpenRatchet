[CmdletBinding()]
param(
    [string]$Iso = '',
    [int[]]$Level = @(),
    [switch]$All
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot

if (-not $Iso) {
    $preferred = Join-Path $root 'games\Ratchet & Clank (USA) (En,Fr,De,Es,It).iso'
    if (Test-Path -LiteralPath $preferred -PathType Leaf) {
        $Iso = $preferred
    } else {
        $candidate = Get-ChildItem -LiteralPath (Join-Path $root 'games') -Filter '*.iso' -File |
            Select-Object -First 1
        if (-not $candidate) {
            throw 'No R&C1 ISO found under games/. Pass -Iso explicitly.'
        }
        $Iso = $candidate.FullName
    }
}

$python = Get-Command python.exe -ErrorAction SilentlyContinue
if (-not $python) { $python = Get-Command python -ErrorAction SilentlyContinue }
if (-not $python) { $python = Get-Command py.exe -ErrorAction SilentlyContinue }
if (-not $python) { $python = Get-Command py -ErrorAction SilentlyContinue }
if (-not $python) { throw 'Python was not found on PATH.' }

$script = Join-Path $PSScriptRoot 'extract-native-levels.py'
$toc = Join-Path $root 'build\toc.json'
$outdir = Join-Path $root 'build\extracted\levels'
$argsList = @($script, $Iso, '--toc', $toc, '--outdir', $outdir)
if ($All) {
    $argsList += '--all'
} elseif ($Level.Count -gt 0) {
    foreach ($index in $Level) {
        $argsList += @('--level', [string]$index)
    }
}

Write-Host "ISO:      $Iso"
Write-Host "TOC:      $toc"
Write-Host "Levels:   $outdir"
& $python.Source @argsList
if ($LASTEXITCODE -ne 0) {
    throw "Native level extraction failed with exit code $LASTEXITCODE."
}
