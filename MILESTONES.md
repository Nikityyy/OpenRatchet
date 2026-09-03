# OpenRatchet milestones

This file is the concise source of truth for current development. Historical
PS2Runtime/GS-first investigations remain in Git history; the project has now
pivoted to a native-port architecture.

Status values: `DONE`, `IN PROGRESS`, `TODO`.

## Milestone status

| Milestone | Status | Acceptance summary |
|---|---|---|
| M0 — Reproducible recompilation baseline | `DONE` | User-owned game extraction, Ghidra export, PS2Recomp generation, native build and repeatable launch exist. |
| M1 — Legacy boot characterization | `DONE` | Recompiled game reaches deep startup/SIF/DMA/graphics-related execution and provides a usable reference baseline. |
| M2 — Native ownership boundary | `DONE` | OpenRatchet owns the application lifecycle and all game-function replacement registration; PS2Runtime is hidden as an EE fallback with unchanged verified boot behavior. |
| M3 — Native VFS / asset access | `IN PROGRESS` | Known file/WAD/resource loads use a native indexed filesystem instead of sector-specific CDVD/SIF startup injection. |
| M4 — Native Ratchet scene renderer | `TODO` | Authentic R&C1 level assets render through a PC-native Ratchet renderer without requiring a PS2 GS framebuffer. |
| M5 — First authentic game-driven native frame | `TODO` | Running recompiled game logic supplies camera/object/scene state to the native renderer continuously. |
| M6 — Native input and playable area | `TODO` | PC controller/keyboard input drives original game simulation in a controllable area. |
| M7 — Gameplay/platform completion | `TODO` | Rendering coverage, audio, saves, streaming, UI, cutscenes, transitions and gameplay systems support a complete playthrough. |
| M8 — Release hardening | `TODO` | Reproducible release builds, performance, compatibility, configuration and documented limitations. |

## Active work

### Current milestone

M3 — Native VFS / asset access.

### Phase-4 change under verification

- Game function `0x20b618` is now a real native replacement registered through
  `game::declareNativeAssetReplacements`; it no longer calls the generated EE
  decompressor.
- The replacement reads the complete WAD stream from guest RAM, snapshots the
  encoded bytes into host memory, decodes with `assets::decompressWad`, writes
  the authentic output directly to the caller's guest-RAM destination, returns
  the decompressed byte count in `v0`, and returns to `$ra`.
- The decompressor-specific compatibility code has been deleted from
  `guest_overrides.cpp`: no 0x2000 scratchpad preload, no synthetic SPR_FROM
  transfer, no CHCR completion clear, no generated wait-PC loop, and no
  `g_guest20b618Original` fallback remain.
- Correctness remains anchored to the independently established retail boot-WAD
  fingerprint (`0x50e0f` encoded bytes -> `0xa346c` output bytes, output FNV32
  `0xd3cb9822`) and the 249-stream WAD2 corpus regression manifest.
- Native storage remains unchanged: the disc TOC and WAD2/0 sector payload are
  supplied through `NativeVfs`; raw unmigrated disc ranges still retain their
  bounded generated fallback.

### Acceptance test

A Release build and all six CTest tests must pass. A 20-second native diagnostic
must remain alive and preserve graphics activity. The native storage path must
still show the disc TOC and full WAD2/0 read:

```text
[OpenRatchet:VFS] native disc TOC ... destination=0x137b80 bytes=0x2960 ...
[OpenRatchet:VFS] native sector read ... source=0x3809 sectors=0xa2 destination=0x1fa7000 ... asset=wads2/0
```

The authoritative decompressor must now report directly:

```text
[OpenRatchet:WAD] native ... encodedSize=0x50e0f ... bytes=0xa346c ... outputHash=0xd3cb9822 status=ok oracle=match
```

There must be no decompressor-specific legacy diagnostics such as
`[OpenRatchet:DMAC] SPR_FROM`, `[OpenRatchet:DMAC] SPR completion`, or
`[OpenRatchet:WAD] shadow`. Replacement registration should remain
`bootstrap=2`, `runtime=10`, with zero installation errors: one legacy runtime
replacement disappeared and one native asset replacement took ownership of the
same address.

### Next phase after acceptance

Treat the now-authentic decompressed boot archive as a native asset source:
identify its archive entries and the first scene/render resources, add bounded
native parsers with fixtures, and establish the first renderer-facing asset
model. The next graphics work should consume Ratchet semantics/data structures,
not revive GS/SPR hardware emulation.
