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
- SIF transport now records successful client-to-service bindings and routes
  data-bearing calls only to a service provider with a real payload.
- PCSX2 IOP handler `0x3b094` proved CDVD init service `0x80000592`, function
  `0`, returns `{1, 0x21d, 0x21d, 0}`; generated `FUN_00120eb0` proved Ratchet
  consumes the two module-version fields and requires major version 2 or newer.
- Verification run `native-20260805-141747` copied that 16-byte payload to the
  guest receive buffer, emitted the size-derived `0x1040` response descriptor,
  completed the previously deferred packet, and advanced to the next RPC bind
  and call while remaining alive for 10.18 seconds.
- Prior stable graphics baseline remains 56 SIF completions, `dma=556`,
  `gif=513`, `gsw=0`, `vif=2`; the bounded run reached the next startup RPC
  before graphics and therefore did not re-observe those counters.
- Presentation still fails: the raw runtime tick contains
  `dispfb1=0x1400` and `display1=0x1bf27f00000000`, but the host presentation
  selection still reports `displayFbp=0` and `sourceFbp=0`; VRAM has no
  nonzero bytes, the copied frame has no non-black pixels, and no authentic
  primitive draw event was observed.
- The single characterization boot reached the reference game's asset/graphics
  initialization (`FUN_001e9658` -> `FUN_001eb798`) after startup SIF; the
  armed function-`0x16` and function-`0x01` callsites did not occur on that
  path. All temporary breakpoints were cleared.
- Verification run `native-20260805-155924` completed 22 SIF packets,
  including service `0x80000593`, function `0x04`, with a four-byte zero
  payload. It advanced past that former wait into an uncaptured data-bearing
  call: client `0x158040`, function `0x01`, receive `0x158080`, size `4`,
  `status=5`, sequence `0x14`; it remained alive for 10.14 seconds.
- Generated/Ghidra mapping proved client `0x158040` binds to service
  `0x80000003`; PCSX2 function `0x01` consumed request word `0x1999` and
  returned `0x53300`, while function `0x02` consumed `0x53300` and returned
  `0`. Both emitted four-byte `0x80000008` responses.
- Verification run `native-20260805-162020` completed 24 SIF packets. The
  transport retained outbound DMA payloads by remote buffer and completed the
  request-sensitive function-`0x01` shape before advancing to an uncaptured
  call: client `0x158400`, function `0xff`, receive `0x158200`, size `4`,
  `status=5`, sequence `0x16`; it remained alive for 10.17 seconds.
- Tooling verification run `native-20260805-165344` remained alive for 10.20
  seconds with 24 SIF completions and emitted one structured record for the
  active divergence: service `0x80000006`, function `0xff`, no send buffer or
  payload, receive `0x158200`, size `4`, `status=5`, sequence `0x16`, reason
  `unsupported-shape`. The compact diagnostic parses this record directly.
- `tools\pcsx2_sif_capture.py` is live-verified against DebugServer: it armed a
  conditional breakpoint, blocked to the hit, captured registers, an evaluated
  address and a contiguous memory window in one JSON transcript, then removed
  its breakpoint while preserving unrelated state. Capture transcripts under
  `build\reference-captures` are ignored by Git.
- The `pcsx2-reset` MCP performed a real reset to `0xbfc00000` and its blocking
  continue stopped at the expected conditional EE breakpoint `0x118cb0`.
  After the EE/IOP deduplication fix, `preserve_breakpoints=true` rearmed that
  single breakpoint exactly once with its condition and description intact.
- Latest verified logs:
  `build/native/test-logs/native-20260805-162020.stdout.log` and
  `build/native/test-logs/native-20260805-162020.stderr.log`.
- The compact diagnostic now recognizes the direct SIF records: the latest run
  reported 15 completions and the exact next deferred call instead of zero.
- The upgraded GhidraMCP handshake is verified through its stdio bridge and
  plugin at `127.0.0.1:8089`: project `OpenRatchetTest` exposes
  `/PS2_MAIN.ELF`; metadata resolves to this repository's extracted ELF as
  little-endian `MIPS-R5900`; and focused decompilation at `0x121630` succeeds.
- The last PCSX2 PINE/DebugServer reference capture was connected to Ratchet &
  Clank; establish one fresh handshake before the next live batch and reuse it.
  At `0x11a948`, response packet `0x20154d80` carried a data-bearing CALL
  for packet `0x20155000`: `status=0x5`, request `1`, receive `0x15afc0`,
  size `4`, sequence `0x117ddd`. After the handler reached `0x11aa54`, the
  ring header changed from `0x440` to `0x400` as the `0x40`-byte message was
  consumed; packet status became `0x4`, sequence became `0`, and the client
  first word was cleared.
- Fresh PCSX2 PINE/DebugServer capture at generated `0x121304` proved the
  CDVD DiskReady call: service `0x8000059a`, function `0`, client `0x159990`,
  receive `0x1324c0`, size `4`. Stepping over it changed receive word
  `1→2`, emitted the `0x80000008` response at `0x20154d80`, and changed the
  request packet from bind `0x80000009` to call `0x8000000a`.
- Verification run `native-20260805-144242` wrote the matching four-byte
  DiskReady completion, advanced through the next bind (`0x80000593`), and
  deferred only its subsequent unsupported call. The compact parser reported
  zero SIF completions despite these direct runtime log records.
- Fresh PCSX2 capture at generated `0x1213f8` proved service `0x80000593`,
  function `0x22`, client `0x132d08`, receive `0x1324c0`, size `4`: receive
  word changed `2→1`, client sequence `8→9`, and the response ring emitted
  `0x80000008`. Native run `native-20260805-145512` completed that call and
  advanced to the next bound service while remaining alive for 10.18 seconds.
- Fresh PCSX2 capture at generated `0x120be4` proved service `0x80000595`,
  function `0x0e`, client `0x132490`, receive `0x131340`, size `4`: receive
  word changed `0→2`, client sequence `0x0a→0x0b`, and the response ring
  emitted `0x80000008`. Native run `native-20260805-150454` completed that
  call, then deferred only service `0x80000593`, function `0x04`.

### Active divergence

Service `0x80000003`, functions `0x01` and `0x02`, now complete only when
their captured outbound words match the reference chain. The next native packet
remains authentically busy at packet `0x20155000`: client `0x158400`, bound
service `0x80000006`, function `0xff`, no send buffer/payload, receive
`0x158200`, size `4`, status `5`, sequence `0x16`. Its reference response has
not yet been characterized.

### Next experiment

Map client `0x158400` through generated output and its binding/callsite, plus
all immediately forward-reachable SIF calls. Build one capture manifest from
`tools\sif-capture.example.json`, verify both handshakes, reset with
`preserve_breakpoints=true` after using the helper's `--arm-only` mode, then run
its `--capture-only` mode once. Capture each call's service/function,
send/receive data, response-ring transition, packet status, and client sequence
before adding any service-table behavior. If ownership leaves SIF, stop and
hand off that owning subsystem.

Iteration acceptance delta: native must complete only the newly reference-
verified call shape and advance from the current `status=5` packet without
synthetically completing unsupported calls. M1 remains passed; M2 remains
unpassed because guest VRAM/frame presentation is still absent.

### Known temporary debt

These bridges enabled investigation but are not the desired full-port
architecture:

- `guest_11a948` still scans fixed SIF pools and supplies startup compatibility
  responses. It now uses stateful binding and service payload dispatch, but
  packet discovery must move to the SIF transfer boundary.
- The declarative SIF compatibility table contains only seven verified service
  behaviors: CDVD init versions `0x21d/0x21d`, DiskReady function `0` result
  `2`, service `0x80000593` functions `0x22` result `1` and `0x04` result `0`,
  service `0x80000595` function `0x0e` result `2`, and service `0x80000003`
  functions `0x01` (`0x1999 -> 0x53300`) and `0x02` (`0x53300 -> 0`). The last
  two require the captured EE-to-IOP DMA word associated with the packet's
  remote send buffer. Unsupported shapes remain pending; replace the table when
  native IOP execution owns these responses.
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

Validate and run a prepared multi-breakpoint SIF capture manifest:

```powershell
python .\tools\pcsx2_sif_capture.py <manifest.json> --validate-only
python .\tools\pcsx2_sif_capture.py <manifest.json> --arm-only
# Call pcsx2_system_reset(preserve_breakpoints=true), then:
python .\tools\pcsx2_sif_capture.py <manifest.json> --capture-only `
    --output .\build\reference-captures\capture.json
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
| 2026-08-05 | Stateful CDVD init RPC payload | IOP handler proved `{1,0x21d,0x21d,0}`; unit tests passed; native copied 16 bytes, emitted `0x1040`, and advanced to DiskReady | Init delta passed; M2 not passed |
| 2026-08-05 | DiskReady reference recapture | PINE/DebugServer handshake passed; post-boot IOP breakpoint `0x3f648` did not hit and `0x41378` was zero | Evidence handoff; exact payload still required |
| 2026-08-05 | Stateful CDVD DiskReady RPC payload | PCSX2 at `0x121304` proved function `0` writes `{2}` to `0x1324c0`; native completed the call and reached bound service `0x80000593` | DiskReady delta passed; M2 not passed |
| 2026-08-05 | Stateful service `0x80000593` RPC payload | PCSX2 at `0x1213f8` proved function `0x22` writes `{1}` to `0x1324c0`; native completed it and reached bound service `0x80000595` | Delta passed; M2 not passed |
| 2026-08-05 | Stateful service `0x80000595` RPC payload | PCSX2 at `0x120be4` proved function `0x0e` writes `{2}` to `0x131340`; native completed it and reached service `0x80000593`, function `0x04` | Delta passed; M2 not passed |
| 2026-08-05 | Batched SIF/PINE workflow | Four verified responses consolidated into a declarative table; diagnostics reported 15 completions and exact deferred function `0x04`; focused tests and 10s run passed | Workflow delta passed; M2 not passed |
| 2026-08-05 | GhidraMCP fork migration | Stdio bridge connected to `OpenRatchetTest`; metadata verified the extracted R5900 ELF and focused decompilation at `0x121630` | Static-reference tooling verified; M2 unchanged |
| 2026-08-05 | Startup SIF characterization and function `0x04` | One PINE/DebugServer boot proved service `0x80000593`, function `0x04` writes `{0}` to `0x1324c0`; native completed it, reached 22 completions, and deferred the new client `0x158040` shape | Iteration delta passed; M2 not passed |
| 2026-08-05 | Request-sensitive SIF service `0x80000003` | PCSX2 proved `0x1999 -> 0x53300` for function `1` and `0x53300 -> 0` for function `2`; native matched the DMA-captured request and advanced to client `0x158400` function `0xff` | Iteration delta passed; M2 not passed |
| 2026-08-05 | Batched capture and structured RPC diagnostics | Live DebugServer smoke produced and cleaned an ordered JSON capture; native run `165344` exposed service `0x80000006` and the complete deferred request in one record; build and 3 tests passed | Tooling delta passed; M2 unchanged |

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
