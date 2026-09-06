# OpenRatchet Milestones & Roadmap

This file is the concise source of truth for OpenRatchet development. Historical PS2Runtime/GS-first emulation approaches remain in Git history; the project follows an OpenGOAL-style semantic PC port architecture. PS2Recomp serves only as an automatic fallback for untouched EE game logic while platform subsystems, asset pipelines, and renderers are progressively replaced by native PC implementations.

Status values: `DONE`, `IN PROGRESS`, `TODO`.

---

## Complete Project Roadmap (Phases 0 – 25)

| Phase | Status | Goal | Acceptance / Visual Milestone |
|---|---|---|---|
| Phase 0 | `DONE` | Full audit & baseline | Frozen reproducible PS2Recomp baseline, verified execution logs. |
| Phase 1 | `DONE` | Native ownership boundary | OpenRatchet owns application lifecycle; PS2Runtime demoted to fallback backend. |
| Phase 2 | `DONE` | Native VFS & storage bypass | Native disc TOC & WAD indexing replace sector-specific CDVD/SIF startup injection. |
| Phase 3 | `DONE` | Native WAD decompressor gate | Host decompressor validated byte-identical across 249/249 authentic WAD streams. |
| Phase 4 | `DONE` | Authoritative native WAD pipeline | Decompression replaced natively; SPR/DMAC scratchpad emulation bridge removed. |
| Phase 5 | `DONE` | Native R&C1 level model | Extraction and parsing of the authentic 0x2434 amalgamated level containers. |
| Phase 6 | `DONE` | First authentic native visual | Recognizable 3D collision geometry rendered natively via PC GPU (OpenGL/Raylib). |
| Phase 7 | `DONE` | Textured tfrag terrain | Actual level terrain rendered with native RGBA8 textures, GS palettes, and VIF unpacking. |
| Phase 8 | `DONE` | Native sky, ties & shrubs | Full static environment: tie structures, shrub vegetation, camera-relative sky shells. |
| Phase 9 | `DONE` | Native mobys & instance accounting | Ratchet, crates, NPCs, enemies rendered in bind pose; strict 296/296 accounting. |
| Phase 10 | `DONE` | Skeletal & model animation | Native pose decoding/skinning validated for all Level-0 skeletal frames; Ratchet visibly animates from its external retail sequence bank with retail loop interpolation. |
| Phase 11 | `IN PROGRESS` | Live game-state & camera bridge | Native renderer continuously follows running recompiled game logic and gameplay camera. |
| Phase 12 | `TODO` | Native input & playable Ratchet | PC controller/keyboard input drives original simulation; playable Ratchet. |
| Phase 13 | `TODO` | Native 2D / UI renderer | Native font, text, fades, sprites, and screen-space overlays. |
| Phase 14 | `TODO` | Frontend & main menu | Boot sequence, logos, title screen, main menu, New Game / Load Game. |
| Phase 15 | `TODO` | Gameplay UI & menus | HUD, weapon wheel, pause menu, vendor screens, dialogs, subtitles. |
| Phase 16 | `TODO` | In-engine cutscene rendering | Native rendering of in-engine scripted cinematic sequences. |
| Phase 17 | `TODO` | Native video playback | Insomniac / Sony pre-rendered movies and sequence playback. |
| Phase 18 | `TODO` | Native audio pipeline | Music, streaming sound, SFX, and dialogue via native host audio backends. |
| Phase 19 | `TODO` | Native save system | PS2 memory-card logic mapped cleanly to transparent PC save files. |
| Phase 20 | `TODO` | Level transitions & planet travel | Seamless inter-planet flight and level streaming without simulated optical disc IO. |
| Phase 21 | `TODO` | Rendering completeness | Water, particles, transparency, special material shaders, lighting, post-processing. |
| Phase 22 | `TODO` | Gameplay completeness audit | Verification of every weapon, gadget, enemy, boss, and mission interaction. |
| Phase 23 | `TODO` | Remove remaining PS2 dependencies | Eliminate obsolete SIF/IOP/GS/emulation runtime baggage completely. |
| Phase 24 | `TODO` | PC features & polish | Arbitrary resolutions, ultrawide, unlocked frame rates, remapping, settings. |
| Phase 25 | `TODO` | Full-game regression | Zero-failure playthrough from New Game through final boss to end credits. |

---

## Phase 10 — Skeletal & Model Animation

### Progress & Validated Steps

- **Step 1 (`DONE`):** Verified class animation headers, sequence pointer tables, and vertex-table stream layout.
- **Step 2 (`DONE`):** Validated packed 16-byte skinning program (2-way, 3-way, main records) and VU0 4-qword alignment.
- **Step 3 & 4 (`DONE`):** Decoded rig layouts (0x40 skeleton, 0x10 common transforms) and sequence frame pointer tables.
- **Step 5 (`DONE`):** Probed authentic frame payloads across variable frame strides (0x40 to 0x2A0).
- **Step 6 (`DONE`):** Implemented dense pose evaluator (signed s16 quaternions, common-transform translations, scratchpad parent hierarchy).
- **Step 7 (`DONE`):** Validated direct 0x40 pose-joint palette mapping (disproved synthetic inverse-bind formula).
- **Step 8 (`DONE`):** Executed native CPU skinning program over 105,495 vertices with persistent cross-packet VU0 matrix registers.
- **Step 9 & Fix (`DONE`):** Rendered first visually animated moby (`oClass 530`) with authentic retail post-compose: `renderMatrix = poseMatrix × class+0x14`.
- **Step 10 (`DONE`):** Implemented authentic pose-space shortest-hemisphere quaternion NLERP interpolation.
- **Step 11A (`DONE`):** Implemented sparse translation override codec (`stream 2`), successfully decoding and skinning all 847/847 Level 0 skeletal frames (`stream1Active = 0`).

### Current Step: Step 12 — Ratchet Gameplay Animation Bank

- **Retail correction (proved):** `moby +0x52 == 0xFF` is a generic animation-transition/cache-pose state, not a dedicated Ratchet sequence format. `FUN_00212f90` obtains one of 16 transition slots, asks `FUN_0020ede8` to evaluate the current pose into that slot, and `sub_0020C880` later resolves the cached frame at `0x1AABC0 + slot*0x800`. Do not use this state as the player-bank discriminator.
- **Retail location (proved):** LevelCoreHeader `+0x78` is `ratchetSequenceTableOffset`. On Level 0 it is `0x7850` into the separate core-index blob. The table contains exactly 134 strictly increasing absolute core pointers, matching `oClass 0`'s `sequenceCount = 134`. The older `+0x74 = 0x70A550` value is preserved as neutral `coreHeader74`; its semantic role is not assigned without a retail consumer proof. The old unverified `sceneViewSize` label for `+0x7c` is likewise retired in favour of neutral `coreHeader7c`.
- **Codec contract (proved on supplied Level-0 data):** Ratchet's external sequence headers use the same pose/frame codec already implemented for ordinary mobys, with one addressing difference: the 134 sequence-table entries are core-absolute, and each sequence's frame pointers are sequence-relative. All 2,558 Ratchet frames satisfy the Step-11A sparse layout; all 25,569 stream-1 records take the retail inactive/skip branch (`stream1Active = 0`).
- **Step 12A (`DONE`):** Native external-table decoder plus strict whole-bank pose/skinning gate. Windows Release/CTest/viewer acceptance confirms 134/134 sequences, 2,558/2,558 decoded poses and 2,558/2,558 native skin executions, with the mandatory 20-second runtime regression unchanged from baseline.
- **Step 12B.1 (`DONE`):** Windows visual acceptance confirmed Ratchet itself deforms coherently from Level-0 external sequence 0 (10 frames, 4,026 skin vertices, 6,856 visible triangles) with correct topology/textures and no mesh explosion or scale/placement regression.
- **Step 12B.2 (`DONE`):** Retail end-of-sequence semantics are proved from `FUN_0020d580` (`0x20D658..0x20D6C4`): on same-sequence forward playback retail promotes frame B to frame A, increments frame B, wraps frame B to zero at `frameCount`, installs the wrapped next-frame pointer, and retains the fractional animation time. The viewer validates and interpolates all 10/10 sequence segments including frame 9 -> 0. Windows visual acceptance confirmed the loop discontinuity is gone.
- **Step 13 (`DONE`):** Final Phase-10 regression/cleanup gate passed: Release build succeeds, CTest is 14/14, every native viewer accounting/animation gate remains `status=ok`, `git diff --check` reports no whitespace errors (only expected Git LF/CRLF conversion warnings), and the mandatory 20-second runtime regression remains at the established baseline (`Alive at duration=True`, graphics activity observed, 52 SIF completions, harness exit code 0).

### Phase 10 Final Acceptance

- Ordinary Level-0 skeletal frames: **847/847 decoded and 847/847 native skin executions**.
- Ratchet external animation bank: **134/134 sequences, 2,558/2,558 decoded poses, 2,558/2,558 native skin executions**.
- Ratchet visual gate: external sequence 0, 10 frames, 4,026 skinned vertices, 6,856 visible triangles, **10/10** interpolation segments including retail frame 9 -> 0 wrap.
- No mesh explosion, scale/placement regression, UV/texture regression, or loop pop in Windows visual acceptance.
- Phase 11 is the next active milestone: replace viewer-selected animation/camera state with live state from the running recompiled game.

---

## Active Work: Phase 11 — Live Game-State & Camera Bridge

Phase 10 deliberately drove the native renderer from viewer-owned demo state.
Phase 11 replaces that microscope state with read-only snapshots of the running
retail simulation before any native input or gameplay ownership is introduced.

### Step 11.1 — Retail Live-Moby State Contract (`DONE`)

- **Pool ownership (proved):** `sub_001E9B10` allocates exactly `0x4000` bytes
  for the live Moby arena, stores its base pointer at guest global `0x15FF18`,
  and stores `base + 0x3F00` at `0x15FF20`. Retail Moby records are exactly
  `0x100` bytes, giving a hard capacity of 64 live slots.
- **Traversal contract (proved):** `FUN_0020D868` begins at the `0x15FF18`
  base, advances by `0x100`, reads signed byte `moby+0x20`, processes values
  `>= 0`, skips negative values other than `-1`, and treats exactly `-1` as
  the end-of-pool sentinel. OpenRatchet therefore follows the retail sentinel
  instead of blindly scanning 64 records.
- **Identity fields (proved):** `FUN_0020C5F0` writes the resolved class pointer
  at `+0x24`, the requested `oClass` at `+0xA6`, and `(moby-poolBase)>>8` at
  `+0xAC`.
- **Animation fields (proved):** `FUN_00212ED8` establishes sequence A/B at
  `+0x52/+0x53`, frame A/B at `+0x50/+0x51`, frame pointers at `+0x68/+0x6C`,
  and `FUN_0020D580` consumes/advances those fields with interpolation state at
  `+0x54`. `+0x70` is preserved as raw animation flags until individual bits
  receive their own consumer proof.
- **Native contract implementation:** `src/game/rac1_live_state.*` decodes only
  these proved fields from a read-only guest-RDRAM span, validates the retail
  arena extent/last-slot pointer and requires the `-1` traversal terminator.
  No camera or transform semantics are inferred in this step.
- **Windows acceptance:** Release build, 15/15 CTest, the complete Phase-10
  native viewer regression, and the mandatory 20-second fallback-runtime gate
  all pass without changing the established SIF/runtime baseline.
- **Boundary correction:** the camera is kept as an independent later oracle.
  The initial Phase-11 sketch grouped camera discovery into Step 11.1, but the
  retail Moby pool has a self-contained ownership contract while the camera has
  separate producers/consumers. They must not be conflated merely to reduce the
  number of steps.

### Step 11.2 — Live PS2Runtime RDRAM Attachment (`DONE`)

- `OpenRatchetRuntime` now attaches the proved decoder directly to
  `PS2Runtime::memory().getRDRAM()`; no level-file fallback or synthetic state is
  permitted.
- The fallback game thread writes RDRAM concurrently, so each host snapshot is
  taken under `PS2Runtime::GuestExecutionScope`. This uses the runtime's existing
  function-boundary handoff, making the read coherent and avoiding an unsynchronised
  C++ host/guest data race. The lock is released before any logging.
- The current PS2Runtime presentation callback is used only as a temporary
  sampling clock while PS2Runtime remains the EE fallback executor. Sampling is
  immediate and then once per 60 host presentations; unchanged snapshots are not
  re-logged. This callback does not own game semantics or rendering.
- Runtime output is strict and source-labelled. Before retail allocates the
  arena the legitimate state is `status=pool-not-initialized`; once present, the
  log reports the real pool/last pointers, retail terminator slot, traversed and
  skipped counts, Ratchet candidates, and `unaccounted=0` when accounting is
  complete.
  `tools/diagnose-native.ps1` surfaces these records in a dedicated
  `Live Moby state` section so the 20-second runtime gate captures the bridge
  without manual log-file inspection.
- Step 11.2 changes observation only. Native animation selection, world
  transforms and camera state remain untouched until their later Phase-11 gates.
- **Windows acceptance:** Release build and all 15/15 CTests pass, the complete
  Phase-10 native viewer regression is unchanged, and the 20-second runtime gate
  reports `[OpenRatchet:live:moby] source=guest-rdram capacity=64
  status=pool-not-initialized` while preserving the established 52-completion
  SIF baseline. This proves the runtime attachment itself before solving the
  startup service that currently prevents retail from allocating the arena.

### Step 11.2B — Retail DBCMAN Startup Semantics (`DONE`)

- Boot WAD2/0 descriptor 17 is the exact retail `Dbc_Manager`
  (`PsIIdbcman  2500`) IOP ELF. Static analysis proved service `0x80000900`,
  `SetWorkAddr` (`0x80000904`), the initial 0x80-byte zero work snapshot, and
  the 16-entry first-free link allocator used by `0x80000901`.
- The temporary service bridge was used only as a proof gate. Windows acceptance
  first moved the runtime from 52 to 53 SIF completions by completing the exact
  `SetWorkAddr` semantics, then to 56 completions after the exact initial link
  transaction. Execution advanced from `pc=0x201790` through `0x217070` to the
  next platform dependency at `pc=0x2018B0`.
- That next dependency is service `0x80000400` function `0xFE`, the memory-card
  initialization/version path. This is the point where continuing RPC-by-RPC
  would violate the native-port architecture, so the temporary DBC transport
  proof is retired rather than expanded.

### Step 11.2C — Native Platform Bootstrap HLE (`DONE`)

- **Architecture correction:** controller/DBC and memory-card startup now move
  above SIF to explicit native game/platform API replacements. No new MCSERV or
  DBCMAN packet rows are added.
- **DBC init boundary:** native `0x124510` reproduces the exact successful
  EE-visible state proven by generated `sub_00124510` plus retail DBCMAN:
  zero `0x15B480..0x15B4FF` (0x80-byte work snapshot) and
  `0x15B500..0x15B53F` (16 EE state words), resets the 16-slot native link
  allocator, and returns retail success `1`.
- **DBC link boundary:** native `0x124718` replaces only the synchronous platform
  allocation transaction. Generated `sub_00124A88` remains authoritative for
  all EE-side link-object initialization. The native allocator follows the
  proved retail first-free policy and hard-fails after 16 occupied slots instead
  of inventing a seventeenth.
- **Memory-card bootstrap boundary:** native `0x20AC58` bypasses only the startup
  `sceMcInit` wrapper. Its caller does not consume a result and the successful
  wrapper has no game-visible state side effect. Actual card/save semantics are
  intentionally reserved for Phase 19 `NativeSave`.
- **Bad-code removal:** DBCMAN (`0x80000900`, `0x8000091B`) and MCSERV
  (`0x80000400`) startup bind mappings, DBC-specific RPC reconstruction/state,
  MCSERV captured-call behavior, and DBC guest-memory side-effect plumbing are
  removed from the legacy SIF layer. The old positive packet-synthesis tests are
  replaced by negative ownership guards proving those services now remain
  unsupported if they ever leak back down to SIF.
- **New strict gate:** `native_platform_bootstrap_tests` verifies the exact
  0xC0-byte DBC guest-state write with untouched boundary bytes, rejects an
  undersized guest span without partial writes, and verifies the three native
  replacement addresses.
- **Windows acceptance:** Release build and 16/16 CTests pass, the complete
  Phase-10 viewer remains `status=ok`, and the 20-second runtime reports all
  three `[OpenRatchet:platform]` bootstrap paths as native HLE with
  `runtime declared=13 installed=13 install_errors=0`. DBCMAN/MCSERV no longer
  appear in the startup SIF path. Execution advances to `pc=0x12E668`, where
  the next dependency binds custom services `0x00123456/0x00123457`.

### Step 11.2D — Native Audio Bootstrap HLE (`DONE`)

- **Subsystem identification (proved):** generated `FUN_0012DA28` binds
  services `0x00123456` and `0x00123457`; its embedded error/source strings are
  `error: sceSifBindRpc in %s, at line %d` and
  `/usr/local/989snd/ee/989snd.c`. The current `0x123456/function 0` blocker is
  therefore Sony 989snd audio startup, not another unknown game service.
- **Boundary choice:** do not model 989snd packet functions. The game-level
  wrapper `sub_0022C8D0` is the narrow startup API used by the main boot path at
  `0x2018F8`. Native HLE owns that wrapper and preserves its direct EE/game-side
  state writes plus the direct game sound-manager initialization from
  `FUN_00215390`, while omitting 989snd/SPU/IOP work until Phase 18 NativeAudio.
- **Legacy removal:** startup bind mappings for `0x00123456/0x00123457` are
  removed from `sif_startup_responses`; negative SIF tests now prove that 989snd
  cannot silently leak back into packet synthesis.
- **Strict state gate:** `native_audio_bootstrap_tests` compares the entire
  32 MiB guest image against an independent reference reconstruction of every
  direct wrapper write, verifies no extra bytes change, rejects an undersized
  guest span without partial mutation, and verifies the replacement returns to
  the original caller without inventing a return value.
- **Windows acceptance:** Release build and 17/17 CTests pass, the complete
  Phase-10 viewer remains `status=ok`, and the mandatory 20-second runtime gate
  reports `runtime declared=14 installed=14 install_errors=0` together with
  native DBC init/link, memory-card init and 989snd audio-bootstrap
  `status=ok`. The latest SIF RPC (`0x80000595/function 0x1`) is already
  `completed reason=matched`; there is no current deferred/unsupported SIF
  blocker. Execution instead stalls at `pc=0x216858` while the live Moby state
  remains authentically `status=pool-not-initialized`.

### Step 11.2E — Native Game/Platform Init Boundaries (`DONE`)

- **Native sector-I/O boundary:** the old `pc=0x216858` wait was the 989snd-backed
  game sector wrapper `sub_00216828`. Its startup request is exactly `wads[0]`
  (`0x5E2`, 9 sectors -> `0x1AABC0`). Later, the direct Moby-pool route reaches
  asynchronous helper `FUN_00216788` with exactly TOC `wads2[69]` (`0x38F6`,
  `0x834` sectors -> `0x01654000`), matching the extracted `0x41A000`-byte WAD.
  Both game-facing operations now use `NativeVfs::readSectors`; neither creates
  synthetic 989snd manager state or has a hidden 989snd/SIF fallback.
- **ROM/FILEIO probes:** the `0x11BA40` retry storm was Sony FILEIO init reached
  only by `libgraph::checkModelVersion` and `libscf::IsT10K` reading
  `rom0:ROMVER`. OpenRatchet answers those semantic platform questions directly:
  syscall `0x80` is unavailable in the current host and the retail PC port is not
  a DTL-T10000. No FILEIO service was added.
- **PS2Recomp root cause (counterfactually proved):** Ratchet declares the main
  stack `[0x01FFC000,0x02000000)`, while PS2Recomp previously reserved that exact
  same 0x4000-byte range for synchronous DMAC callbacks. `_SifCmdIntHandler` then
  overwrote the main thread's saved `RA=0x1E9688`. The runtime correction
  dispatches synchronous DMAC handlers below the interrupted guest SP while
  preserving an isolated register context. The dedicated PS2Recomp regression
  is 15/16 on the pre-fix runtime and 16/16 after the fix; the corrupting write
  disappears and Retail advances from roughly DMA/GIF 520/519 to 1034/1032.
  OpenRatchet now carries this as an upstream-ready patch applied only to a
  disposable build-local archive of pinned PS2Recomp revision
  `61300792a0c75c6fd399d89ac538ccdfe30f908d`; `third_party/PS2Recomp` must stay
  clean and is never the patched build source.
- **Later-phase boundaries, not premature emulation:** `sub_00209168` is the
  memory-card preflight (`sceMcGetInfo`/`Sync`/`GetDir`) and now selects its own
  non-blocking Retail result `0` until Phase 19. `FUN_0023A3B8` is the complete
  movie/MPEG wrapper and returns its proved completed result `0` until Phase 17.
  `sub_0022D708` is the level sound-bank wrapper around `snd_BankLoadByLoc` and
  selects the existing no-bank result `0` until Phase 18. None of these boundaries
  fabricates save data, pad input, MPEG state, sound banks, or SIF transport.
- **Authentic Moby-pool publication (phase gate):** after the final `0x216788`
  NativeVfs transfer, original Retail execution enters `sub_001E9B10`. Retail
  itself allocates and publishes `poolBase=0x00DC2EC0` to `0x15FF18` and
  `poolLast=0x00DC6DC0` to `0x15FF20`; their `0x3F00` difference is exactly the
  64-slot, 0x100-byte-stride contract. No pool/global/terminator is synthesized.
  The read-only decoder then reports an authentic live pool with `capacity=64`,
  a valid Retail terminator, at least one Ratchet candidate, `unaccounted=0`,
  and **`status=ok`**. The exact record/terminator count is timing-dependent as
  Retail continues populating the pool; the final clean local run first observed
  one live record before later simulation growth. Retail continues beyond pool
  initialization and performs further level VFS reads.
- **Windows acceptance (`DONE`):** the final clean source contains no temporary
  generated-code probes. Release build succeeds, CTest is **18/18**, and the
  complete Phase-10 viewer remains `status=ok` with `missing=0` and
  `unaccounted=0`. The 20-second runtime reports `runtime declared=21
  installed=21 install_errors=0`, authentic game-sector reads including
  `0x216788`/`wads2[69]`, and finally `[OpenRatchet:live:moby]` with the Retail
  pool pointers, a valid terminator, at least one Ratchet candidate,
  `unaccounted=0`, and **`status=ok`**. Retail graphics/DMA activity continues far
  beyond the previous startup stalls. Step 11.2 is therefore complete.
- **Checkpoint cleanup:** before the Phase-11 checkpoint commit, PS2Recomp is
  restored to an immutable clean dependency. The proven DMAC fix is applied only
  to a build-local committed-HEAD archive, and the incremental Windows helper
  touches dirty inputs only when their SHA-256 content actually changes. This
  prevents repeated PCH invalidation and hundreds of needless `FUN_*` rebuilds.
  Local validation against a pristine pinned checkout proves: dirty-checkout
  rejection, patch application without modifying upstream, full OpenRatchet
  link, 18/18 pre-11.3 CTests, a no-op incremental rebuild, and the 20-second
  Retail gate at `21/21` replacements with authentic `[OpenRatchet:live:moby] ... status=ok`. Windows has now independently confirmed the prepared compatibility
  source, a clean `third_party/PS2Recomp`, the fast content-aware incremental
  rebuild, the Phase-10 viewer regression, `21/21` runtime replacements, and the
  authentic live-Moby gate. Step 11.3 adds the nineteenth CTest on top of that
  already-green checkpoint; the checkpoint dependency is no longer an open gate.

### Remaining Phase 11 Steps

- **Step 11.3 (`DONE`):** Native Ratchet animation selection now comes from
  coherent live Retail state instead of the viewer demo clock. Phase 10 proved
  the immutable oClass-0 bank contains **134 external sequences** while its 134
  original class-local `class+0x48` slots are zero. Generated `sub_00204790`
  proves the complementary runtime rule: Retail increments `class+0x0C`, writes
  the old count into `moby+0x52/+0x53`, installs a newly materialized sequence at
  `class+0x48+oldCount*4`, and absolutizes that appended sequence's frame table.
  The bridge therefore models Ratchet as a leading external-bank prefix plus any
  runtime-local suffix, with the native Phase-10 bank as an independent oracle
  for the external prefix. `sub_0020C880` remains the proved normal local
  producer and proves the `sequenceA==0xFF` transition cache at the exact
  `0x1AABC0 + frameA*0x800` base; `FUN_0020EDE8` remains the authoritative
  materialized-pose consumer of `moby+0x68/+0x6C`, and other proved producers may
  repoint those packets as explicit `direct-guest-packet` provenance. `moby+0x54`
  is carried unchanged as Retail alpha; `moby+0x70` remains uninterpreted.

  The Windows runtime also proved a legitimate pre-materialization construction
  state rather than another pointer failure. Across timing samples the live class
  advertised `sequenceCount=135`, `externalSequenceCount=134`,
  `runtimeLocalSequenceCount=1`; the appended ID `134` was observed as A and as
  either B `0` or B `134`, while both consumed endpoint pointers were still zero.
  `FUN_0020C5F0` proves why the initial `sub_0020C880` call can be skipped for
  Ratchet's zero first local slot, and `sub_00204790` itself does not write
  `moby+0x68/+0x6C`. OpenRatchet therefore preserves the coherent two-zero case as
  `endpoints-not-materialized` instead of inventing packets. This is an accepted
  live-selection state, not a fabricated `status=ok`; once both pointers exist,
  the native pose bridge decodes the exact observed Retail packets. One-sided
  zero pointers, malformed packets, invalid local metadata and accounting
  mismatches still fail closed.

  Final Windows acceptance is green: Release links successfully from the prepared
  PS2Recomp compatibility source, `third_party/PS2Recomp` remains clean, CTest is
  **19/19**, the full Phase-10 viewer remains `status=ok`, the 20-second fallback
  run stays alive with graphics activity and `runtime declared=21 installed=21
  install_errors=0`, `[OpenRatchet:live:moby]` retains `ratchetCandidates=1`,
  `unaccounted=0`, `status=ok`, and `[OpenRatchet:live:ratchet-animation]`
  independently reports the proved 134+1 storage split with
  `status=endpoints-not-materialized` at the sampled construction checkpoint.
  Targeted GCC/Clang `-Werror` regressions additionally cover materialized
  external/local/transition/direct packet-to-packet poses, the exact 135-entry
  mixed-storage shape, one-sided-zero hard failures and malformed packets. The
  current sampled PC / unrelated deferred SIF state is not used as a Step-11.3
  blocker oracle. Rendering ownership remains Step 11.6.
- **Step 11.4 (`DONE`):** The live Moby/Ratchet world-transform boundary
  is now retail-derived without an inferred Euler order or host axis remap.
  `FUN_0020D868` consumes `moby+0x10` as the xyz world-position vector in its
  spatial-distance path, while `FUN_0021E230` independently writes spawn xyz to
  `+0x10/+0x14/+0x18`. `FUN_0020C5F0` initializes `moby+0x2C` from
  `class+0x24`; `FUN_0020CCA8` and `sub_0020CD48` consume that live scale with
  the literal `0x3A800000 = 1/1024`.

  Rotation is bridged from Retail's own materialized basis rather than by
  guessing the VU0 Euler convention. `FUN_0020DEF8` feeds the vector at
  `moby+0x40` to VU0 microprogram `0xD18`, stores `vf20/vf21/vf22` to
  `+0xC0/+0xD0/+0xE0`, and then forms world xyz explicitly as
  `basisX*x + basisY*y + basisZ*z + position`. Its local coordinates are
  multiplied by raw `moby+0x2C` while position is multiplied by 1024, proving
  the native world-space bridge
  `position + basis * (rawSkinnedPosition * mobyScale/1024)`. The 0x100-byte
  Moby is zeroed by `FUN_0020C5F0`; therefore an exactly all-zero basis block is
  preserved as `basis-not-materialized` rather than synthesized from `+0x40`.

  `src/game/rac1_live_transform.*` now validates the unique traversed Ratchet,
  candidate accounting, finite transform inputs and materialization state on
  every coherent runtime handoff. `rac1_live_transform_tests` counterfactually
  pins the Retail basis-column order, exact 1/1024 scale conversion, zero-basis
  construction state and hard failures for non-finite/accounting errors. Local
  GCC/Clang `-Werror` validation is green, including every one of the 20
  CTest-equivalent targets available in the audit snapshot plus the complete
  `openratchet_runtime.cpp` translation unit. Windows acceptance is also green:
  Release build/link succeeds without PS2Recomp churn, **20/20 CTests pass**,
  the Phase-10 viewer remains regression-free, `third_party/PS2Recomp` stays
  clean, runtime replacements remain `21/21` with `install_errors=0`, and the
  20-second Retail run reaches the authentic `basis-not-materialized`
  construction state with one Ratchet and `unaccounted=0`. This sampled zero
  basis is evidence of pre-materialization, not a blocker oracle and not a
  reason to synthesize host orientation. Step 11.5 is the next active step.
- **Step 11.5 (`TODO`):** Independently prove and bridge the retail gameplay
  camera/view state.
- **Step 11.6 (`TODO`):** Continuously update native rendered Moby instances
  from the live simulation snapshot.
- **Step 11.7 (`TODO`):** Full Phase-11 regression and mandatory 20-second
  runtime gate before commit.

