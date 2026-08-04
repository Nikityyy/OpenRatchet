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

- Native startup progresses past the former `0x11ac78` wait.
- The window still shows the runtime's magenta fallback framebuffer; authentic
  rendering is the next milestone.
- Native logs show SIF initialization and follow-up DMA activity.

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

Status: `DONE`

Acceptance criteria:

- The producer of the `0x154f80` state is identified with Ghidra and runtime
  evidence.
- The fix is made at the responsible runtime, syscall, interrupt, DMA, IOP,
  or generated-code boundary—not by hiding the polling loop.
- PC `0x11ac78` is no longer the long-term execution state.
- No unimplemented syscall or stub error is introduced.
- Guest threads remain alive and make measurable progress for several seconds.

Observed result:

- Ghidra identified `FUN_0011a480` installing the missing `0x11a448` and
  `0x11a428` handlers and registering DMAC handler `0x11a948`.
- PCSX2 showed the equivalent original state with `0x154f80 = 1`.
- Native startup stayed alive for 12 seconds with window title `OpenRatchet 2`.
- Native stderr logged the verified INIT response injection, three SIF DMA
  submissions, and `sceSifSetReg reg=0x80000002 ... value=0x1`.
- No `unimplemented`, `stub`, `error`, or `failed` diagnostics were observed;
  existing `SyscallOverride:fallback` diagnostics remain a limitation.

Files changed for M1: `src/main.cpp`, `src/guest_overrides.h`, and
`src/guest_overrides.cpp`.

Build and launch commands:

```powershell
.\tools\build-native.cmd -Configuration Release
.\build\native\Release\openratchet.exe .\build\extracted\PS2_MAIN.ELF
```

The tested launch used the same executable and ELF arguments through a
temporary log-capture process with the duplicate `Path`/`PATH` environment
keys normalized for that child process.

Test logs: `build/native/Release/m1-refactor-20260804-133534.stdout.log` and
`build/native/Release/m1-refactor-20260804-133534.stderr.log`.

### M2 — First authentic native frame

Status: `IN PROGRESS`

Acceptance criteria:

- The guest submits real DMA/GIF/GS work.
- Runtime counters show nonzero graphics activity.
- The host displays a framebuffer produced by the guest, not the magenta
  fallback and not a hard-coded test image.
- Display registers, framebuffer address, resolution, and VRAM contents are
  captured and compared with PCSX2.
- The native process survives at least 5 seconds of continuous frame updates.

Handoff note (2026-08-04):

- Ghidra/PCSX2 tracing identified `FUN_0011a948` as the SIF response handler;
  the native override now injects the verified INIT response and bridges the
  observed `0x80000009`/`0x8000000a` transactions.
- PCSX2 responses captured for requests `0x80000592` and `0x8000059a` return
  `(0x3f570, 0x3fb20)` and `(0x3f648, 0x3fc50)` respectively.
- Native verification after the bridge: process remains alive for 10 seconds,
  SIF completions occur, and graphics activity is still `gif=0`, `gsw=0`.
  The bridge currently loops on request `0x80000003`, returning zero results;
  this is the next trace target.
- Next chat: reset PCSX2, break at `0x0011a948`, capture the response queue for
  the original `0x80000003` request, add only that verified result mapping,
  rebuild, and then trace the first real GIF/GS submission. Do not mark M2
  complete until the acceptance criteria below are observed.

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
6. Codex never creates commits. All changes made during one chat, including
   code and documentation, belong in one user-created commit. Codex prepares
   the changes and supplies the suggested commit message; the user always
   commits.

## Test results

| Date | Milestone | Native result | PCSX2/reference result | Notes |
|---|---|---|---|---|
| 2026-08-04 | M0/M1 | Window opens; magenta fallback; stalls at `0x11ac78` | Not recorded in this run | `dma=0`, `gif=0`, `gsw=0`, `vif=2` |
| 2026-08-04 | M1 | Startup passes the former wait; process alive 12s; SIF response and follow-up DMA logged | DebugServer connected; `0x154f80 = 1`; equivalent startup PC `0x118cc0` | M1 acceptance passed; framebuffer remains fallback |

## Blockers

- None for M1. Next milestone: capture and enable the first authentic native
  frame from guest DMA/GIF/GS activity (M2).
