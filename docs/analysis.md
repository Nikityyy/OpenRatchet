# Ghidra Analysis Pipeline

This document explains the disassembly and analysis pipeline for OpenRatchet.

## Overview

We use Ghidra with the `ghidra-emotionengine-reloaded` plugin to parse the stripped PS2 ELF (`SCUS_971.99`) and recover function boundaries. Since the binary lacks debug symbols, control flow analysis is essential to generate the function map used by PS2Recomp.

## Pipeline Steps

1. **Ghidra Analysis**: We run Ghidra in headless mode. It imports the ELF and runs its auto-analysis using the MIPS R5900 processor definition.
2. **Export**: The `ExportPS2Functions.java` post-script extracts the analyzed function names and address ranges.
3. **ps2xAnalyzer Fallback**: If Ghidra isn't installed or configured, `ps2xAnalyzer.exe` from the PS2Recomp toolchain can perform a basic pass to generate the base `rc1.toml` config file.
4. **Function Map Generation**: The `tools/analyze.py` script combines any generated boundaries along with known manual exceptions into a final CSV map.

## How to run Headless Analysis

Ensure you have the `GHIDRA_HOME` environment variable set to your Ghidra installation directory. Then run:

```powershell
python tools/analyze.py --elf data/raw/SCUS_971.99
```

This will:
- Run Ghidra headless (if `GHIDRA_HOME` is valid)
- Or fallback to `ps2xAnalyzer.exe`
- Generate `data/analysis/auto-map.csv`
- Update `data/analysis/rc1.toml` with the `ghidra_output` field

## Known Manual Exceptions

Some functions are completely missed by auto-analysis due to non-standard prologues or obfuscated control flow in the original game. We manually inject these:

- `sub_0011DC18` (0x0011DC18 - 0x0011DCC8)
- `sub_001E9488` (0x001E9488 - 0x001E9658)
- `sub_001E9658` (0x001E9658 - 0x001E9AB8)

## Adding New Exceptions

If the recompiled game crashes with `MISSING-TARGET: 0x...`, or you find a function that is incorrectly sized:
1. Open the ELF in the Ghidra GUI.
2. Navigate to the missing address.
3. Manually disassemble and define the function.
4. Add its details to the `exceptions` list inside `tools/analyze.py`.
5. Rerun the script.
