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
| Phase 11 | `TODO` | Live game-state & camera bridge | Native renderer continuously follows running recompiled game logic and gameplay camera. |
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

## Active Work: Phase 10 — Skeletal & Model Animation

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

- **Retail correction (proved):** `moby +0x52 == 0xFF` is a generic animation-transition/cache-pose state, not a dedicated Ratchet sequence format. `FUN_00212f90` obtains one of 16 transition slots, asks `FUN_0020ede8` to evaluate the current pose into that slot, and `sub_0020C880` later resolves the cached frame at `0x1BABC0 + slot*0x800`. Do not use this state as the player-bank discriminator.
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
