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

### Phase-2 change under verification

- `platform::NativeVfs` indexes the extracted `wads/` and `wads2/` assets from
  `build/toc.json` and validates the required boot WAD.
- The game-facing synchronous sector reader at `0x12f208` is now a native
  OpenRatchet replacement. Indexed WAD/WAD2 ranges are loaded directly from
  host files into guest RAM without traversing the PS2 CDVD/SIF path.
- Raw, not-yet-indexed disc ranges still use the generated EE/CDVD function as
  an explicit fallback. The current sector `0x121` metadata probe is therefore
  preserved for now.
- Boot-WAD sector/count metadata is sourced from the parsed TOC rather than
  hard-coded constants.
- The decompressor compatibility bridge now consumes the WAD already loaded in
  guest RAM; `guest_overrides.cpp` no longer opens or owns the host WAD file.
- A native VFS unit test covers indexing, bounded reads, cross-asset reads,
  unresolved ranges, and atomic failure behavior.

### Acceptance test

A Release build and all CTest tests must pass. A 10-second native diagnostic must
remain alive and reach at least the previous boot/graphics baseline. It should
also show the storage migration explicitly, including lines equivalent to:

```text
[OpenRatchet:VFS] indexed=641 present=641 missing=0 ...
[OpenRatchet:VFS] boot asset=wads2/0 sector=0x3809 sectors=0xa2 ...
[OpenRatchet:VFS] fallback sector read ... source=0x121 sectors=0x1 ...
[OpenRatchet:VFS] published boot metadata sector=0x3809 sectors=0xa2
[OpenRatchet:VFS] native sector read ... source=0x3809 sectors=0xa2 ... asset=wads2/0
```

The native replacement counts should remain `bootstrap=2` and `runtime=9` with
zero installation errors. Graphics output is not expected to improve in this
storage phase; existing graphics activity must not regress.

### Next phase after acceptance

Continue M3 upward from raw sector semantics: identify the next stable
resource/file API boundary and migrate additional game data access so the CDVD
and SIF compatibility paths can shrink instead of gaining new cases.
