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

### Phase-5 change under verification

- Preserve the retail TOC tail as 19 raw `SectorRange` entries while retaining
  compatibility with old extraction metadata. Do not treat the raw second word
  as a trustworthy host-file size.
- Add a focused native level extractor that discovers authentic `0x2434` level
  envelopes from TOC sector references, validates their internal ranges, and
  computes a separate native extraction span so one real level can be copied
  directly from the user's ISO.
- Add `assets::inspectRac1Level`, an independent host parser for the R&C1
  0x2434 amalgamated level header, level-data ranges, level-core index and
  embedded compressed core.
- Expose the renderer-relevant tfrag/sky/collision offsets, class tables and
  texture-table counts as typed native metadata.
- Keep this phase non-visual and bounded. It is the final asset-structure gate
  before Phase 6 creates the first PC-native Ratchet geometry renderer.

### Acceptance test

A Release build and all seven CTest tests must pass. Then
`tools/extract-native-levels.ps1` must preserve the 19-entry raw retail tail,
discover at least one authentic level envelope, and extract the first validated
native span. `native_level_inspector` must parse that real file with `status=ok`
and report non-corrupt core/render metadata. Finally, the
normal runtime diagnostic must remain alive; after the level metadata refresh it
should report a complete 0x2960 disc TOC and the level catalog.

### Next phase after acceptance

Phase 6 is visual: consume the native level/core data model, decode the first
renderable tfrag geometry/textures, and draw it through a PC-native renderer. A
PS2 GS framebuffer is not part of that path.
