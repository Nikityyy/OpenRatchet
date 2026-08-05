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

- Ghidra identified `FUN_0011a948` as the SIF response handler and
  `FUN_0011c840` as a poll of command `0x80000003` until response `result0`
  becomes nonzero. `FUN_0011ade0` copies the response fields back into the
  waiting request.
- A fresh PCSX2 trace captured the two response states for that poll: the
  first completion returned `(result0,result1)=(0,0)`, and the next completion
  for the same request returned `(0x4f848,0x4f890)`.
- PCSX2 responses captured for requests `0x80000592` and `0x8000059a` return
  `(0x3f570, 0x3fb20)` and `(0x3f648, 0x3fc50)` respectively.
- The native override now resets this state at `0x11cf10`, ignores stale
  response-pool packets after the native callback clears their request object,
  and emits the same zero-then-nonzero `0x80000003` sequence.
- Native verification after that change: the process remained alive for 10
  seconds, the log recorded both `0x80000003` completions, and guest PC
  advanced from `0x11c860` to `0x120e08`. The next blocker is repeated request
  `0x80000593` completions with zero results; graphics counters remained
  `dma=0`, `gif=0`, `gsw=0`, `vif=2`.
- M2 is not complete: no authentic framebuffer or nonzero graphics activity
  has been demonstrated. The next concrete step is to trace the original
  `0x80000593` response and its following SIF/DMAC transaction in PCSX2 before
  adding another bridge mapping.

Continuation note (2026-08-04, commit boundary):

- PCSX2/PINE captured the verified startup response data for `0x80000593`:
  `(result0,result1)=(0x410f0,0x417c0)`. The native override now returns those
  values. The following `0x80000595` request was also captured and bridged as
  `(0x41060,0x41bd0)`.
- Ghidra showed the callback target `0x120788` copying two response blocks and
  returning to `0x1206d8`; a native guest override now implements that exact
  copy behavior. The previous missing indirect-call target diagnostic is gone.
- Before the final GS-layer edit, the native harness ran for 10 seconds with
  `dma=514`, `gif=513`, `gsw=0`, `vif=2`, and PC `0x2017a4`. The log showed
  repeated real GIF setup packets, but frame uploads still reported
  `displayFbp=0`, `sourceFbp=0`, and `preferred=0`, so M2 acceptance was not
  claimed.
- Runtime tracing proved the next boundary: setup packets start a local-to-host
  transfer, while the following raw image payload arrives as a separate DMA
  submission whose leading zero-loop word was incorrectly treated as a GIF tag.
  The root-owned `src/guest_overrides.cpp` graphics bridge now detects that
  active-transfer packet shape, prepends a valid IMAGE tag, and forwards it
  through the public GS API.
- The verified Release build passed after this fix. The post-fix harness run
  completed successfully: the process launched, stayed alive for 10.13 seconds,
  and was stopped by the harness. It reported 22 SIF completions and
  `tick=240 pc=0x2017a4 dma=514 gif=513 gsw=0 vif=2`.
- The post-fix log confirms the raw 4096-byte transfer packets no longer appear
  as zero-loop GIF packets, so the stateful image-payload branch is active.
  However, frame uploads still report `displayFbp=0`, `sourceFbp=0`, and
  `preferred=0` (`native-20260804-223936.stdout.log`). M2 therefore remains in
  progress: the next concrete investigation is the guest's GS privileged
  presentation setup (`PMODE`, `DISPFB1/2`, `DISPLAY1/2`) and its comparison to
  PCSX2, not another SIF response bridge.

Files changed in this handoff: `src/guest_overrides.cpp` and `MILESTONES.md`.
The nested `third_party/PS2Recomp` checkout is clean; no third-party file is
required for this fix.

Build and native test commands:

```powershell
.\tools\build-native.cmd -Configuration Release
.\build\native\Release\openratchet.exe .\build\extracted\PS2_MAIN.ELF
```

The tested launch used the same executable and ELF arguments through a
temporary log-capture process with duplicate `Path`/`PATH` environment keys
normalized for that child process. The 10-second test captured logs in
`build/native/native-test.stdout.log` and `build/native/native-test.stderr.log`;
the test process was still alive and was stopped after the observation window.

Latest verified build command:

```powershell
.\tools\build-native.cmd -Configuration Release
```

Build result: passed; output was `build/native/Release/openratchet.exe` and the
complete log was `build/native/build-Release.log`.

Post-fix native test:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\tools\run-native-test.ps1 -DurationSeconds 10
```

Result: launched `True`, alive at duration `True`, elapsed `10.13` seconds,
stopped by harness `True`; logs are
`build/native/test-logs/native-20260804-223936.stdout.log` and
`build/native/test-logs/native-20260804-223936.stderr.log`. M2 acceptance did
not pass because the host still used the fallback presentation path.

Continuation note (2026-08-04, privileged GS fast-path investigation):

- The native run was reproduced again after the PCSX2 DebugServer and PINE
  connections were verified. The Release build passed, and the 10-second
  harness run launched successfully, stayed alive for 10.12 seconds, and was
  stopped by the harness. It reported 22 SIF completions and
  `tick=120 pc=0x2017d8 dma=514 gif=513 gsw=0 vif=2`.
- Ghidra and the checked-in GS stub show the reset packet carries A+D writes
  for `PMODE`, `SMODE2`, `DISPFB1/2`, and `DISPLAY1/2`. The native GS decoder
  already handles the display-buffer addresses `0x59` through `0x5c`, while
  the `0x41`/`0x42` A+D IDs collide with ordinary packed-register IDs. A
  root-only bridge attempt was tested and then discarded because this run
  takes the optimized packed-GIF DMA path, which bypasses that callback.
- The post-test log still reports `displayFbp=0`, `sourceFbp=0`, and
  `preferred=0` for the uploaded frames. No `[gs:prim]` draw events were
  observed, so authentic guest-produced framebuffer content is still not
  demonstrated. M2 remains in progress.
- The next concrete step is to trace the optimized packed-GIF DMA path at the
  root-owned boundary and apply the missing privileged-register semantics
  there, then verify VRAM/frame content and draw events against the live
  PCSX2 GS state. No third-party or generated file was changed.

Root-only replacement test:

- Build and test repeated after moving the GS bridge out of the nested checkout.
- Result: launched `True`, alive at duration `True`, elapsed `10.12` seconds,
  22 SIF completions, `dma=514`, `gif=513`, `gsw=0`, `vif=2`.
- The log records `size=4112 nloop=256 flg=2` IMAGE packets after the setup
  packets, proving the root-owned bridge preserves the behavior.
- Logs: `build/native/test-logs/native-20260804-224407.stdout.log` and
  `build/native/test-logs/native-20260804-224407.stderr.log`.

Continuation note (2026-08-05, native return-state investigation):

- Verified connections before investigation: PCSX2 DebugServer and PINE both
  reported connected for Ratchet & Clank (SCUS-97199), and Ghidra was active
  on port 8193. The final PCSX2 status check later reported both channels
  disconnected; runtime-dependent work stopped at that point.
- Ghidra decompiled `FUN_00201650` and showed the original call
  `FUN_001f97e8(0x1941c0,0xffffffff80808080,0x100)`. The native generated
  path had not reached that fill because `FUN_0020b618` entered with return
  address `0x2017ec` and exited with `ctx->pc=0`, leaving the only guest
  thread at `0x2017a4`.
- Root-owned `src/guest_overrides.cpp` now repairs that specific
  `0x20b618` return state using the saved guest return-address vector. A
  Release build passed, and the native run advanced to tick 480 with 211 SIF
  completions (`dma=514`, `gif=513`, `gsw=0`, `vif=2`) instead of terminating
  the guest thread at `0x2017a4`.
- The native run still did not produce a nonzero framebuffer or primitive
  draw event. Its first presentation probe remained 512x512 with
  `nonBlackPixels=0` and `nonZeroVramBytes=0`. After the return repair, the
  guest progressed into a repeated `0x80000006` SIF wait at PC `0x11caa0`.
  The next step is to capture the original response/result-pointer behavior
  for that request in PCSX2 and bridge it with real data; do not synthesize
  the staging buffer or claim M2 acceptance.

Build and test commands:

```powershell
.\tools\build-native.cmd -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-native-test.ps1 -DurationSeconds 10
```

Latest logs: `build/native/test-logs/native-20260805-001658.stdout.log` and
`build/native/test-logs/native-20260805-001658.stderr.log`.

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
| 2026-08-04 | M2 in progress | Root-only bridge test alive 10.12s; PC `0x2017a4`; `dma=514`, `gif=513`, `gsw=0`, `vif=2`; 22 SIF completions; IMAGE packets observed | PCSX2/PINE: `0x80000593 -> (0x410f0,0x417c0)` and `0x80000595 -> (0x41060,0x41bd0)`; Ghidra confirms callback `0x120788` copy behavior | Uploads remain `displayFbp=0`, `sourceFbp=0`, `preferred=0`; no authentic framebuffer acceptance |

## Blockers

- None for M1. M2 remains in progress. Next concrete action: inspect the guest's
  GS privileged presentation setup (`PMODE`, `DISPFB1/2`, `DISPLAY1/2`) against
  PCSX2 and capture a non-fallback framebuffer with nonzero display/source
  evidence. If that passes, update M2 acceptance; otherwise continue from the
  GS transfer trace.

## New-chat handoff

Start in `C:\Users\berge\Downloads\OpenRatchet` with the current uncommitted
changes intact. Do not reset or reconfigure. First run:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\tools\run-native-test.ps1 -DurationSeconds 10
```

Then inspect the newest files under `build\native\test-logs`; the completed
diagnostic run was `native-20260805-004924`. Acceptance still requires
authentic guest-produced output, not merely a surviving process, nonzero
DMA/GIF counters, or the magenta fallback. Suggested commit message for the
root changes: `fix: correct startup SIF response layout and commands`.

The nested `third_party/PS2Recomp` checkout is clean; do not edit or reset it.

## Latest M2 continuation (2026-08-05)

- Verified before relying on runtime evidence: PCSX2 DebugServer and PINE both
  reported connected for Ratchet & Clank (SCUS-97199), and Ghidra was active
  on port 8193.
- Ghidra `FUN_0011ade0` and the live request state at `0x158400` established
  the SIF response layout: packet `+0x24` is the completion field, while
  `+0x28` and `+0x2c` are result0/result1. The root bridge had been writing
  result0 into the completion field. `src/guest_overrides.cpp` now emits the
  correct layout and keeps the initial `0x80000003` completion pending.
- PCSX2-backed mappings were added for `0x80000006 -> 0x2020d0`,
  `0x80000900 -> 0x60f38`, `0x8000091b -> 0x61338`,
  `0x80000400 -> 0x5ad00`, `0x123456 -> 0x56500`, and
  `0x123457 -> 0xd6e30`. Ghidra confirmed `0x123456` and `0x123457` are
  real commands from `FUN_0012da28`, not malformed native requests.
- The Release build passed. The final diagnostic launched the native process,
  kept it alive for 10.11 seconds, and stopped it cleanly. It recorded 56 SIF
  completions and `tick=120 pc=0x1198b0 dma=514 gif=513 gsw=0 vif=2`.
- The request storm is gone and the trace reaches the post-`0x123457` DMA
  copy, but M2 acceptance still fails: frame uploads remain
  `displayFbp=0`, `sourceFbp=0`, `preferred=0`, and the presentation probe
  reports zero non-black pixels and zero nonzero VRAM bytes. No authentic
  framebuffer or primitive draw event was demonstrated.

Files changed in this continuation: `src/guest_overrides.cpp` and
`MILESTONES.md`. The nested `third_party/PS2Recomp` checkout and generated
output remain unchanged.

Build and diagnostic commands:

```powershell
.\tools\build-native.cmd -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\tools\diagnose-native.ps1 -DurationSeconds 10
```

Build result: passed; executable `build/native/Release/openratchet.exe` and
build log `build/native/build-Release.log`. Diagnostic logs:
`build/native/test-logs/native-20260805-004924.stdout.log` and
`build/native/test-logs/native-20260805-004924.stderr.log`.

The next concrete M2 action is to trace the native path after the verified
post-`0x123457` DMA copy and the optimized packed-GIF transfer, then fix the
missing guest-produced GS/VRAM state using a matching PCSX2 trace. Do not claim
M2 until display/source addresses, nonzero VRAM, and a non-fallback frame are
observed.

Continuation note (2026-08-05, VBlank interior-handler fix):

- The refreshed native run showed a persistent 30-second stall with repeated
  `[INTC:missing] cause=2 handler=0x12f1c8 id=1` diagnostics. Ghidra and the
  generated `sub_0012F1A0` show that `0x12f1c8` is the original VBlank-start
  callback's interior entry, not an invalid handler; the generated function
  table only exposed the containing function's `0x12f1a0` entry.
- Root-owned `src/guest_overrides.cpp` now implements the instructions at that
  interior entry: it advances the original VBlank counter at `0x15ed48`,
  combines the field value from `0x1000800` with the base at `0x15ed40`, stores
  the result at `gp-0x7eb0`, and returns through the interrupt context's zero
  return address. The alias is registered at `0x12f1c8`; generated output and
  `third_party/PS2Recomp` remain unchanged.
- The Release build passed. The post-fix 10-second diagnostic launched and
  stayed alive for 10.11 seconds, with 56 SIF completions and
  `tick=120 pc=0x1198b0 dma=514 gif=513 gsw=0 vif=2`. The prior VBlank missing
  handler diagnostics are gone.
- M2 remains incomplete: the same run still reports
  `displayFbp=0`, `sourceFbp=0`, `preferred=0`, and the presentation probe has
  zero non-black pixels and zero nonzero VRAM bytes. The raw image payloads are
  still zero-filled. PCSX2 comparison confirmed the original staging buffer at
  `0x1941c0` contains the expected `0x80` fill; the native trace only shows an
  earlier zero-fill and does not yet reach the later `FUN_00201650` staging
  fill. The next concrete action is to trace the guest path after the repaired
  `0x20b618` return and the remaining SIF/DMA data population, then verify the
  corresponding staging bytes and GS VRAM contents against PCSX2.

Build and test commands for this continuation:

```powershell
.\tools\build-native.cmd -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\diagnose-native.ps1 -DurationSeconds 10
```

Build log: `build/native/build-Release.log`. Post-fix logs:
`build/native/test-logs/native-20260805-010548.stdout.log` and
`build/native/test-logs/native-20260805-010548.stderr.log`.

Continuation note (2026-08-05, `0x80000006` result-pointer correction):

- PCSX2 DebugServer and PINE were verified connected for Ratchet & Clank
  (SCUS-97199), and Ghidra was verified active on port 8193 before tracing.
- Ghidra `FUN_0011ade0` confirms that the SIF response field at packet `+0x28`
  is copied into the request result slot at `+0x14`. PCSX2's live request at
  `0x158400` contains command `0x6` and result0 `0x220d0`; `0x2020d0` is code
  in the original ELF, not the matching result pointer.
- Root-owned `src/guest_overrides.cpp` now bridges request `0x80000006` with
  `result0=0x220d0`. The Release build passed, and the post-fix diagnostic
  launched, stayed alive for 10.12 seconds, and was stopped by the harness.
  It recorded 56 SIF completions and `tick=120 pc=0x1198b0 dma=514 gif=513
  gsw=0 vif=2`; the log records the corrected completion.
- M2 remains incomplete. The run still reports `displayFbp=0`, `sourceFbp=0`,
  `preferred=0`, zero non-black pixels, zero nonzero VRAM bytes, and zero-filled
  image payloads. The next concrete action is to trace the native guest path
  after the repaired `0x20b618` return and the `0x80000006` completion, then
  capture the corresponding `FUN_00201650` staging fill and GS VRAM updates.

Build and test commands for this continuation:

```powershell
.\tools\build-native.cmd -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\diagnose-native.ps1 -DurationSeconds 10
```

Build log: `build/native/build-Release.log`. Post-fix logs:
`build/native/test-logs/native-20260805-011651.stdout.log` and
`build/native/test-logs/native-20260805-011651.stderr.log`.

Continuation note (2026-08-05, boot WAD and SPR streaming):

- Static/reference investigation identified two missing native behaviors in
  the startup resource path. Ghidra `FUN_0020b618` submits a 0x200-QWC SPR
  transfer from `a0+0x10` and then streams additional 0x2000-byte windows;
  the native MMIO layer left the SPR CHCR start bit set and never populated
  scratchpad. PCSX2 memory at `0x1fa7000` matched the repository asset
  `build/extracted/wads2/wad2_0.wad` byte-for-byte at the WAD header/payload
  prefix.
- Root-owned `src/guest_overrides.cpp` now loads the extracted boot WAD at
  the guest `0x12f208` startup load boundary, refills the scratchpad from the
  guest SPR register state, and resumes the generated decompressor across its
  known SPR wait/interior PCs. No generated or third-party file was changed.
- The final Release build passed. The 10-second diagnostic launched and stayed
  alive for 10.11 seconds before the harness stopped it cleanly. It recorded
  56 SIF completions and `tick=120 pc=0x1198b0 dma=556 gif=513 gsw=0 vif=2`.
  The log shows two authentic WAD-backed SPR windows (`sourceOffset=0x0` and
  `0x2000`), decompressor return `pc=0x2017ec`, nonzero output at `0x500000`,
  and nonzero SIF payload descriptors such as `src=0x552640 size=0x1999`.
- M2 acceptance still does not pass. The run remains stalled at
  `0x1198b0`; frame uploads still report `displayFbp=0`, `sourceFbp=0`, and
  `preferred=0`, while the presentation probe remains zero non-black pixels
  and zero nonzero VRAM bytes. The earlier image-payload diagnostics remain
  zero because the GS staging/presentation path has not yet produced an
  authentic framebuffer.
- PCSX2 cleanup was completed after tracing: DebugServer and PINE remain
  connected for Ratchet & Clank (SCUS-97199), Ghidra port 8193 is idle, and no
  breakpoints or watchpoints remain.

Build and test commands for this continuation:

```powershell
.\tools\build-native.cmd -Configuration Release
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\diagnose-native.ps1 -DurationSeconds 10
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\run-native-test.ps1 -DurationSeconds 30
```

Build log: `build/native/build-Release.log`. Final diagnostic logs:
`build/native/test-logs/native-20260805-015843.stdout.log` and
`build/native/test-logs/native-20260805-015843.stderr.log`. The 30-second
stability run used the same executable and logged
`native-20260805-015716.stdout.log` and
`native-20260805-015716.stderr.log`; it stayed alive but made no progress
past `tick=120`.

The next concrete M2 action is to trace the post-decompression
`0x80000006`/staging path and the first subsequent GS upload, then replace the
startup-specific WAD fallback with the matching general CDVD/IOP data path.
Do not claim M2 until display/source addresses, nonzero VRAM, and a
non-fallback frame are observed.
