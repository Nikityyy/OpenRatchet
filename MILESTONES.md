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
| M3 — Native VFS / asset access | `DONE` | Known file/WAD/resource loads use a native indexed filesystem instead of sector-specific CDVD/SIF startup injection. |
| M4 — Native Ratchet scene renderer | `IN PROGRESS` | Authentic R&C1 level assets render through a PC-native Ratchet renderer without requiring a PS2 GS framebuffer. |
| M5 — First authentic game-driven native frame | `TODO` | Running recompiled game logic supplies camera/object/scene state to the native renderer continuously. |
| M6 — Native input and playable area | `TODO` | PC controller/keyboard input drives original game simulation in a controllable area. |
| M7 — Gameplay/platform completion | `TODO` | Rendering coverage, audio, saves, streaming, UI, cutscenes, transitions and gameplay systems support a complete playthrough. |
| M8 — Release hardening | `TODO` | Reproducible release builds, performance, compatibility, configuration and documented limitations. |

## Active work

### Current milestone

M4 — Native Ratchet scene renderer.

### Phase-7 change under verification

- Extend the native level-core load result with the complete core-index and
  GS-RAM blobs required by renderer-owned texture decoding.
- Add an independent R&C1 8-bit paletted texture decoder, including the PS2
  CLUT index permutation and alpha expansion.
- Add a native R&C1 tfrag decoder for the five embedded VIF storage buffers. It
  reconstructs common/LOD01/LOD0 vertex streams, LOD0 strips, material changes,
  UVs and vertex colors directly into host triangles without a VIF/VU emulator.
- Move `native_level_viewer` from collision-debug geometry to actual textured
  tfrag terrain rendered through raylib/OpenGL.
- Keep the normal recompiled-game runtime untouched; this phase proves the
  visual terrain asset/renderer path before game-state integration.

### Acceptance test

A Release build and all ten CTest tests must pass, including the new synthetic
`tfrag` and texture format tests. `native_level_inspector` must remain
`status=ok`. `tools/run-native-level-viewer.ps1 -LevelIndex 0` must print a
`[OpenRatchet:tfrag] ... status=ok` line with nonzero tfrag/strip/triangle/batch
counts and open a coherent textured rendering of the authentic level. The
normal 20-second runtime regression must remain alive with the native WAD path
intact.

### Next phase after acceptance

Phase 8 expands native scene coverage beyond terrain: sky plus static visual
objects (ties/shrubs) and their texture/material paths, while keeping the
renderer independent of the PS2 GS/VU pipeline.
