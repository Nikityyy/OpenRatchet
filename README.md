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

`src/runtime/openratchet_runtime.*` is the top-level host owner. Address-based
game replacements are declared through `src/runtime/native_replacements.*`;
legacy boot wrappers are routed through that same boundary until their PS2
subsystems are replaced natively.

`src/platform/native_vfs.*` owns indexed access to extracted WAD/WAD2 content
using `build/toc.json`. It also reconstructs the game-visible disc TOC, so the
loader at `0x12f2b8` no longer needs an IOP/SIF request on the native path. The
game-facing sector reader serves indexed resources directly from host files and
falls back to the generated EE/CDVD implementation only for raw ranges that have
not yet been migrated. `build/toc.json` is therefore a required native runtime
artifact alongside `build/extracted/PS2_MAIN.ELF`. New extractions preserve the
retail TOC's final 19 raw per-level `SectorRange` pairs exactly and store
separately validated host extraction spans under `native_levels`.

`src/assets/wad_decompressor.*` is the first native compressed-asset primitive,
and `src/game/native_assets.*` now owns game function `0x20b618`. Compressed
WAD streams are decoded directly in the host and written to guest RAM; the old
scratchpad/SPR-DMAC copy-and-poll bridge has been removed completely. The game
still receives the original function contract: `v0` is the decompressed byte
count and control returns directly to the caller. The target boot WAD is pinned
to an independently established output fingerprint, and the test suite validates
all 249 compressed streams in the 165-file extracted WAD2 corpus against a fixed
aggregate reference manifest.

PS2Runtime remains an EE/game-logic fallback and temporary compatibility backend,
not the target platform architecture.


### Native level extraction / inspection

Phase 5 adds a focused path for renderer assets. The extractor preserves the
raw retail level-table bytes but discovers actual level file spans from validated
`0x2434` amalgamated headers rather than trusting the raw TOC length field. To
refresh the metadata and extract only the first validated R&C1 level, run:

```powershell
.\tools\extract-native-levels.ps1
```

Use `-Level N` for a particular TOC level or `-All` when the complete native
level corpus is needed. Extracted files live under `build/extracted/levels/`.
`native_level_inspector` parses the original on-disc level envelope, level-data
header and level-core index and validates its compressed core with the native
WAD decoder. The parser now also preserves the decompressed core as a native
host buffer for renderer-owned asset decoders.

### Native level viewer

Phase 6 established the PC-native renderer with authentic R&C1 collision
geometry. Phase 7 moved it onto textured tfrag terrain. Phase 8 expands the
same native scene with static visual objects and sky: OpenRatchet decompresses
the retail gameplay WAD, joins tie/shrub class meshes to their authentic
instance matrices, decodes their LevelCore texture tables, and parses the
camera-relative sky shells and self-contained sky textures. These formats are
converted directly to host triangles/RGBA images; no VIF/VU/GS emulator is
involved.

After extracting level 0 and building Release, run:

```powershell
.\tools\run-native-level-viewer.ps1 -LevelIndex 0
```

The window displays the native R&C1 scene currently covered by the renderer:
textured tfrag terrain, ties, shrubs and sky. Use `TAB` to toggle wireframe.
The collision decoder remains covered by tests as an independent geometry
oracle. `native_level_viewer` is a development microscope only; the shipping
port will render these assets inside the normal OpenRatchet runtime using live
game state.

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
