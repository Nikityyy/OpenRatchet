# OpenRatchet milestones

This file is the concise source of truth for project status and the next
experiment. Historical states are available in Git; do not append an
investigation diary here.

Status values: `DONE`, `IN PROGRESS`, `BLOCKED`, `TODO`.

## Milestone status

| Milestone | Status | Acceptance summary |
|---|---|---|
| M0 — Reproducible native baseline | `DONE` | Verified build, launch, logs, reference tools, and repeatable startup evidence. |
| M1 — Native boot passes initial synchronization | `DONE` | Guest leaves the former `0x11ac78` wait and remains alive without a new fatal stub. |
| M2 — First authentic native frame | `IN PROGRESS` | Guest-produced VRAM and a continuously presented non-fallback frame. |
| M3 — Title/menu and input | `TODO` | Stable title/menu rendering and repeatable keyboard/controller navigation. |
| M4 — First playable area | `TODO` | Load and enter the first controllable area with authentic assets. |
| M5 — Playable core loop | `TODO` | Movement, camera, collision, combat, enemies, and scene transitions. |
| M6 — Required game systems and content | `TODO` | Audio, streaming, saves, cutscenes, UI, levels, and progression systems. |
| M7 — Complete-game progression | `TODO` | A start-to-credits playthrough with required content and no blocking defects. |
| M8 — Release hardening | `TODO` | Reproducible builds, performance, stability, compatibility, and documented limitations. |

## Active work

### Current milestone

M2 — First authentic native frame.

### Latest verified native state

- Release build succeeds through `tools\build-native.cmd`.
- M1 remains passed: native execution no longer stalls at `0x11ac78`.
- Startup SIF work is active and the guest submits real DMA/GIF traffic.
- The extracted boot WAD reaches the native guest decompressor through a
  temporary root-owned bridge.
- Two authentic WAD-backed SPR windows were observed, decompression returned to
  `0x2017ec`, output at `0x500000` became nonzero, and later SIF descriptors
  contained nonzero payloads.
- The latest stable run remained alive but stopped making progress at guest PC
  `0x1198b0`.
- Latest counters: 56 SIF completions, `dma=556`, `gif=513`, `gsw=0`, `vif=2`.
- Presentation still fails: the raw runtime tick contains
  `dispfb1=0x1400` and `display1=0x1bf27f00000000`, but the host presentation
  selection still reports `displayFbp=0` and `sourceFbp=0`; VRAM has no
  nonzero bytes, the copied frame has no non-black pixels, and no authentic
  primitive draw event was observed.
- Latest verified logs:
  `build/native/test-logs/native-20260805-015843.stdout.log` and
  `build/native/test-logs/native-20260805-015843.stderr.log`.

### Active divergence

Native execution stalls at `0x1198b0` after boot-WAD decompression and the
startup `0x80000006`/SIF data path. PCSX2 continues through the corresponding
resource/staging sequence and eventually produces nonzero graphics data.

The exact wait condition and producer at `0x1198b0` have not yet been recorded
as a bounded native-versus-reference comparison. Until that upstream
transition is understood, further broad GS-register work is premature.

### Next experiment

Investigate only the `0x1198b0` divergence:

1. Map `0x1198b0` to generated output and the containing original function.
2. Identify the exact wait condition, request/buffer involved, expected writer,
   and successful exit PC.
3. Capture one equivalent PCSX2 transition: relevant registers, request fields,
   destination pointer, and destination bytes before/after completion.
4. Classify the owner as SIF/RPC, CDVD, DMA/interrupt, or recompiled control
   flow.
5. Implement only the proven reusable behavior in root-owned code.

Iteration acceptance delta:

- native leaves `0x1198b0` through the authentic path; and
- the affected control/data state matches the PCSX2 capture; and
- no existing M1 or graphics-transfer progress regresses.

This iteration does not need to complete M2. If it exposes a different
subsystem blocker, record that as the single next experiment and stop.

### Known temporary debt

These bridges enabled investigation but are not the desired full-port
architecture:

- `guest_11a948` scans fixed SIF pools and synthesizes command-specific response
  values/pointers. Replace with stateful SIF/RPC transport carrying real data.
- `guest_12f208` loads a named boot WAD from the configured extracted-media
  directory and recognizes startup-specific sector/argument patterns. Replace
  with general CDVD sector/file I/O.
- `guest_20b618` supplies SPR windows and repeatedly resumes a generated
  decompressor around known wait PCs. Replace with generic SPR/DMAC transfer and
  completion semantics.
- `guest_1f97e8` repairs one generated-call destination. Determine and fix the
  underlying dispatch/recompilation/ABI cause.
- The root GIF image-payload adapter prepends an IMAGE tag for separated raw DMA
  payloads. Retain only if a focused test proves this is the correct stable
  public-runtime boundary.
- Interior callback overrides such as VBlank must remain exact translations of
  verified ELF semantics and receive regression coverage.

Do not extend a temporary bridge with another magic case unless the active
experiment proves the value is invariant and no correct reusable layer is
available yet.

## Milestone acceptance criteria

### M0 — Reproducible native baseline

- Build inputs, ELF, generated output, runtime checkout, commands, and logs are
  known and reproducible.
- Native and PCSX2 reference states can be inspected with verified tools.

### M1 — Native boot passes initial synchronization

- The producer of the original startup wait is understood.
- Native leaves `0x11ac78` without bypassing its polling loop.
- Guest execution remains alive and measurable for at least five seconds.
- No new fatal unimplemented syscall/stub is introduced.

### M2 — First authentic native frame

All criteria must pass in one reproducible run:

- Guest execution reaches authentic rendering setup.
- Real guest DMA/GIF/GS work reaches the graphics runtime.
- GS privileged display state and framebuffer addresses are nonzero and match
  the equivalent PCSX2 state.
- Guest-produced VRAM contains nonzero frame data.
- The host displays that framebuffer, not the magenta fallback, a hard-coded
  image, host-injected pixels, or another synthetic substitute.
- Frames continue updating for at least five seconds.
- The native process remains stable and the M1 path does not regress.

### M3 — Title/menu and input

- Title/menu rendering is stable for multiple frames.
- Keyboard and controller input reach the guest.
- At least one menu transition is repeatable.
- VSync and input polling do not deadlock or starve guest execution.

### M4 — First playable area

- Required assets load through general CDVD/SIF/streaming behavior.
- Textures, geometry, UI, and level data are authentic.
- The player enters the first controllable area.

### M5 — Playable core loop

- Movement, camera, collision, attacks, enemies, pickups, and scene transitions
  work.
- A representative gameplay session runs for at least 10 minutes without a
  hang, crash, runaway CPU use, or persistent rendering corruption.

### M6 — Required game systems and content

- Audio, music, streaming, cutscenes, loading screens, saving/loading, UI, and
  controller mappings work.
- Level-specific behavior is generalized into subsystem support rather than a
  growing table of per-level bypasses.

### M7 — Complete-game progression

- A new game can progress through every required planet/level and reach the
  ending/credits.
- Required weapons, gadgets, vendors, missions, bosses, cinematics, deaths,
  checkpoints, and saves function without blocking defects.
- Repeated transitions and long sessions do not corrupt guest or host state.

### M8 — Release hardening

- Clean setup/build/run procedures are documented and reproducible.
- Automated smoke and subsystem regression tests cover the supported path.
- Performance and frame pacing are acceptable on the supported PC target.
- Known limitations are explicit and are not confused with passed behavior.

## Verified commands

Build only after source changes or when the executable is stale:

```powershell
.\tools\build-native.cmd -Configuration Release
```

Run the compact native diagnostic when the active experiment requires runtime
evidence:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\tools\diagnose-native.ps1 -DurationSeconds 10
```

Underlying harness:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\tools\run-native-test.ps1 -DurationSeconds 10
```

## Latest evidence

| Date | Iteration | Result | Acceptance |
|---|---|---|---|
| 2026-08-04 | M1 startup synchronization | Left `0x11ac78`; process stable; SIF/DMA progressed | M1 passed |
| 2026-08-04 | Initial M2 graphics bridge | `dma=514`, `gif=513`; separated IMAGE payloads reached GS adapter | M2 not passed: zero framebuffer/VRAM |
| 2026-08-05 | SIF layout and startup mappings | 56 completions; request storm removed; PC advanced | M2 not passed: payload/frame still zero |
| 2026-08-05 | VBlank interior callback | Missing-handler diagnostics removed | M2 not passed |
| 2026-08-05 | `0x80000006` result pointer | Corrected reference-backed result pointer to `0x220d0` | M2 not passed |
| 2026-08-05 | Boot WAD and SPR streaming | Authentic WAD data decompressed; `dma=556`; stalled at `0x1198b0` | Current state; M2 not passed |

## Handoff format

Every completed iteration should leave exactly one current handoff in `Active
work` and report:

- proven root cause or disproved hypothesis;
- files changed;
- exact build/test commands and logs;
- before/after state and acceptance result;
- regressions and remaining temporary debt;
- one next experiment;
- one suggested commit message, or no-commit recommendation.
