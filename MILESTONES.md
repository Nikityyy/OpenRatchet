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
- Before this iteration, the stable run reached guest PC `0x1198b0` after a synthetic
  data-bearing SIF/RPC completion incorrectly cleared the guest packet busy
  state; the bounded fix now defers that completion instead.
- Verification run `native-20260805-131535` stayed alive for 10.14 seconds,
  completed the startup bind, then logged an earlier data-bearing CALL at
  packet `0x20155000` with receive `0x1324c0`, size `0x10`, status `0x5`, and
  `busy=1`; no `0x1198b0` error was emitted, but the target PC was not captured.
- Prior stable graphics baseline: 56 SIF completions, `dma=556`, `gif=513`,
  `gsw=0`, `vif=2`; the bounded verification run captured no runtime tick or
  graphics activity before the authentic pending call.
- Presentation still fails: the raw runtime tick contains
  `dispfb1=0x1400` and `display1=0x1bf27f00000000`, but the host presentation
  selection still reports `displayFbp=0` and `sourceFbp=0`; VRAM has no
  nonzero bytes, the copied frame has no non-black pixels, and no authentic
  primitive draw event was observed.
- Latest verified logs:
  `build/native/test-logs/native-20260805-131535.stdout.log` and
  `build/native/test-logs/native-20260805-131535.stderr.log`.
- PCSX2 PINE/DebugServer reference capture is connected to Ratchet & Clank.
  At `0x11a948`, response packet `0x20154d80` carried a data-bearing CALL
  for packet `0x20155000`: `status=0x5`, request `1`, receive `0x15afc0`,
  size `4`, sequence `0x117ddd`. After the handler reached `0x11aa54`, the
  ring header changed from `0x440` to `0x400` as the `0x40`-byte message was
  consumed; packet status became `0x4`, sequence became `0`, and the client
  first word was cleared.

### Active divergence

Native leaves SIF/RPC call packets (`0x8000000a`) pending when a nonzero
receive payload is requested and no payload provider exists. This preserves the
PCSX2 busy transition, but native has no reusable IOP provider that can produce
the response payload and `0x80000008` ring message. The first deferred native
call is earlier than the captured reference call (`receive=0x1324c0`,
`size=0x10`, request `0`), so it remains pending.

### Next experiment

Add a reusable IOP/SIF payload provider at the SIF transfer boundary for the
first deferred data-bearing call. It must emit the response payload and
`0x80000008` ring descriptor only when a real service result exists, then
compare ring-head advance, packet status/sequence, client clear, and receive
bytes against the captured PCSX2 transition.

Iteration acceptance delta:

- native leaves `0x1198b0` through the authentic path;
- the affected data-bearing call state matches the PCSX2 capture; and
- no existing M1 or graphics-transfer progress regresses.

This iteration proves the reference packet-clear transition and confirms that
the native no-payload deferral is the correct safety behavior, but it does not
capture the target PC or graphics-transfer non-regression. Treat this as an
evidence handoff, not a complete acceptance pass; M2 remains unpassed.

This iteration does not need to complete M2. If it exposes a different
subsystem blocker, record that as the single next experiment and stop.

### Known temporary debt

These bridges enabled investigation but are not the desired full-port
architecture:

- `guest_11a948` scans fixed SIF pools and supplies startup compatibility
  responses; it now defers data-bearing calls without an IOP payload. Replace
  it with stateful SIF/RPC transport carrying real data.
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
| 2026-08-05 | SIF data-bearing completion deferral | 10.14s alive; startup bind completed; earlier CALL packet remained `status=0x5`, `busy=1`; target PC/graphics not captured | Evidence handoff; M2 not passed |
| 2026-08-05 | PCSX2 SIF response-ring capture | `0x11a948` consumed a `0x40`-byte `0x80000008` response; packet status `0x5→0x4`, sequence `0x117ddd→0`, client word cleared | Evidence handoff; M2 not passed |

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
