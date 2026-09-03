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

### Phase-8 change under verification

- Decode the retail NTSC gameplay WAD alongside the renderer-owned level core
  so native scene code can use authentic tie/shrub instance transforms without
  routing those assets through guest RAM.
- Generalize the R&C1 paletted texture decoder across the tfrag, tie and shrub
  LevelCore texture tables.
- Add native R&C1 tie LOD0 and shrub mesh decoders. They reconstruct the stored
  GS command layout into host triangles, resolve class-local texture slots
  through the LevelCore class tables, and apply gameplay instance matrices.
- Add a native R&C1 sky decoder for shell geometry, vertex colours and its
  self-contained PSMT8 texture/palette data.
- Expand `native_level_viewer` from textured tfrag terrain to a combined native
  scene containing tfrags, ties, shrubs and camera-relative sky shells.
- Keep the normal recompiled-game runtime untouched; the separate viewer
  remains a renderer-development microscope until live game-state integration.

### Acceptance test

A Release build and all twelve CTest tests must pass, including the new
`rac1_static_scene` and `rac1_sky` format tests. `native_level_inspector` must
remain `status=ok` and report a nonzero decompressed gameplay WAD.
`tools/run-native-level-viewer.ps1 -LevelIndex 0` must print both
`[OpenRatchet:tfrag] ... status=ok` and `[OpenRatchet:scene] ... status=ok`.
Authentic tie/shrub instance geometry and sky must be spatially coherent with
Phase-7 terrain, with no exploded meshes or materially incorrect texture
mapping. The normal 20-second runtime regression must remain alive with the
native WAD path intact.

### Next phase after acceptance

Phase 9 adds native moby class/model rendering and retail moby instances so
Ratchet, crates, NPCs, enemies and other dynamic game objects can enter the
native scene before animation and live game-state integration.
