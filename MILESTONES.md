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
| M2 — Native ownership boundary | `IN PROGRESS` | OpenRatchet owns the application lifecycle and all game-function replacement registration; PS2Runtime is hidden as an EE fallback with unchanged verified boot behavior. |
| M3 — Native VFS / asset access | `TODO` | Known file/WAD/resource loads use a native indexed filesystem instead of sector-specific CDVD/SIF startup injection. |
| M4 — Native Ratchet scene renderer | `TODO` | Authentic R&C1 level assets render through a PC-native Ratchet renderer without requiring a PS2 GS framebuffer. |
| M5 — First authentic game-driven native frame | `TODO` | Running recompiled game logic supplies camera/object/scene state to the native renderer continuously. |
| M6 — Native input and playable area | `TODO` | PC controller/keyboard input drives original game simulation in a controllable area. |
| M7 — Gameplay/platform completion | `TODO` | Rendering coverage, audio, saves, streaming, UI, cutscenes, transitions and gameplay systems support a complete playthrough. |
| M8 — Release hardening | `TODO` | Reproducible release builds, performance, compatibility, configuration and documented limitations. |

## Active work

### Current milestone

M2 — Native ownership boundary.

### Phase-1 change under verification

- `OpenRatchetRuntime` becomes the root application owner.
- PS2Recomp/`PS2Runtime` is hidden behind that host as the temporary EE fallback.
- A project-owned `NativeReplacementRegistry` becomes the single address-based
  replacement boundary.
- Existing legacy bootstrap/runtime wrappers are declared through the registry;
  their behavior and installation order are intentionally unchanged.
- Existing non-function graphics compatibility hooks remain explicit legacy
  device bridges until the renderer phase replaces them.
- Generated output and `third_party` remain read-only.

### Acceptance test

A Release build and all CTest tests must pass. A 10-second native diagnostic must
remain alive and show the same boot/SIF/runtime progression as the pre-phase
baseline, plus these new ownership diagnostics:

```text
[OpenRatchet:native] replacements stage=bootstrap declared=2 ... install_errors=0
[OpenRatchet:native] replacements stage=runtime declared=9 ... install_errors=0
[OpenRatchet:native] host owns application runtime; PS2Recomp retained as EE fallback backend
```

No visual or guest-behavior improvement is expected in M2; this phase exists to
make later native subsystem replacement safe and bounded.

### Next phase after acceptance

M3 begins by introducing a native VFS/index over the already extracted game
content and moving the boot-WAD/file-access path out of `guest_overrides.cpp`
without changing game logic.
