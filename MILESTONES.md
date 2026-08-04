# OpenRatchet Milestones

This is the acceptance checklist for making OpenRatchet progressively playable.
Work on one milestone at a time. Use Ghidra to understand the original ELF and
PCSX2 to establish expected behavior; test the actual fix in the native build.

## Status

- `DONE`: acceptance criteria verified
- `IN PROGRESS`: current work
- `BLOCKED`: a specific external or technical dependency prevents progress
- `TODO`: not started

## Current state

- Native window opens, but shows the runtime's magenta fallback framebuffer.
- The guest repeatedly reaches PC `0x11ac78`.
- The observed counters are `dma=0`, `gif=0`, `gsw=0`, `vif=2`.
- The immediate blocker is startup progress: identify what should update the
  guest word at `0x154f80` and why that producer or synchronization path does
  not complete.

## Milestones

### M0 — Reproducible baseline

Status: `IN PROGRESS`

Acceptance criteria:

- Native build and launch command are recorded.
- The exact ELF, generated-output directory, runtime revision, and test date
  are recorded.
- A clean startup log is saved with the first failure or stall.
- The same startup behavior can be reproduced twice.
- PCSX2 reaches the equivalent original-game startup point for comparison.

Deliverable: a short baseline entry in the test-results section below.

### M1 — Native boot progresses past the current wait

Status: `IN PROGRESS`

Acceptance criteria:

- The producer of the `0x154f80` state is identified with Ghidra and runtime
  evidence.
- The fix is made at the responsible runtime, syscall, interrupt, DMA, IOP,
  or generated-code boundary—not by hiding the polling loop.
- PC `0x11ac78` is no longer the long-term execution state.
- No unimplemented syscall or stub error is introduced.
- Guest threads remain alive and make measurable progress for several seconds.

### M2 — First authentic native frame

Status: `TODO`

Acceptance criteria:

- The guest submits real DMA/GIF/GS work.
- Runtime counters show nonzero graphics activity.
- The host displays a framebuffer produced by the guest, not the magenta
  fallback and not a hard-coded test image.
- Display registers, framebuffer address, resolution, and VRAM contents are
  captured and compared with PCSX2.
- The native process survives at least 5 seconds of continuous frame updates.

### M3 — Title screen and input

Status: `TODO`

Acceptance criteria:

- The title/menu screen is visible for multiple frames.
- Keyboard or controller input is received by the guest.
- At least one menu transition works and can be repeated.
- VSync and input polling do not deadlock or starve guest execution.

### M4 — Start a game and load the first playable area

Status: `TODO`

Acceptance criteria:

- The native runtime loads the required CD/DVD assets from the extracted game
  data.
- SIF/IOP/CDVD requests used by the first level complete correctly.
- Textures, geometry, and level data appear without placeholder rendering.
- The player can enter the first controllable area.

### M5 — Playable core loop

Status: `TODO`

Acceptance criteria:

- Player movement, camera, collision, attacks, and at least one enemy or
  equivalent gameplay interaction work.
- The game runs continuously for at least 10 minutes without a hang, crash,
  runaway CPU usage, or permanent rendering corruption.
- Repeated scene transitions do not leak or corrupt guest state.

### M6 — Supporting game systems

Status: `TODO`

Acceptance criteria:

- Required audio playback works for gameplay and at least one streamed asset.
- Memory-card operations work for save and load.
- Required cutscene, streaming, and loading-screen paths work.
- Controller mappings cover the inputs required by normal gameplay.

### M7 — Regression and handoff

Status: `TODO`

Acceptance criteria:

- The complete tested path from launch to gameplay is documented.
- A repeatable build and test procedure is documented.
- Known limitations are listed separately from actual blockers.
- A clean run from the committed state passes the milestone test set.

## Working rules

1. Do not treat a warning, fallback color, or repeated-PC message as the root
   cause without tracing the path that produced it.
2. Do not count a milestone as complete because the program merely compiles or
   opens a window.
3. Do not count a forced, hard-coded, or synthetic frame as M2.
4. Preserve existing user changes and do not commit automatically.
5. After completing one milestone, stop and wait for review and commit before
   starting the next major milestone.

## Test results

| Date | Milestone | Native result | PCSX2/reference result | Notes |
|---|---|---|---|---|
| 2026-08-04 | M0/M1 | Window opens; magenta fallback; stalls at `0x11ac78` | Not recorded in this run | `dma=0`, `gif=0`, `gsw=0`, `vif=2` |

## Blockers

- Current: determine and repair the startup synchronization path associated
  with guest address `0x154f80`.

