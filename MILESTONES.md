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
| Phase 11 | `DONE` | Live game-state & camera bridge | Native renderer follows the running recompiled game through proved live Moby, animation, transform, camera, renderer-identity, and gameplay-sky bridges; unavailable Retail state remains explicitly deferred. |
| Phase 12 | `IN PROGRESS` | Native input & playable Ratchet | PC controller/keyboard input drives original simulation; playable Ratchet. |
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
- **Step 11.5 (`DONE`):** The gameplay-camera boundary is proved
  independently of the Moby pool and bridged read-only from Retail's global
  0x3A0-byte camera state at `0x00186F40`. `sub_001E9B10` zeroes exactly that
  block. `FUN_0020D868` independently consumes `state+0x140` (`0x187080`) as
  the xyz observer/camera world position in Moby spatial logic, while
  `FUN_001ED2B0` and `sub_001EDAA8` publish the selected camera transform's
  position and three orientation vectors to `+0x140` and
  `+0x350/+0x360/+0x370`. `FUN_00218D10` provides an independent initialization
  oracle: position `(256,256,64)` plus the identity xyz orientation.

  The renderer-facing view/projection contract is taken from Retail's already
  materialized matrix rather than reconstructed from a guessed FOV or host
  camera convention. `FUN_001F2260` builds the camera matrices from the selected
  orientation, and `FUN_0022BF94` directly loads qwords `+0x100/+0x110/+0x120/
  +0x130`, combines them as `clipX*x + clipY*y + clipZ*z + clipW*w`, then runs
  `vclipw` and perspective division. `FUN_00227A08` independently consumes the
  adjacent `+0xC0..+0xF0` matrix together with `+0x140` position. Therefore
  `src/game/rac1_live_camera.*` preserves the exact Retail position,
  orientation-vector and clip-transform values without deriving target/up,
  Euler angles, handedness or FOV. All-zero orientation and clip matrices remain
  explicit pre-materialization states; non-finite values fail closed.

  `rac1_live_camera_tests` pins the literal Retail addresses, the exact
  X/Y/Z/W clip-vector multiplication order, the Retail init state and all
  materialization/error gates. The runtime samples this independent global in
  the same coherent `GuestExecutionScope` as the other Phase-11 state and logs
  `[OpenRatchet:live:camera]`; diagnostics expose it directly. Local GCC/Clang
  `-Werror` validation is green for the new test and complete runtime TU.
  Windows acceptance is also green: Release build/link succeeds, **21/21 CTests
  pass**, the full Phase-10 native viewer remains regression-free,
  `third_party/PS2Recomp` stays clean, runtime replacements remain `21/21` with
  `install_errors=0`, and the mandatory 20-second fallback run stays alive with
  graphics activity. The sampled camera state is the authentic all-zero
  construction state `status=orientation-not-materialized`; this is evidence of
  pre-materialization, not a reason to fabricate identity orientation, FOV or a
  host camera. Step 11.6 is now the next active step.
- **Step 11.6 (`DONE`):** Transfer final frame ownership in
  `openratchet.exe` from the PS2 GS/framebuffer presentation path to the existing
  native Phase-6..10 renderer, then continuously update native scene/Moby state
  from the proved 11.2-11.5 live bridges. The current horizontal-line/fragmented
  fallback image is therefore not a GS bug to emulate around; it is precisely the
  presentation path Step 11.6 supersedes. No fake camera, transform or animation
  state may be introduced when a live component is still in an explicit
  pre-materialization status.

  - **Step 11.6A (`DONE`):** The narrow
    ownership cut is the existing PS2Runtime post-GS/pre-`EndDrawing` host
    callback. OpenRatchet first flushes the already queued compatibility draw,
    clears that image, and then owns the final visible frame; PS2Runtime remains
    only the fallback EE/window host and `third_party/PS2Recomp` is unchanged.
    The Phase-10 mesh upload/draw helpers are extracted into
    `src/render/native_mesh_renderer.*` and reused by both the standalone viewer
    and the runtime, so no second renderer is introduced.

    Runtime scene identity is fail-closed. A successful *complete* native indexed
    sector read may publish its exact asset identity, and the only currently
    accepted Retail->native level mapping is the independently proved Level-0
    request `wads2[69]` (`0x38F6`, `0x834` sectors -> `0x01654000`) -> native
    level index 0. Partial reads, neighboring WAD indices, different destinations
    and unmatched ranges cannot activate a level. `rac1_render_bridge_tests` pins
    this negative space.

    The runtime static-world renderer reuses the authoritative native Level-0
    texture/tfrag/tie/shrub decoders and keeps their vertices in Retail world
    coordinates. When Step-11.5 reports `status=ok`, its four already-materialized
    clip columns are loaded directly as the host projection matrix in exact
    `clipX*x + clipY*y + clipZ*z + clipW*w` order; there is no `Camera3D`, guessed
    FOV, target/up reconstruction or axis remap. When camera state is not
    materialized, the scene is explicitly deferred. Sky remains
    `deferred-retail-transform-unbridged`, and live Mobys remain
    `deferred-live-identity-unbridged`; neither receives a viewer/demo fallback.

    Renderer accounting logs `mapped/materialized/rendered/deferred/unaccounted`
    plus renderer, Moby-pool, camera, animation and transform states;
    `tools/diagnose-native.ps1` surfaces the permanent `OpenRatchet:render:*`
    gates directly. Local
    `-Wall -Wextra -Werror` gates pass for the render bridge, native VFS observer,
    runtime renderer, runtime translation unit and the modified viewer. A real
    Level-0 decoder smoke gate materializes 78 terrain batches / 24,520 terrain
    triangles and 201 tie+shrub batches / 396,708 tie + 327,841 shrub triangles.
    Windows acceptance is complete: Release builds and links successfully, **22/22**
    CTests pass, the Phase-10 native level viewer remains regression-free,
    `third_party/PS2Recomp` stays clean, runtime replacements remain **21/21**
    with `install_errors=0`, and the mandatory 20-second runtime remains alive
    with graphics activity and harness exit 0. The proved Level-0 transfer maps
    and materializes the native scene with `unaccounted=0`. At the sampled
    checkpoint the Retail camera remains authentically
    `orientation-not-materialized`, so renderer accounting correctly reports
    `rendered=0 deferred=1` rather than inventing a host camera. The resulting
    black native-owned frame is therefore the expected fail-closed output of
    11.6A, not a GS presentation regression.
  - **Step 11.6B (`DONE`):** Bridge the remaining renderer-facing Retail
    identity needed for live Mobys/Ratchet and the Retail sky transform, then
    consume the already proved 11.3 pose packets and 11.4 basis/position/scale
    only when each component is actually materialized. No Phase-12 input
    semantics are included.
    - **Step 11.6B1 (`DONE`):** Ratchet's
      renderer identity is now joined without instance proximity or relocated
      pointer guesses. Retail `FUN_0020C5F0` receives `oClass` as its second
      argument and writes it unchanged to `moby+0xA6`; Steps 11.3/11.4 already
      select exactly one traversed `oClass==0` Moby. The native Level-0 decoder
      independently exposes exactly one rendered `oClass==0` topology. Those
      domains are therefore joined only when animation and transform identify
      the same guest Moby address and the live transform also reports
      `oClass==0`. A tempting stronger relation was explicitly disproved: the
      native Ratchet class offset is `0x739540`; `0x654000 + 0x739540` is
      `0xD8D540`, not the sampled live `moby+0x24` class pointer `0xA9E340`.
      `moby+0x24` is consequently not used as a native-class relocation oracle.

      The Phase-10 CPU-skinned dynamic-VBO helper has been extracted from the
      viewer into `src/render/native_skinned_moby_renderer.*` and is shared by
      viewer and runtime. Runtime Level 0 now validates and retains Ratchet's
      exact `skinVertexIndex` topology: **4 material batches / 6,856 triangles /
      4,026 skin vertices**. When and only when both Step-11.3 live pose packets
      and Step-11.4 world basis are materialized, the runtime decodes those
      observed packets, executes the already-proved Ratchet skinning program,
      transforms each raw skinned point by the exact
      `position + basis * (raw * scale/1024)` bridge, and updates that shared
      topology. Current `endpoints-not-materialized` / `basis-not-materialized`
      states remain deferred; no native-bank animation or static instance
      transform is substituted. Renderer accounting now exposes
      `liveMobyMapped/liveMobyMaterialized/liveMobyRendered`, `ratchetIdentity`,
      `ratchetFrame`, and `ratchetGpu`. On the current sampled construction state
      the expected new invariant is one mapped Ratchet, zero materialized/rendered
      live Mobys, all live records deferred, and `liveMobyUnaccounted=0`.

      Local GCC/Clang `-Wall -Wextra -Werror` gates pass for the pure identity
      bridge, shared dynamic renderer, runtime renderer and modified viewer; the
      complete runtime translation unit is also clean apart from known pinned
      PS2Recomp-header warnings demoted from `-Werror`. A real Level-0 no-op-GPU
      smoke test reproduces the 11.6A static counts plus the exact 4/6,856/4,026
      Ratchet topology, and counterfactually creates then updates the dynamic GPU
      buffers from an authentic decoded Ratchet skinning frame. Windows acceptance
      is now green as well: Release builds, **22/22** CTests pass, the Phase-10
      viewer keeps the exact 6,856/4,026 Ratchet visual gate, the 20-second runtime
      remains alive with graphics activity and harness exit 0, and the stable native
      frame reports `liveMobyMapped=1`, `ratchetIdentity=ok`,
      `liveMobyUnaccounted=0`, `poolUnaccounted=0`, `status=ok` while preserving the
      authentic `endpoints-not-materialized`, `basis-not-materialized` and
      `orientation-not-materialized` deferrals. `third_party/PS2Recomp` is clean.
    - **Step 11.6B2A (`DONE`):**
      Extend proved live class identity from Ratchet to every active Retail Moby
      without joining to a Phase-10 static instance. The first Windows gate exposed
      the level-core/runtime-domain distinction (`oClass=1905` is valid live state
      but absent from Level 0's 125 native class entries). The second Windows gate
      then caught a separate field/table mistake: all 5/5 active Mobys failed when
      `moby+0x24` was compared against `0x1B3580[slot]`. Re-reading the constructor
      proves that `FUN_0020C5F0` performs **two different slot-table lookups**:
      `slot = *(u8 *)(0x1B3AC0 + oClass)`, then
      `classDataPtr = *(u32 *)(0x1B3200 + slot*4)` is stored at `moby+0x24`, while
      the independent word `*(u32 *)(0x1B3580 + slot*4)` is stored at `moby+0x74`.
      The latter is not the class-data pointer and remains semantically
      uninterpreted here. `sub_00203640` publishes loaded class-data pointers into
      `0x1B3200`; `FUN_0020C5F0` immediately consumes `moby+0x24` as the class block
      (`+0x0E/+0x44/+0x24/+0x40/+0x48`), independently corroborating the field.
      OpenRatchet therefore requires exact `0x1B3200[slot] == moby+0x24` agreement
      for every active record. Only after that Retail identity proof does the native
      level catalog classify topology. Level 0 remains exactly **125** unique native
      classes = **14 renderable + 7 intentionally invisible + 104 class-only**;
      registry-valid identities absent from that catalog are explicitly
      `runtime-only`. Out-of-range oClass, `0xFF` slots, class-data pointer mismatch,
      duplicate native classes and count mismatches remain hard failures. Local
      GCC/Clang tests include a conflicting `0x1B3580` decoy specifically to prevent
      regression to the disproved table. Windows revalidation is now green: Release
      builds, **22/22 CTests pass**, the Phase-10 viewer remains exact, PS2Recomp is
      clean, and the stable 20-second runtime classifies all five sampled active
      Mobys as **3 renderable-topology + 1 class-only + 1 runtime-only**, with
      `liveMobyMapped=5`, all Retail registry error counters zero,
      `liveMobyUnaccounted=0`, `poolUnaccounted=0`, `liveMobyClassMap=ok` and
      `status=ok`.
    - **Step 11.6B2B (`DONE`):** The Retail
      gameplay-sky transform is reconstructed from the renderer path rather than
      copied from the Phase-10 viewer. `FUN_0022B288` reads the sky pointer at
      `0x0016045C`, shell count at `sky+0x6`, shell pointers at `sky+0x20`, the
      base angle at `0x00160404`, and the renderer translation qword at
      `0x00160460`. For each shell it rebuilds the object matrix at transient
      scratch `0x001D96E0`, then `FUN_0022B690/FUN_0022BF94` consume that matrix
      with the exact Step-11.5 clip columns. OpenRatchet samples only the stable
      inputs and reconstructs the per-shell object matrices; it never treats
      `0x001D96E0` as persistent state and never substitutes the viewer camera.

      `FUN_001FA070` proves the rotation order Z -> Y -> X; the sky passes X=0,
      so the bridge evaluates the exact `Ry(Y) * Rz(Z)` path plus Retail's
      per-shell scales. The angle wrapper and sine polynomial are transcribed with
      the original binary32 constants and operation ordering, and dedicated tests
      pin the matrices bit-for-bit against independent R5900 oracle values.

      The first Windows B2B run passed Release, **23/23 CTests**, viewer regression,
      runtime liveness and accounting, but its permanent provenance probe proved
      that the live gameplay sky is **not** the Phase-8 level-core sky: live
      `0x714640` has 4 shells with `(clusterCount,flags)` signatures
      `(38,1),(25,0),(3,0),(24,0)`, whereas the viewer/core resource has 5 shells /
      118 clusters / 2,366 triangles. Root-cause analysis of the exact Retail
      level loader `sub_001EA830` closes that ambiguity. The complete indexed
      `wads2/69` asset (`0x834` sectors, encoded WAD size `0x419F6E`) decompresses
      to `0x7318C0` bytes at the same guest output base `0x654000` seen at runtime.
      Retail forms `s5 = wadBase + *(u32 *)(wadBase+0x04)` and passes
      `s5 + *(u32 *)(wadBase+0x14)` to `FUN_002028E0`; for this exact WAD the
      offsets are `0x5C000 + 0x64640 = 0x0C0640`, and
      `0x654000 + 0x0C0640 = 0x714640` exactly. Native decoding at `0x0C0640`
      reproduces the live resource byte-semantically: **4 shells / 90 clusters /
      1,472 triangles / 8 textures / 5 shell-separated GPU batches**, with the
      same four shell signatures.

      The corrected runtime renderer therefore keeps the Phase-8/core sky solely
      for the standalone viewer, but obtains gameplay-sky topology from the exact
      WAD2 asset that triggered the proved Level-0 load. It natively decompresses
      that asset, resolves the Retail `+0x04 + +0x14` sky offset, and joins live
      shell transforms only to this same-resource topology. Shell count, every
      shell's native source offset/relocated guest pointer, and each
      `(clusterCount,flags)` pair are verified **before** a deferred transform is
      accepted, so `translation-not-materialized` can no longer conceal the old
      5-vs-4 resource mismatch. Local GCC/Clang `-Wall -Wextra -Werror` contracts,
      real WAD2/69 decode smoke and the full no-op-GPU Level-0 renderer smoke are
      green. Windows revalidation is now green as well: Release builds, **23/23**
      CTests pass, the Phase-10 viewer remains exact on its independent 5-shell
      core-sky path, and the 20-second runtime remains alive with graphics activity
      and harness exit 0. The live gameplay sky is the proved 4-shell resource at
      `0x714640`; all four shell pointers/counts/flags pass the runtime-WAD identity
      join before the authentic `translation-not-materialized` deferral is accepted.
      Stable renderer accounting therefore reports `skyMap=live-transform-deferred`,
      `skyMapped=1`, `skyMaterialized=0`, `skyDeferred=1`, `skyUnaccounted=0`,
      `liveMobyMapped=5`, `liveMobyUnaccounted=0`, `poolUnaccounted=0`, and
      overall `status=ok`. The Retail camera remains authentically
      `orientation-not-materialized`, so the black native-owned frame is still the
      correct fail-closed output. `third_party/PS2Recomp` remains clean.
- **Step 11.7 (`DONE`):** Final Phase-11 regression passed on Windows: Release
  build succeeds, **23/23 CTests pass**, the complete Phase-10 Level-0 viewer
  regression remains `status=ok`, the mandatory 20-second runtime stays alive
  with graphics activity and harness exit 0, all native bootstrap/runtime
  replacements remain installed with zero install errors, renderer accounting
  remains fully accounted, and `third_party/PS2Recomp` is clean. Phase 11 closes
  without fabricating camera, sky translation, Ratchet pose, or Ratchet basis
  state that Retail has not materialized yet.


---

## Active Work: Phase 12 — Native Input & Playable Ratchet

Phase 12 moves controller ownership above the remaining PS2 DBC transport while
preserving the original Ratchet & Clank game-side controller state machine and
input parser. The acceptance target is not a host-written movement state: native
keyboard/controller samples must enter the same Retail parsing path that gameplay
already consumes.

### Step 12.1 — Retail Pad-Report Boundary (`DONE`)

- **Retail controller object (proved):** `FUN_00217048` constructs the primary
  controller state at guest `0x0013C940`, stores the DBC link handle at `+0x194`,
  and initializes connection state `+0x198/+0x19C`. `sub_002170C8` is the
  per-update controller state machine.
- **Raw-input consumer (proved):** once connected, `sub_002170C8` calls
  `sub_00124BD8(handle, scratch)`, forwards its returned byte count and scratch
  pointer unchanged to `FUN_00217328`, and leaves that Retail parser responsible
  for logical buttons, press/release edges, stick deadzones/history and gameplay
  controller globals. OpenRatchet does **not** write `+0x1A0/+0x1A4` or movement
  state directly.
- **Six-byte report contract (proved):** `FUN_00217328` always consumes bytes
  `0..1` as an active-low 16-bit button word (`(b0<<8)|b1`, then XOR `0xFFFF`),
  and for report lengths `>=6` consumes bytes `2..5` as four centered analog
  axes in exact order right-X, right-Y, left-X, left-Y with Retail center `0x7F`.
  Its optional pressure-byte path begins only at length `0x12`; Step 12.1 returns
  exactly six bytes so pressure semantics remain honestly deferred instead of
  fabricated.
- **Native ownership boundary:** `src/game/rac1_native_input.*` replaces only the
  four libdbc-facing transactions used by the Retail update path:
  `0x124BD8` raw receive, `0x124CB0` connection-transition info,
  `0x124DA0` device status, and `0x1250D8` auxiliary transition info. Status `1`
  selects Retail's connected/readable path. The two optional transition queries
  expose zero PC-specific metadata bytes while deterministically clearing the
  caller's four-byte scratch; no SIF/IOP transaction is reintroduced.
- **Host sampling:** the native presentation owner publishes one coherent input
  snapshot per host frame. Keyboard defaults are WASD = left stick, arrows =
  right stick, Space/E/F/R = Cross/Circle/Square/Triangle, Shift = L1/R1,
  Z/C = L2/R2, Enter = Start and Backspace = Select. Raylib gamepad 0 maps its
  standard sticks, D-pad, face buttons, shoulders, triggers, Start/Select and
  stick clicks directly to the corresponding Retail pad bits. Unfocused windows
  publish neutral input.
- **Concurrency/accounting:** the six-byte report is packed into one atomic
  snapshot, so the guest callback cannot observe a torn host sample. Invalid
  guest output ranges fail closed with zero returned bytes and no partial write.
  Diagnostics report the semantic boundary as `[OpenRatchet:input]` and identify
  the unchanged Retail consumer `FUN_00217328`.
- **Initial Windows gate:** Release build succeeds, **24/24 CTests pass** (including
  `rac1_native_input`), the complete native Level-0 viewer remains unchanged, and
  the ordinary 20-second runtime baseline still reaches Level 0 with renderer/live
  Moby accounting `status=ok`. All **25/25** declared runtime replacements install
  with zero errors and `third_party/PS2Recomp` remains clean. However, neither a
  20-second nor a focused 60-second run ever enters any of the four pad callbacks;
  `[OpenRatchet:input]` remains absent even at runtime tick 2520. Step 12.1 is
  therefore not accepted merely because the input module's unit contract is green.
- **Upstream simulation blocker (proved):** the stable sampled PC `0x232D60` is
  inside `sub_00232D00`, which binds a separate custom SIF RPC client at
  `0x1DD1A8` to service ID `0x11` and waits for the bind before the only startup
  caller can continue to `sub_00202A98` and eventually `FUN_00217A10 ->
  sub_002170C8`. This is not the simultaneously visible 989snd client at
  `0x15EBC0`; the two transports are deliberately kept separate.
- **Retail stash API contract (proved):** `0x232E40` reserves one of exactly 64
  slots, uses `sceSifSetDma` to stage `a1*16` real EE bytes, records an `a2*16`
  slot length and returns the slot index. `0x232F20` uses service function 1 to
  copy a bounded 16-byte-unit subrange back to an EE destination, while
  `0x233038` returns the slot length. `sub_00232D00` service function 2 only
  acquires the remote IOP buffer base/capacity and clears the 64-entry EE table.
  All four `sub_00202A98` store call sites use equal copy/reserved lengths, and
  `FUN_00226848` retrieves a full slot immediately before the existing native-
  compatible decompressor path.
- **Native resource-stash prerequisite:** `src/game/rac1_native_stash.*` now owns
  those four game/resource-facing functions (`0x232D00`, `0x232E40`, `0x232F20`,
  `0x233038`) as a genuine host-memory byte stash. It preserves 64 Retail-visible
  slot IDs, exact 16-byte units, offset/length/error semantics and real data
  round-tripping, but removes the PS2-only SID-`0x11` bind, IOP address and SIF DMA
  transport. Invalid guest ranges or unmaterialized bytes fail closed; no fake
  service response or synthetic IOP pointer is created. The dedicated stash test
  passes with GCC and Clang under `-Wall -Wextra -Werror`.
- **Post-stash Windows evidence (proved):** the native stash prerequisite is
  live, not merely unit-tested. Runtime initializes all 64 host slots and stores
  real payloads in slot 0 (`63488` bytes) and slot 1 (`22528` bytes), while the
  old stable `pc=0x232D60` wait disappears. Retail advances to a new stable
  `pc=0x1EB968`, yet `[OpenRatchet:input]` is still absent. The new pending SIF
  trace is exactly client `0x15EBC0`, function `8`, zero send bytes and a
  `0x0C`-byte receive area at `0x133100`.
- **989snd command-8 producer/consumer (proved):** `FUN_0012DA28` binds client
  `0x15EBC0` to Sony 989snd service `0x123456`. `sub_0012E1A8` is a zero-argument
  wrapper that invokes `FUN_0012E6E0(command=8, payloadBytes=0)`, producing the
  exact pending call above. The startup caller `sub_001E9488` executes the
  already-native Phase-18-deferred level-bank wrapper `sub_0022D708` at
  `0x1E99A8`, copies its bank result into `s0` in the delay slot, immediately
  calls `sub_0012E1A8` at `0x1E99B0`, and later enters `sub_001EB798`. There,
  `0x1EB968` repeatedly calls `FUN_0012DC80`; its `FUN_0012DE70` path checks the
  same client `0x15EBC0`. This is one causal audio follow-up/wait chain, not a
  generic asset-I/O protocol and not an input-hook defect.
- **Native audio follow-up prerequisite:** the existing
  `native_audio_bootstrap` boundary now also owns `sub_0012E1A8` directly. It
  selects the exact successful `SifCallRpc` submission result `0` proved at
  `sub_0011B1C8`, returns to the unchanged Retail caller and publishes no guest
  memory, 989snd client, IOP, response-buffer or busy-state mutation. This is a
  Phase-18-deferred audio API HLE, not a synthetic service response; the higher
  semantic name of 989snd command 8 remains intentionally unguessed.
- **Post-command-8 Windows evidence (proved):** Release build succeeds,
  **25/25 CTests pass**, and the complete Level-0 viewer regression remains
  `status=ok`. The `989snd-command8` native HLE fires, the old `pc=0x1EB968`
  polling stall disappears, and native input becomes genuinely reachable:
  `[OpenRatchet:input] component=device-status ... status=ok` and
  `component=connection-transition ... status=ok` execute from the unchanged
  Retail controller state machine. Raw six-byte receive/parser reachability is
  not yet proved, so Step 12.1 is still open.
- **PS2Recomp continuation defect (proved):** the new stable failure is
  `pc=0x22BE08` with `No exact recompiled function`. Generated
  `sub_0022BBA0` executes `BGEZAL` at `0x22BE00`, writes the architected link
  address `0x22BE08`, enters the overlapping helper body at `0x22BEC4`, and its
  `JR $ra` returns to that exact continuation. `register_functions.cpp` contains
  entries for `0x22BBA0`, `0x22BCC0` and `0x22BEC4` but not `0x22BE08`. The
  PS2Recomp control-flow analyzer promotes JAL/JALR return PCs yet omitted all
  four REGIMM link branches (`BLTZAL/BGEZAL/BLTZALL/BGEZALL`). The current R&C1
  fallback contains four such BGEZAL sites: `0x22BE00 -> 0x22BEC4` plus
  `0x21835C/0x218484/0x21863C -> 0x21880C`; every one has a legitimate PC+8
  return continuation.
- **Build-local recompiler prerequisite:** the OpenRatchet-owned PS2Recomp
  compatibility patch now promotes REGIMM branch-and-link `PC+8` to the same
  resumable-owner path already used for JAL/JALR, with an upstream-style
  CodeGenerator regression and an OpenRatchet counterfactual CTest. The latter
  fails against the pristine pinned analyzer at `0x22BE08` and passes against
  the patched build-local source. `tools/bootstrap.ps1 -Stage Recompile` now
  builds `ps2_recomp` from the same immutable patched-copy mechanism rather than
  modifying or compiling directly from `third_party/PS2Recomp`.
- **First patched-recompile Windows attempt (proved tooling blocker):** the
  immutable patched PS2Recomp source prepares and builds successfully from
  `build/tooling/_openratchet/PS2Recomp`, but recompilation stops before analysis
  because Ghidra-exported `build/game.toml` still contains an absolute `input`
  path from the former checkout `OpenRatchet2`. The active repository is
  `OpenRatchet`, so PS2Recomp correctly reports `Could not load ELF file` and
  discovers zero functions. This is a reproducibility/tooling-path defect, not a
  REGIMM failure and not a reason to touch game HLE state.
- **Recompile-config portability correction:** `tools/bootstrap.ps1 -Stage
  Recompile` now treats Ghidra's absolute filesystem fields as export-time
  metadata and canonicalizes exactly one `input`, `output`, and `ghidra_output`
  entry to the current checkout before launching PS2Recomp. Missing or duplicate
  keys fail hard rather than silently recompiling against an unintended path.
  The authoritative `-Elf` argument/default remains `build/extracted/PS2_MAIN.ELF`.
- **REGIMM Windows acceptance (`DONE`):** static recompilation now succeeds from
  the patched immutable tool copy against the active checkout: 1308 functions are
  discovered, 1260 recompiled, 48 stubbed, 23,125 additional entrypoints emitted,
  zero instructions remain unhandled, and the report has zero errors.
  `register_functions.cpp` contains the exact four required owner resumptions
  `0x218364`, `0x21848C`, `0x218644`, and `0x22BE08`; generated
  `sub_0022BBA0` contains both `case 0x22be08u` and `label_22be08`. Release build
  succeeds, the complete Level-0 viewer regression is unchanged, **26/26 CTests
  pass** including `ps2recomp_control_flow`, runtime replacements remain
  bootstrap `2/2` and runtime `30/30`, and the former
  `No exact recompiled function for guest PC 0x22be08` failure is absent.
- **Post-resume frame blocker (proved):** Retail now advances beyond `0x22BE08`
  and repeatedly resumes inside `sub_00235BE8` at `pc=0x235F1C` with
  `ra=0x70000000` and `sp=0x70000070`. Those scratchpad addresses are intentional
  state established by the Retail routine, not stack corruption. Immediately
  before the observed loop, `sub_00235BE8` programs EE DMAC channel 9
  (`0x1000D400`, TO_SPR) in normal mode with `SADR=0x2000`, `MADR=s6` and
  `QWC=min(0x80,s7)*2`, then consumes the staged 32-byte records directly from
  `0x70002000`. Its caller later invokes `FUN_001F9928`, which programs channel 8
  (`0x1000D000`, FROM_SPR) in normal mode to copy `0x40` qwords from
  scratchpad offset `0x3600` to guest `0x1E3200`.
- **PS2Recomp SPR-DMA root cause (counterfactually proved locally):** the pinned
  runtime recognizes DMA CHCR writes and reports completion, but performs data
  movement only for VIF0/VIF1/GIF. Channels 8/9 have no transfer producer at all;
  a CHCR read nevertheless clears `STR`, so Retail can observe a completed
  transfer whose scratchpad/RDRAM bytes were never produced. An OpenRatchet
  regression reproduces the exact channel-9 RDRAM->SPR and channel-8 SPR->RDRAM
  normal transfers plus 16-KiB SADR wrapping: it fails against pristine
  PS2Memory at the first channel-9 byte comparison and passes against the
  build-local compatibility correction with GCC and Clang.
- **Minimal fallback correction:** the OpenRatchet-owned PS2Recomp compatibility
  patch implements only synchronous **normal-mode** SPR channels 8/9 for
  contiguous RDRAM: exact `QWC*16` byte movement, 14-bit scratchpad wrapping,
  MADR/SADR advancement, QWC zeroing, STR clear, D_STAT channel completion and
  queued DMAC cause. FROM_SPR writes mark modified guest RAM.
  Chain/interleave/MFIFO and non-RDRAM semantics are not invented. This is
  correctness for the still-active Retail EE fallback, not a new OpenRatchet
  DMAC subsystem and not a reversal of the native Phase-4 WAD decompressor
  boundary.
- **First SPR Windows build attempt (test-harness blocker):** the patched
  `ps2_runtime` library itself compiled, but the standalone
  `ps2recomp_spr_dma_tests` executable initially failed at link time on the four
  generated dispatch-table globals (`g_ps2RecompiledFunctionTableBase`,
  `g_ps2RecompiledFunctionTableEnd`, `g_ps2RecompiledFunctionTableSlotCount`, and
  `g_ps2RecompiledFunctionTable`). This was not an SPR semantic failure: the
  static runtime library references those runner-owned globals even though the
  memory-only regression never dispatches guest code. The regression now reuses
  PS2Recomp's own `ps2xTest/src/test_function_table.cpp`, whose explicit purpose
  is to provide the same globals when unit tests link `ps2_runtime` without
  generated runner code.
- **SPR Windows acceptance (`DONE`):** Release rebuild succeeds and **27/27
  CTests pass**, including `ps2recomp_spr_dma`; the complete native Level-0
  viewer remains unchanged. The persistent `pc=0x235F1C` condition disappears,
  Retail advances into the controller path, and the native six-byte receive is
  observed as `[OpenRatchet:input] component=read ... reportBytes=6 ...
  parser=retail-FUN_00217328 ... status=ok`. This is the decisive Step-12.1
  acceptance: host input now reaches the unchanged Retail parser rather than a
  host-authored gameplay state. Step 12.1 is complete.
- **Post-Step-12.1 DECI2 blocker (proved):** the next stable execution condition
  is `pc=0x1198B0` inside `FUN_001197A8`. Retail has set state `0x154A50+0x0C`
  nonzero, requested a DECI2 send through `FUN_00119468 -> Deci2Call(code=3)`,
  and then loops through `sub_00119498 -> Deci2Call(code=4)` until that exact
  state word is cleared. `sub_001199C8` proves the registered session contract:
  protocol `0x210`, callback state/`opt` `0x154A50`, and handler `FUN_00119610`
  (`0x119610`). The handler's event-3 path drains the queued payload through
  `sceDeci2ExSend`; its event-4 completion path reaches `0x119788` and writes
  zero to callback-state `+0x0C`. The existing Release runtime never produces
  those registered callbacks, so Retail cannot complete its own state machine.
- **PS2Recomp DECI2 root cause and correction (`DONE`):**
  pinned PS2Recomp hides the entire `Deci2Call` implementation behind
  `_DEBUG || RUNTIME_DECI2CALL`, and even when enabled treats request-send/poll
  as unconditional success without dispatching the registered handler. The
  build-local compatibility patch makes DECI2 syscall semantics available in
  Release, preserves the open-time `opt`/handler session state, synchronously
  dispatches event `3` from `sceDeci2ReqSend`, records one pending completion,
  and dispatches event `4` once from `sceDeci2Poll`. The callback inherits the
  interrupted guest SP, receives the exact open-time `opt` as `a2`, uses a zero
  RA sentinel, and executes through the existing runtime function table; no host
  write is substituted for Retail's `state+0x0C = 0` completion. A dedicated
  counterfactual regression fails on pristine Release-style DECI2 because event
  3 is never dispatched and passes on the patched source with both GCC and
  Clang, including payload drain, event-4 completion and no duplicate completion.
- **Direct parsed-input gate (`LIVE / NONZERO INPUT PROOF PENDING`):**
  `rac1_native_input` now exposes a strictly read-only inspector for the exact
  `FUN_00217328` outputs in controller state `0x13C940`: right-X/right-Y/left-X/
  left-Y at `+0x100/+0x104/+0x108/+0x10C`, current logical buttons at `+0x1A0`,
  pressed edges at `+0x1A4`, and released edges at `+0x1A8`. The runtime samples
  those fields under the existing `GuestExecutionScope` and logs
  `[OpenRatchet:input] component=parsed-live ... ownership=retail-read-only` only
  as an observation; it never mutates parser/game state. Unit coverage proves
  exact field extraction, stick order, failure on truncated guest memory and
  byte-for-byte non-mutation of the inspected controller region.
- **DECI2 / parsed-input Windows acceptance:** the 45-second Windows runtime no
  longer stalls at `pc=0x1198B0`. Retail repeatedly reaches the read-only parser
  snapshot as `[OpenRatchet:input] component=parsed-live ...
  parser=retail-FUN_00217328 ... ownership=retail-read-only status=ok`. Neutral
  host state is therefore proved end-to-end; a focused nonzero button/stick run
  is still required to close Step 12.2 completely.
- **Step-12.3 active blocker (proved):** after the DECI2 correction Retail now
  hard-stalls at `pc=0x12E820` from runtime tick 240 onward, with `ra=0x12E83C`
  and frozen DMA/GIF/VIF counters, Ratchet transform, animation and camera. This
  address is the full/busy loop inside Sony 989snd `FUN_0012E6E0`. The loop calls
  `FUN_0012DC80`; its pump reaches `sub_0012E9D8`, which submits exact RPC
  function `0x4D` through client `0x15EBC0`. The same Windows run records that
  request permanently `pending reason=unbound-client ... busy=1`. The function-
  `0x4D` evidence is therefore now causally on the active execution path rather
  than merely concurrent telemetry.
- **Native 989snd command-submit prerequisite:** `native_audio_bootstrap` now
  also owns shared EE command submitter `FUN_0012E6E0` (`0x12E6E0`). This is one
  boundary for the 24 generated 989snd command wrappers instead of packet-specific
  SIF responses or per-command guessed semantics. The replacement accepts the
  already-Phase-18-deferred audio request, preserves the caller's registers and
  guest memory, and returns through the original RA without creating queue slots,
  client/busy state, response data, IOP state or audio output. Incoming `v0` is
  preserved rather than inventing a replacement result because no game-consumed
  return is proved for this shared submitter. `FUN_0012DC80`/`sub_0012E9D8` remain
  unchanged; they are simply no longer entered by newly submitted deferred audio
  commands.
- **Windows acceptance of the shared 989snd boundary is complete:** **28/28
  CTests** pass, the Level-0 viewer remains exact, runtime replacements are
  **31/31** with zero install errors, and the 45-second runtime records
  `component=989snd-command-submit ... status=ok`. The former `pc=0x12E820`
  freeze is gone: DMA/GIF/VIF counters and Ratchet animation continue advancing.
  The same run closes Step 12.2 with a real Cross transition through the unchanged
  Retail parser: native `pressed=0x4000` becomes `buttons=0x4000` and
  `pressedEdges=0x4000`, followed by `releasedEdges=0x4000`; the inspector remains
  read-only and no DBC/SIF fallback is reintroduced.
- **Native presentation substep 12.3A is Windows-accepted.** The absolute-world
  camera correction and the GS/OpenGL Y-convention bridge are both now validated
  on Windows. Release builds, **28/28 CTests pass**, the Level-0 viewer preserves
  every strict accounting invariant, and the mandatory 20-second runtime remains
  alive with graphics activity observed. `openratchet.exe` reaches native-owned
  rendering with `renderer=ok`, `camera=ok`, `rendered=1` and overall `status=ok`.
  The former black-frame failure is closed by exactly
  `Mclip * T(-cameraPosition)`, and the former pure vertical inversion is closed by
  preserving Retail X/Z/W while negating only clip Y before the OpenGL viewport.
  `FUN_0022BF94` remains the oracle for the post-divide GS screen transform; no
  host `Camera3D`, guessed FOV, target/up or host-authored gameplay camera is used.
- **New visual evidence after 12.3A:** the runtime frame is now oriented correctly,
  but it still contains large stretched/crossing triangle corruption that is not
  present in `native_level_viewer`. This is therefore no longer a camera-convention
  blocker. It is a renderer/frontend parity blocker and must be isolated before
  gameplay-response work continues.
- **Renderer-parity rule for the next substep:** viewer and runtime may intentionally
  differ in state acquisition, camera ownership, animation clock, visibility,
  streaming/lifecycle and debug UI, but equivalent canonical render inputs must use
  shared rendering semantics and backend behavior. Geometry/topology conversion,
  texture/material interpretation, vertex/index data, world-transform math,
  cull/depth/blend state, batching and native draw submission must not have
  accidental viewer/runtime forks. Deterministic parity diagnostics should compare
  at least vertex/index counts and hashes, material/texture identity, transform
  hashes, primitive topology, render state and draw ordering. If canonical hashes
  differ, the defect is upstream in materialization/preparation; if they match but
  pixels differ, the defect is downstream in GPU state, resource lifetime or draw
  submission.

### Remaining Phase 12 Steps

- **Step 12.2 (`DONE`):** Windows proves live non-neutral host input reaches
  unchanged `FUN_00217328` continuously and publishes authentic current/edge state
  through the read-only `parsed-live` gate. No host-authored parser outputs or
  DBC/SIF fallback are used.
- **Step 12.3A (`DONE`):** Native presentation foundation. Real Level-0 pixels are
  visible in `openratchet.exe`; absolute-world -> camera-relative composition and
  GS-screen-Y -> OpenGL-viewport conversion are Windows-validated. Release build,
  **28/28 CTests**, strict viewer regression and the mandatory 20-second native run
  are green. The scene is not yet visually accepted because the live runtime still
  shows stretched/crossing triangles absent from the viewer.
- **Step 12.3B (`TODO`, next after this checkpoint commit): Viewer/runtime render
  parity.** Converge all equivalent render operations onto shared semantics/backend
  while preserving only documented intentional frontend differences. Add canonical
  frame parity evidence (counts/hashes/state) and eliminate the unexplained runtime-
  only triangle corruption. Exit requires: no unintended duplicate renderer
  semantics, all remaining frontend differences explicitly classified, equivalent
  geometry/material/transform/render-state data proven equal, the Level-0 viewer
  still visually correct, and the live runtime free of unexplained stretched or
  corrupt triangles.
- **Step 12.3C (`TODO`): Authentic gameplay response.** After render parity is
  closed, trace focused movement/camera input through the already-proved parser to
  the original Retail gameplay consumers and prove resulting Ratchet/camera state
  changes. Do not substitute host-authored movement, camera state or a viewer
  camera.
- **Step 12.4 (`TODO`):** Controller parity/polish required for the phase gate,
  including any Retail-required pressure/rumble semantics only after their
  consumers are proved.
- **Step 12.5 (`TODO`):** Full Phase-12 regression and mandatory Windows runtime
  gate with genuinely playable Ratchet before Phase-12 completion.

The validated 12.3A state is a legitimate checkpoint commit. **Phase 12 remains
`IN PROGRESS`**; the next implementation task is Step 12.3B, not Phase 13.
