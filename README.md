# OpenRatchet

This repository contains the bootstrap pipeline for a native Ratchet & Clank 1 PC port. It does not contain copyrighted game code or generated recompilation output. The supplied ISO must be user-owned.

## Pipeline

1. `tools/bootstrap.ps1 -Stage Extract` mounts the ISO to copy its EE executable, then runs the upstream `rac-dvd-toc-parser` against hidden sectors and writes `build/extracted` plus `build/toc.json`.

The extraction wrapper sanitizes original developer paths embedded in VAG headers (for example `Z:\I5\sound\spee`) so they cannot escape the selected output directory on Windows.
2. Import the extracted `PS2_MAIN.ELF` into a compatible Ghidra installation with `ghidra-emotionengine-reloaded`, then run `PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java`. Save the generated `build/game.toml`.
3. Run `tools/bootstrap.ps1 -Stage Recompile`. It builds upstream `ps2_recomp` and processes `build/game.toml`. Set the TOML `general.output` field to this repository's `generated/` directory.
4. Configure and build this host with `tools/bootstrap.ps1 -Stage Build`.

Use `-FetchTools` to clone the three supported repositories into `third_party/`. Ghidra is intentionally not downloaded: the extension must match the installed Ghidra release and the export is an interactive project operation.

## Current prerequisites

Required: Python 3, CMake 3.21+, a C++20 compiler, Java/Ghidra, and a PS2Recomp checkout. The bootstrap defaults to `C:\ghidra_12.1.2_PUBLIC_20260605\ghidra_12.1.2_PUBLIC`; use `-GhidraDir` if it moves. Ghidra's GUI launcher is in its root directory, while `analyzeHeadless.bat` is under `support`. `SDL2` and `Vulkan` are not required by the current PS2Recomp runtime; it uses raylib as its host backend. The `GuestMemory` class is only the project-owned boundary for future MMIO/device work; PS2Runtime remains the source of truth for guest execution.

The matching `ghidra-emotionengine-reloaded` extension is detected from Ghidra's per-user extension directory under `%APPDATA%\ghidra\<version>\Extensions`.

The build enables MSVC `/MP` and uses all detected processor cores by default. Override this with `-Jobs N`, for example `-Jobs 12`, if memory pressure becomes a problem.

The ISO currently present is approximately 3.9 GiB. Extraction can take several minutes and requires enough free disk space for duplicated assets.
