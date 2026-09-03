# OpenRatchet

OpenRatchet is a native Ratchet & Clank 1 PC-port project. The repository does
not ship copyrighted game data; a user-owned PS2 copy is required.

The long-term architecture is documented in `ARCHITECTURE.md`. OpenRatchet owns
the PC application and progressively replaces PS2 platform services with native
systems. PS2Recomp remains a fallback executor for original EE game logic that
has not yet received a native implementation.

## Bootstrap pipeline

1. `tools/bootstrap.ps1 -Stage Extract` mounts the ISO to copy its EE executable,
   then runs `rac-dvd-toc-parser` against hidden sectors and writes
   `build/extracted` plus `build/toc.json`.
2. Import `build/extracted/PS2_MAIN.ELF` into Ghidra with
   `ghidra-emotionengine-reloaded`, run
   `PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java`, and save the
   generated config as `build/game.toml`.
3. Run `tools/bootstrap.ps1 -Stage Recompile`. The generated PS2Recomp C++ goes
   into `generated/` and is treated as read-only fallback game logic.
4. Configure/build the native host with `tools/bootstrap.ps1 -Stage Build` or
   use the verified incremental helper `tools/build-native.cmd`.

Use `-FetchTools` to clone supported external repositories into `third_party/`.
Ghidra is intentionally not downloaded automatically because the extension must
match the installed Ghidra version.

## Current native boundary

`src/runtime/openratchet_runtime.*` is the top-level host owner. Address-based game
replacements are declared through `src/runtime/native_replacements.*`; current
legacy boot wrappers are routed through that same boundary until their PS2
subsystems are replaced natively.

The existing PS2Runtime-backed boot behavior is intentionally preserved during
this first architecture phase. It is a fallback implementation, not the target
platform architecture.

## Current prerequisites

Required: Python 3, CMake 3.21+, a C++20 compiler, Java/Ghidra, and a PS2Recomp
checkout. The bootstrap defaults to
`C:\ghidra_12.1.2_PUBLIC_20260605\ghidra_12.1.2_PUBLIC`; use `-GhidraDir` if it
moves.

The matching `ghidra-emotionengine-reloaded` extension is detected from
Ghidra's per-user extension directory under
`%APPDATA%\ghidra\<version>\Extensions`.

The build enables MSVC `/MP` and uses all detected processor cores by default.
Override with `-Jobs N` if memory pressure becomes a problem.
