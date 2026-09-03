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

### Phase-6 change under verification

- Correct the LevelCoreHeader array-pair interpretation to the retail
  `{count, offset}` layout and expose the natively decompressed level core as a
  renderer-owned host buffer.
- Add an independent R&C1 collision decoder for the core collision block. It
  expands the octree, packed vertices, triangles/quads, collision types, and
  Ratchet-only collision groups directly into native triangle geometry.
- Add `native_level_viewer`, linked directly to raylib/OpenGL rather than the
  PS2 graphics path. It uploads that authentic level geometry to a PC GPU
  vertex buffer and provides a free camera plus solid/wireframe modes.
- Keep the normal recompiled-game runtime untouched in this phase. The purpose
  is to prove the native renderer/data boundary visually before the more
  complicated tfrag VIF/texture decoder is introduced.

### Acceptance test

A Release build and all eight CTest tests must pass. The updated
`native_level_inspector` must still report `status=ok` and now show plausible
class/texture counts with their separate offsets. Then
`tools/run-native-level-viewer.ps1 -LevelIndex 0` must print
`[OpenRatchet:viewer] ... status=ok`, open a native PC window, and visibly show
the level's authentic collision geometry. The normal 20-second runtime
regression must remain alive with the native WAD path intact.

### Next phase after acceptance

Phase 7 replaces the diagnostic collision-only visual with actual R&C1 tfrag
terrain: decode the five embedded VIF data groups, reconstruct LOD0 triangle
strips, decode the level texture tables/GS palette data, and render textured
terrain through the same native renderer boundary.
