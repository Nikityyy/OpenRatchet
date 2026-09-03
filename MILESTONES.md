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

### Phase-9 change under verification

- Decode R&C1 moby class-table entries, LOD0 packet meshes, persistent 512-slot
  packet vertex caches, duplicate cache references, packed strip/material index
  streams, and class-local texture remaps into ordinary host triangles.
- Decode the retail gameplay moby instance block and apply each instance's
  uniform scale, ZYX Euler rotation, position and ambient colour to the native
  class mesh.
- Decode the LevelCore moby texture table through the existing native palette
  texture path and add moby batches to `native_level_viewer` alongside tfrags,
  ties, shrubs and sky.
- Keep this phase deliberately bind-pose/static. Moby skeletons, joints,
  animation sequences and live runtime transforms belong to the next renderer
  phase; gameplay-referenced logic-only classes are counted and skipped rather
  than treated as malformed visual data, while unreferenced class blobs are not
  decoded by the scene pass.
- Keep the normal recompiled-game runtime untouched; the separate viewer
  remains the renderer-development microscope until live game-state integration.

### Acceptance test

A Release build and all thirteen CTest tests must pass, including the new
`rac1_moby` format test. `native_level_inspector` must remain `status=ok`.
`tools/run-native-level-viewer.ps1 -LevelIndex 0` must print the existing
`[OpenRatchet:tfrag] ... status=ok`, `[OpenRatchet:scene] ... status=ok`, and a
new `[OpenRatchet:moby] ... status=ok` line with nonzero rendered instances and
triangles. Authentic moby geometry must be spatially coherent with the accepted
Phase-8 scene and use plausible textures; bind-pose/static objects are expected
at this stage. The normal 20-second runtime regression must remain alive with
the native WAD path intact.

### Next phase after acceptance

Phase 10 adds native moby skeletal/model animation and pose evaluation, then
uses that animated model boundary as the basis for connecting the renderer to
live recompiled-game object state.
