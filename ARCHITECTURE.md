# OpenRatchet native-port architecture

OpenRatchet is a native PC port, not a title-specific PS2 emulator.

## Ownership rule

The native OpenRatchet host owns the application. PS2Recomp is a fallback
executor for original EE game logic that has not yet been replaced. It is not
the long-term owner of filesystem, rendering, input, audio, saves, or other
platform services.

```text
user-owned Ratchet & Clank ISO
            |
      extraction/indexing
            |
   +--------+---------+
   |                  |
recompiled EE      native assets
 game logic           |
   |                  |
   +--------+---------+
            |
      OpenRatchet host
   +--------+---------+-------------------+
   |        |         |         |         |
 native   native    native    native    native
  VFS    renderer   input     audio     saves
```

## Recompiled game-function boundary

A game function has two possible implementations:

1. a project-owned native replacement, when its semantics are understood; or
2. the PS2Recomp-generated implementation as the fallback.

All address-based replacements are declared through
`runtime::NativeReplacementRegistry`. Generated output remains read-only.
Legacy compatibility wrappers are temporarily declared through the same
boundary so they can be removed subsystem-by-subsystem instead of being hidden
inside startup code.

## Migration rules

- Preserve original game logic unless a native replacement is deliberate and
  evidence-backed.
- Prefer replacing a PS2-facing game/platform API over emulating the hardware
  below it.
- Filesystem work migrates to a native ISO/extracted-data VFS; do not add new
  sector-specific startup injections.
- Rendering migrates to Ratchet-aware native asset/render paths; a working PS2
  GS framebuffer is not a prerequisite for the PC renderer.
- PCSX2/PS2 hardware implementations may be reference or differential oracles,
  but are not shipping architecture.
- Do not hand-edit generated PS2Recomp output to fix a game function. Fix the
  recompiler/metadata or install a root-owned native replacement.
- Every migration phase must leave a deterministic build/test boundary and be
  independently commit-safe.

## Native storage boundary

`platform::NativeVfs` indexes extracted WAD/WAD2 resources from
`build/toc.json` and reconstructs the game's in-memory disc TOC. The game-facing
synchronous sector reader at `0x12f208` is owned by OpenRatchet: ranges backed
by indexed extracted resources are read directly into guest memory from host
files. The TOC loader at `0x12f2b8` is also native and copies the host-derived
table directly to the game's fixed TOC region instead of asking the IOP for the
0x2960-byte blob. Unknown/raw disc ranges still fall back to the generated
EE/CDVD path until their semantics are migrated.

The retail TOC's final 0x98 bytes are preserved exactly as 19 raw level
`SectorRange` entries for the game-visible TOC. Those raw pairs are **not** used
as host-file extents. Native level extraction independently scans TOC sector
references for an authentic `0x2434` amalgamated level header, validates its
level-data/gameplay/occlusion ranges, and records the resulting contiguous span
under `native_levels`. `tools/extract-native-levels.ps1` can therefore extract a
selected level directly from the user's ISO without trusting malformed or
non-file-size values in the retail tail. Older `toc.json` files remain usable:
their 0x28c8-byte known prefix is reconstructed exactly, and the earlier
temporary `leveldirs` representation is accepted during migration.

This boundary is intentionally above CDVD/SIF hardware. New known resources
should be added to the VFS/resource layer rather than implemented as synthetic
CDVD responses or sector-specific guest overrides.

## Temporary legacy layer

`src/guest_overrides.cpp` is retained only to preserve the current verified
boot while native subsystems are introduced. It still contains known technical
debt: SIF response synthesis, address-specific control-flow repair, callback
bridges, and graphics diagnostics. The WAD decompressor's scratchpad/SPR-DMAC
bridge has been deleted; host WAD file I/O and decompression no longer belong
in this compatibility layer. New platform features must not be added there
unless required solely to keep the verified fallback baseline alive during a
bounded migration.

## Native compressed-asset boundary

`assets::decompressWad` is a PS2-independent implementation of the R&C1 WAD
stream semantics used by game function `0x20b618`. It operates only on byte
spans: no scratchpad, SPR DMA, CHCR polling, or PS2Runtime device state is part
of the decoder API.

OpenRatchet now owns `0x20b618` through `game::declareNativeAssetReplacements`.
The replacement copies the encoded stream to host memory, decodes directly into
the caller's guest-RAM output region, returns the decompressed byte count in
`v0`, and jumps back to the original caller. Copying the encoded source before
decoding deliberately preserves correctness for overlapping guest input/output
ranges, matching the old staging behavior without emulating the staging device.

The removed legacy bridge manually filled PS2 scratchpad, synthesized SPR DMA
completion, polled generated wait PCs up to 200,000 times, and produced a known
incorrect boot-WAD result. It is no longer reachable or registered. Correctness
is instead anchored to an independent boot-WAD oracle and a regression manifest
covering all 249 compressed streams across the extracted 165-file WAD2 corpus.

## Native R&C1 level boundary

`assets::loadRac1LevelCore` is the renderer-facing entry point for a validated
R&C1 native level span. It locates the original 0x2434 on-disc level header at
its preserved absolute header sector inside that span, converts the header's
absolute sector ranges to offsets in the extracted level file, reads the
level-data byte-range header, parses the level-core index, and returns the
natively decompressed core bytes. `assets::inspectRac1Level` remains the
metadata-only convenience wrapper.

The LevelCoreHeader table pairs are represented correctly as `{count, offset}`
on disc and normalized to `{offset, count}` in host types. This matters for the
renderer: class/texture table offsets were previously being displayed as counts.
Core offsets such as `tfrags == 0` are valid; R&C1 can place the tfrag block at
the beginning of the decompressed core.

## First native visual boundary

`assets::decodeRac1Collision` decodes the level collision octree directly from
the decompressed core into a host triangle list. It expands octant-local packed
vertices, triangle/quad faces, collision types, and the optional Ratchet-only
collision groups without touching VIF, VU, GIF, GS or guest memory.

`native_level_viewer` is the first renderer-owned executable. It links directly
to the already-present native raylib/OpenGL dependency, uploads the decoded R&C1
geometry as a PC vertex buffer, and displays it with a free camera. This first
visual milestone intentionally uses collision geometry because it is a direct,
fully understood geometry structure; tfrags require their own VIF packet decoder
and texture path. The collision renderer establishes the native GPU ownership
boundary first, then the same viewer/runtime renderer will gain textured tfrags
rather than introducing a PS2 graphics emulator.

Wrench/noclip are reverse-engineering references only; OpenRatchet's parsers are
independent implementations of the retail structures.
