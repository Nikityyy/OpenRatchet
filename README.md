# OpenRatchet

OpenRatchet is a native Ratchet & Clank 1 PC-port project. The repository does
not ship copyrighted game data; a user-owned PS2 copy is required.

The long-term architecture is documented in `ARCHITECTURE.md`. OpenRatchet owns
the PC application and progressively replaces PS2 platform services with native
systems. PS2Recomp remains a fallback executor for original EE game logic that
has not yet received a native implementation.

## Bootstrap pipeline

1. `tools/bootstrap.ps1 -Stage Extract` mounts the ISO to copy its EE executable,
   then runs `rac-dvd-toc-parser` against hidden sectors and writes
   `build/extracted` plus `build/toc.json`.
2. Import `build/extracted/PS2_MAIN.ELF` into Ghidra with
   `ghidra-emotionengine-reloaded`, run
   `PS2Recomp/ps2xRecomp/tools/ghidra/ExportPS2Functions.java`, and save the
   generated config as `build/game.toml`.
3. Run `tools/bootstrap.ps1 -Stage Recompile`. The generated PS2Recomp C++ goes
   into `generated/` and is treated as read-only fallback game logic.
4. Configure/build the native host with `tools/bootstrap.ps1 -Stage Build` or
   use the verified incremental helper `tools/build-native.cmd`.

Use `-FetchTools` to clone supported external repositories into `third_party/`.
Ghidra is intentionally not downloaded automatically because the extension must
match the installed Ghidra version.

`third_party/PS2Recomp` is an immutable pinned dependency, not a place for
OpenRatchet-specific edits. The required revision is recorded in
`patches/ps2recomp-base-revision.txt`. During native configuration OpenRatchet
verifies that checkout is both at the pinned revision and completely clean, then
archives committed `HEAD` into `build/native/_openratchet/PS2Recomp` and applies
the repository-owned compatibility patch there. The source checkout therefore
stays byte-for-byte upstream-clean; build-local patched files are disposable
artifacts under the ignored `build/` tree.

`tools/build-native.ps1` also tracks SHA-256 content for dirty build inputs. A
patch-extracted source whose timestamp is older than an existing object is
touched once when its content first changes, but unchanged dirty files are not
touched again on every validation run. This preserves safe ZIP-based patching
without repeatedly invalidating the PCH and rebuilding hundreds of generated
`FUN_*` translation units.

## Current native boundary

`src/runtime/openratchet_runtime.*` is the top-level host owner. Address-based
game replacements are declared through `src/runtime/native_replacements.*`;
legacy boot wrappers are routed through that same boundary until their PS2
subsystems are replaced natively.

`src/platform/native_vfs.*` owns indexed access to extracted WAD/WAD2 content
using `build/toc.json`. It also reconstructs the game-visible disc TOC, so the
loader at `0x12f2b8` no longer needs an IOP/SIF request on the native path. The
game-facing sector reader serves indexed resources directly from host files and
falls back to the generated EE/CDVD implementation only for raw ranges that have
not yet been migrated. `build/toc.json` is therefore a required native runtime
artifact alongside `build/extracted/PS2_MAIN.ELF`. New extractions preserve the
retail TOC's final 19 raw per-level `SectorRange` pairs exactly and store
separately validated host extraction spans under `native_levels`.

`src/assets/wad_decompressor.*` is the first native compressed-asset primitive,
and `src/game/native_assets.*` now owns game function `0x20b618`. Compressed
WAD streams are decoded directly in the host and written to guest RAM; the old
scratchpad/SPR-DMAC copy-and-poll bridge has been removed completely. The game
still receives the original function contract: `v0` is the decompressed byte
count and control returns directly to the caller. The target boot WAD is pinned
to an independently established output fingerprint, and the test suite validates
all 249 compressed streams in the 165-file extracted WAD2 corpus against a fixed
aggregate reference manifest.

PS2Runtime remains an EE/game-logic fallback and temporary compatibility backend,
not the target platform architecture.


### Native level extraction / inspection

Phase 5 adds a focused path for renderer assets. The extractor preserves the
raw retail level-table bytes but discovers actual level file spans from validated
`0x2434` amalgamated headers rather than trusting the raw TOC length field. To
refresh the metadata and extract only the first validated R&C1 level, run:

```powershell
.\tools\extract-native-levels.ps1
```

Use `-Level N` for a particular TOC level or `-All` when the complete native
level corpus is needed. Extracted files live under `build/extracted/levels/`.
`native_level_inspector` parses the original on-disc level envelope, level-data
header and level-core index and validates its compressed core with the native
WAD decoder. The parser now also preserves the decompressed core as a native
host buffer for renderer-owned asset decoders.

### Native level viewer

Phase 6 established the PC-native renderer with authentic R&C1 collision
geometry. Phase 7 moved it onto textured tfrag terrain. Phase 8 added ties,
shrubs and camera-relative sky. Phase 9 extends the same native scene to R&C1
mobys: OpenRatchet parses moby LOD0 packet/vertex-cache data, class texture
remaps and retail gameplay instance transforms, then renders the resulting
bind-pose object meshes through the PC-native path. These formats are converted
directly to host triangles/RGBA images; no VIF/VU/GS emulator is involved.

After extracting level 0 and building Release, run:

```powershell
.\tools\run-native-level-viewer.ps1 -LevelIndex 0
```

The window displays the native R&C1 scene currently covered by the renderer:
textured tfrag terrain, ties, shrubs, bind-pose mobys and sky. Use `TAB` to
toggle wireframe. Click inside the window to capture the mouse. The collision
decoder remains covered by tests as an independent geometry oracle.
`native_level_viewer` is a development microscope only; the shipping port will
render these assets inside the normal OpenRatchet runtime using live game state.
Phase 10 now owns the native R&C1 moby animation pipeline: class/sequence
metadata, dense and sparse pose decoding, retail class+0x14 post-compose,
persistent VU0-style matrix-register semantics reproduced on the CPU, native
skinning, and pose-space interpolation. Level 0 validates all 847 ordinary
skeletal frames through this path. Ratchet
(`oClass 0`) uses the same pose/skinning codec but stores its 134 sequence
pointers in the external core-index table selected by LevelCoreHeader `+0x78`.
Step 12A validates all 2,558 Ratchet frames through the same native skinning
pipeline. Step 12B makes the development viewer animate the rendered Ratchet
instance from the first structurally complete moving external sequence, using
retail pose-space interpolation and a dynamic vertex buffer. The same-sequence
end-of-sequence rule is also evidence-backed: retail `FUN_0020d580` advances
frame B and wraps it to frame 0 while preserving fractional interpolation state,
so the viewer includes the final->first interpolation segment instead of hard
resetting. Absolute sequence timing and live sequence selection remain owned by
the Phase-11 game-state bridge rather than guessed by the viewer. Phase 10 is
complete: Windows acceptance validates all ordinary Level-0 skeletal frames and
all 2,558 Ratchet frames through the native pose/skinning pipeline, plus a
visibly continuous Ratchet loop using the retail final-to-first frame rule.
Phase 11 is complete. Its first boundary was intentionally read-only:
`src/game/rac1_live_state.*` codifies the generated retail live-Moby arena
contract before the native renderer consumes any live state. The generated EE
code proves a 0x4000-byte arena whose base is stored at `0x15FF18`, 0x100-byte
records, a 64-slot hard capacity, and the signed `moby+0x20` traversal state
where exactly `-1` terminates the retail walk. The same contract originally exposed only
proved identity/animation fields (`class +0x24`, `oClass +0xA6`, frame pair
`+0x50/+0x51`, sequence pair `+0x52/+0x53`, interpolation `+0x54`, frame
pointers `+0x68/+0x6C`). Step 11.4 now adds independently proved raw live
world-transform fields while camera ownership remains a separate gate.
Step 11.2 attaches that decoder to the actual fallback runtime rather than to
captured or level-file data. `OpenRatchetRuntime` reads
`PS2Runtime::memory().getRDRAM()` under `PS2Runtime::GuestExecutionScope`, so
live snapshots occur at a coherent host/guest handoff and do not race the EE
fallback thread. Retail now creates the arena itself and Windows acceptance
reaches `[OpenRatchet:live:moby] ... unaccounted=0 status=ok`; the exact record
and terminator counts remain timing-dependent.

Step 11.3 is complete above that proved pool boundary. Live Ratchet animation
selection is refreshed from coherent Retail RDRAM on every host/guest handoff;
only diagnostics are throttled. Phase 10 proved the immutable 134-sequence
Ratchet bank while every original class-local `+0x48` pointer is zero, and
generated `sub_00204790` proves that Retail may append a new class-local sequence
at the old sequence-count slot. The bridge therefore models a 134-entry external
prefix followed by any runtime-local suffix and validates that external prefix
against the native Phase-10 bank.

The final Windows runtime gate proved the exact mixed-storage shape:
`sequenceCount=135`, `externalSequenceCount=134`,
`runtimeLocalSequenceCount=1`. Timing samples observed sequence A `134` and
sequence B as either `0` or `134` while both consumed `moby+0x68/+0x6C` pointers
remained zero. That is a proved pre-materialization construction state, not a
pointer error: `FUN_0020C5F0` can skip the normal `sub_0020C880` resolver when
Ratchet's first local sequence slot is zero, and `sub_00204790` updates the
sequence metadata without writing the consumed endpoint pointers. OpenRatchet
therefore reports `endpoints-not-materialized` and never synthesizes packets from
IDs. When both packets exist, `FUN_0020EDE8` remains authoritative and the native
codec consumes exactly the observed Retail packets; runtime-local, corrected
`0x1AABC0 + frameA*0x800` transition-cache, external and proved direct-repoint
paths are validated separately. `+0x54` is the unchanged Retail blend alpha and
`+0x70` remains uninterpreted.

Windows acceptance is green for Step 11.3: Release links from the prepared
PS2Recomp compatibility source, the pinned third-party checkout remains clean,
CTest is 19/19, the Phase-10 viewer remains fully `status=ok`, and the 20-second
runtime remains alive with graphics activity, `21/21` runtime replacements,
authentic `[OpenRatchet:live:moby] ... unaccounted=0 status=ok`, and the live
Ratchet animation bridge reporting the proved 134+1 storage split. Direct
GCC/Clang regressions cover the materialized packet-to-packet pose path plus the
exact pre-materialization state. The standalone viewer deliberately keeps its
Phase-10 `clock=viewer-demo`; rendered live-state ownership is Step 11.6, not
Step 11.3.

Step 11.4 is complete: the live world-transform contract is bridged without an
axis/scale heuristic. Retail `FUN_0020D868` consumes `moby+0x10` as xyz world
position, and `FUN_0020C5F0` initializes `moby+0x2C` from `class+0x24`;
`FUN_0020CCA8` plus `sub_0020CD48` prove the exact `1/1024` conversion used for
that live raw scale. Orientation is not reconstructed with a host Euler guess:
`FUN_0020DEF8` feeds `moby+0x40` into VU0 and stores the resulting basis columns
at `+0xC0/+0xD0/+0xE0`, then itself evaluates
`basisX*x + basisY*y + basisZ*z + position`. The native bridge therefore uses
those Retail-cached columns directly and transforms a Phase-10 raw skinned point
as `position + basis * (rawPoint * mobyScale/1024)`. An exactly zero cached basis
is retained as `basis-not-materialized`, because Retail zeroes the complete Moby
before that basis producer runs. Runtime transform state is sampled coherently
alongside the Step-11.3 animation state and exposed under
`[OpenRatchet:live:ratchet-transform]`; no native renderer ownership moves until
Step 11.6. The Step-11.4 code is locally green across the 20 CTest-equivalent
targets available in the audit snapshot plus GCC/Clang `-Werror` compilation of
the runtime translation unit. Windows acceptance is green as well: **20/20**
CTests pass, the Phase-10 viewer remains unchanged, `third_party/PS2Recomp`
stays clean, runtime replacements remain `21/21` with `install_errors=0`, and
the 20-second live run reports one authentic Ratchet with `unaccounted=0` plus
`[OpenRatchet:live:ratchet-transform] ... status=basis-not-materialized`. That
zero-basis snapshot is the proved Retail pre-materialization state, not a host
rotation fallback or a startup blocker.

Step 11.5 is complete at a separate Retail camera boundary. The camera state is
the fixed 0x3A0-byte global rooted at `0x00186F40`, independently zeroed by
`sub_001E9B10`. `FUN_0020D868` proves `+0x140` as the world-space observer
position; `FUN_001ED2B0`/`sub_001EDAA8` publish selected orientation vectors at
`+0x350/+0x360/+0x370`, with `FUN_00218D10` providing the independent
`(256,256,64)` plus identity-orientation initialization oracle. The native bridge
does not derive a guessed Raylib camera or FOV. Instead it preserves Retail's
materialized clip vectors at `+0x100/+0x110/+0x120/+0x130`, whose exact consumer
`FUN_0022BF94` computes `clipX*x + clipY*y + clipZ*z + clipW*w` before clipping
and perspective division. `rac1_live_camera_tests` pins those addresses and the
matrix direction; `[OpenRatchet:live:camera]` exposes the coherent live state.
Windows acceptance is green: Release links, **21/21 CTests pass**, the Phase-10
viewer remains fully regression-free, `third_party/PS2Recomp` remains clean,
runtime replacements are still `21/21` with `install_errors=0`, and the 20-second
run reports the authentic all-zero construction state
`status=orientation-not-materialized`. This is preserved as pre-materialization
evidence, not replaced by a fake identity camera/FOV.

Step 11.6 is complete. Step 11.6A establishes the narrow final-frame
ownership cut without deepening GS emulation: the existing PS2Runtime host draw
callback occurs after the compatibility framebuffer draw has been queued and
before `EndDrawing`. OpenRatchet flushes that old batch first, clears it, and
then owns the visible frame. `third_party/PS2Recomp` is not modified. The common
Phase-10 texture/mesh upload and draw helpers now live in
`src/render/native_mesh_renderer.*` and are used by both the standalone viewer
and `openratchet.exe`, avoiding a second renderer implementation.

Runtime scene selection is tied to proved Retail identity rather than a hardcoded
demo level. Successful complete native indexed reads can publish their exact
asset; currently only the independently proved Level-0 initialization request
`wads2[69]` (`0x38F6`, `0x834` sectors -> `0x01654000`) maps to native Level 0.
Partial, neighboring or differently targeted reads stay unmapped. Once mapped,
the runtime reuses the authoritative
native tfrag/tie/shrub decoders in raw Retail coordinates. If the Step-11.5 camera
clip transform is materialized, its four columns are loaded directly in the
proved `clipX*x + clipY*y + clipZ*z + clipW*w` order. If not, rendering remains
explicitly deferred: no Raylib demo camera, FOV, identity transform, pose-bank
substitution or axis guess is injected. Sky and live Moby/Ratchet drawing remain
separate 11.6 work until their runtime renderer-facing identity/transform
contracts are proved. Step 11.6A is Windows-validated: Release links, **22/22**
CTests pass, the Phase-10 viewer remains regression-free, PS2Recomp stays clean,
and the 20-second runtime maps/materializes native Level 0 with `unaccounted=0`.
The sampled Retail camera remains `orientation-not-materialized`, therefore the
native-owned frame correctly stays black with `rendered=0 deferred=1`; no fake
viewer camera is introduced merely to force visible output.


Step 11.6B1 now maps Ratchet's renderer identity exactly by Retail `oClass=0`
and same-guest-Moby agreement between the live animation and transform bridges.
The sampled `moby+0x24` class pointer is deliberately not used: native
`classOffset=0x739540` at core base `0x654000` resolves to `0xD8D540`, not the
live `0xA9E340` pointer. The Phase-10 dynamic Ratchet path is shared rather than
reimplemented: `src/render/native_skinned_moby_renderer.*` drives the exact
4-batch / 6,856-triangle / 4,026-skin-vertex `skinVertexIndex` topology in both
viewer and runtime. Production runtime updates occur only when the actual
Step-11.3 pose packets and Step-11.4 basis are both materialized; otherwise the
Ratchet remains mapped-but-deferred. Windows acceptance is green: Release builds,
22/22 CTests pass, the viewer retains the exact Ratchet visual gate, and the stable
20-second runtime reports `liveMobyMapped=1`, `ratchetIdentity=ok`, zero Moby/pool
unaccounted state and `status=ok` while pose, basis and camera remain authentically
unmaterialized. PS2Recomp remains clean.

Step 11.6B2A extends exact class identity across the complete live Retail pool.
Its first Windows gate exposed the distinction between **runtime identity** and
**native level topology** (`oClass=1905` is live-valid but absent from Level 0's
125 native class entries). A second Windows gate then caught a table mix-up: all
5/5 active Mobys mismatched when `moby+0x24` was compared to `0x1B3580[slot]`.
Retail `FUN_0020C5F0` proves the corrected oracle. It maps `oClass` through the
0x800-byte table at `0x1B3AC0`, reads the class-data pointer from
`0x1B3200[slot]`, and stores that pointer at `moby+0x24`. The separate
`0x1B3580[slot]` word goes to `moby+0x74` and is not used as class identity.
OpenRatchet now snapshots the `0x1B3200` path coherently with the live pool and
requires exact class-data pointer agreement. The Level-0 catalog remains a
second-stage topology classification: 125 unique native classes = 14 renderable,
7 intentionally invisible and 104 class-only. A registry-valid class outside
that level catalog is explicitly `runtime-only` with rendering deferred, not
falsely missing. Unregistered IDs, out-of-range IDs, class-data pointer mismatches
and accounting mismatches remain hard errors. No Phase-10 static-instance/
proximity mapping is introduced. Windows revalidation is green: 22/22 CTests
pass and the stable five-Moby sample resolves to 3 renderable-topology + 1
class-only + 1 runtime-only with `liveMobyMapped=5`, zero registry failures,
`liveMobyUnaccounted=0`, `poolUnaccounted=0` and `status=ok`.

Step 11.6B2B implements the Retail sky transform without using the viewer's
camera-relative placement. `FUN_0022B288` consumes the sky pointer at
`0x0016045C`, base angle at `0x00160404` and translation qword at `0x00160460`,
then rebuilds a separate object matrix for every shell in transient scratch at
`0x001D96E0` immediately before the normal Retail renderer consumes it. The
native bridge reconstructs those matrices from stable inputs, reproducing
Retail's Z->Y->X rotation sequence (X is zero for this path), per-shell scale,
angle wrapping and sine polynomial in binary32; tests pin the matrices bitwise
and prove that garbage in the scratch matrix cannot affect the result.

The first Windows provenance run proved an important source split: the standalone
Phase-8 viewer/core sky is **5 shells / 118 clusters / 2,366 triangles / 8
textures / 7 batches**, while the actual live gameplay resource at `0x714640` is
**4 shells** with signatures `(38,1),(25,0),(3,0),(24,0)`. They are different
Retail resources, not a 4-to-5 indexing convention. The exact active loader
`sub_001EA830` identifies the runtime source inside the same indexed `wads2/69`
load used by gameplay: the `0x41A000`-byte asset has encoded WAD size `0x419F6E`,
decompresses to `0x7318C0` bytes, and Retail computes the gameplay-sky offset as
`*(wad+0x04) + *(wad+0x14) = 0x5C000 + 0x64640 = 0x0C0640`. With runtime output
base `0x654000`, that is exactly `0x714640`. Native decoding at `0x0C0640` yields
**4 shells / 90 clusters / 1,472 triangles / 8 textures / 5 GPU batches** and
reproduces all four live shell signatures.

The runtime renderer now uses that exact WAD2 gameplay-sky topology while the
viewer remains on the established core-sky path. The shared decoder preserves
`shellIndex`, each shell's source offset, and `(clusterCount,flags)` provenance,
batches by `(shellIndex,materialIndex)`, and the runtime restores the original
signed-16 XYZ domain with exact `*1024` before applying the Retail object matrix.
Shell identity requires `liveSkyBase + nativeSourceOffset == liveShellGuestPointer`
as well as matching counts/flags before transform deferral, so
`translation-not-materialized` is accepted only when live and native runtime-WAD
resources already match exactly.
Sky accounting remains independent from the static-world gate: an unmaterialized
sky translation defers only sky, while a valid Retail camera may still render
terrain/ties/shrubs. Windows acceptance is green: Release builds, **23/23 CTests**
pass, the standalone viewer remains exact on its separate 5-shell core-sky path,
and the 20-second runtime remains alive with graphics activity and harness exit 0.
The stable live gameplay sky is the 4-shell resource at `0x714640`; its four
pointers/counts/flags pass the runtime-WAD source join before
`translation-not-materialized` is accepted, giving `skyMapped=1`,
`skyUnaccounted=0` and overall renderer `status=ok`. The live Moby pool remains
fully accounted (`liveMobyMapped=5`, `liveMobyUnaccounted=0`,
`poolUnaccounted=0`), while the authentic camera/pose/basis construction states
remain deferred. `third_party/PS2Recomp` is clean. This same Windows run is the
final Phase-11 regression gate, so **Phase 11 is complete**; Phase 12 is the next
roadmap milestone.


Phase 11 has now promoted the controller and save bootstrap above SIF instead
of extending the temporary RPC-response table. Retail WAD2/0 descriptor 17
(`PsIIdbcman  2500`) remains the oracle that proved the DBC success state, but
the runtime no longer needs to bind or service `0x80000900`/`0x8000091B` during
startup. A native replacement for `sub_00124510` reproduces exactly the proved
0xC0 bytes of successful EE-visible DBC state at `0x15B480..0x15B53F`; the
surrounding original game code remains intact. `FUN_00124718` is replaced only
at the platform transaction boundary and supplies the retail first-free link
slot to the original `sub_00124A88` state initialization.

The next observed blocker, service `0x80000400` function `0xFE`, is the PS2
memory-card initialization path. Rather than adding MCSERV packets, Phase 11
owns the game wrapper `FUN_0020AC58`: its successful path merely allows startup
to continue, while real card enumeration/save/load remains deliberately deferred
to the Phase-19 native save backend. DBCMAN/MCSERV startup rows are therefore
removed from the legacy SIF resolver. Diagnostics expose these migrations under
`[OpenRatchet:platform]` so a runtime regression can prove that startup uses the
native HLE boundary instead of hidden packet synthesis.

Windows acceptance of that platform-bootstrap step is now complete: 16/16 tests
pass, the Phase-10 native viewer remains unchanged, and runtime startup reaches
the next custom bind pair `0x00123456/0x00123457`. Static inspection identifies
that pair as Sony 989snd: `FUN_0012DA28` contains the original
`/usr/local/989snd/ee/989snd.c` source path and performs both binds. OpenRatchet
therefore promotes audio in the same way instead of adding another RPC case.

The Phase-11 audio boundary owns the game startup wrapper `sub_0022C8D0` and
reproduces only its direct game-visible initialization state (plus the direct
manager state from `FUN_00215390`). All 989snd/SPU/IOP audio work is deferred to
Phase 18 NativeAudio. The `0x00123456/0x00123457` startup bind rows are removed
from legacy SIF, and negative tests make that ownership permanent. Windows
acceptance for that bootstrap is complete. After the next 11.2E storage boundary,
the suite is 18/18 and the runtime reports 15/15 replacements with no install
errors.

The observed `pc=0x216858` was traced past that bootstrap instead of being
answered with another RPC packet. It is the wait loop inside retail
`sub_00216828`, a synchronous game-facing sector loader implemented on PS2 via
989snd. The startup call is exact: `sub_001E9338` asks it to read TOC entry
`wads[0]` (`start=1506`, `length=9`) into guest `0x1AABC0`; the helper's return
value is `sectorCount << 11`, proving the 2048-byte-sector contract. The
extracted WAD0 is exactly 18,432 bytes. Phase 11.2E therefore lifts
`0x216828` to the existing `NativeVfs::readSectors` boundary. Windows verifies
the exact `0x5E2/0x9 -> 0x1AABC0` transfer with `status=ok`; the old blocker is
gone. This bypasses only the PS2-specific 989snd transport, preserves the
original callers and downstream level/game initialization, does not fabricate
the Moby pool, and has no hidden 989snd/SIF fallback for unresolved ranges.

The next Windows blocker, `pc=0x11BA40`, was then resolved as Sony FILEIO
initialization retrying a BIND to service `0x80000001`. Retail reaches FILEIO
here only to read `rom0:ROMVER` for two platform-identification/capability
queries: `libgraph::checkModelVersion` (`0x121A18`) and `libscf::IsT10K`
(`0x12D200`). OpenRatchet now answers those questions at their native semantic
boundary instead of adding FILEIO RPC synthesis: syscall `0x80`
`_GetGsDxDyOffset` is unavailable in the fallback host, and the retail PC port
is not a Sony DTL-T10000 development TOOL. Local full-link/CTest validation is
18/18; the Retail-extract run installs 17/17 runtime replacements, executes the
native `IsT10K` probe, emits no FILEIO `0x80000001` bind, and advances past
`0x11BA40`. At that intermediate checkpoint the Moby pool was still
observation-only; the later Phase-11.2 cleanup now reaches
`[OpenRatchet:live:moby] ... unaccounted=0 status=ok` authentically, which is the
proved boundary on which Step 11.3 builds.

Phase 11.2 is now structurally complete. The remaining startup work was resolved
at high-level native boundaries rather than by extending packet emulation: the
game memory-card preflight selects its existing non-blocking result until Phase
19, the complete MPEG/movie wrapper is deferred to Phase 17, and the level
sound-bank wrapper is deferred to Phase 18. The asynchronous game-sector helper
`FUN_00216788` (`0x216788`) now shares the same `NativeVfs::readSectors` ownership
as synchronous wrapper `0x216828`; the decisive Level-0 request is exactly
`wads2[69]` (`0x38F6`, `0x834` sectors, `0x41A000` bytes).

A separate PS2Recomp runtime defect was fixed at its owning runtime boundary:
synchronous DMAC handlers previously used a synthetic top-of-RDRAM stack that
exactly overlapped Ratchet's declared 0x4000-byte main-thread stack. They now
inherit the interrupted guest SP while retaining isolated registers, with a
dedicated SIF-DMA regression that fails before and passes after the correction.
OpenRatchet carries that correction as a repository-owned compatibility patch
applied only to a build-local archive of the pinned, clean PS2Recomp checkout;
`third_party/PS2Recomp` itself remains untouched. With the corruption removed,
Retail itself reaches `sub_001E9B10`, publishes the authentic Moby arena globals
`0x15FF18/0x15FF20`, and `[OpenRatchet:live:moby]` reaches `status=ok` with
`unaccounted=0`. No Moby pool or pool globals are synthesized. Step 11.3 can now
drive native animation selection from those live Retail records.

AI-assisted repository work follows [`AI_WORKFLOW.md`](AI_WORKFLOW.md): maximize
verified progress per change, prefer shared root causes over serial symptom fixes,
and stop startup archaeology as soon as the current semantic milestone gate is
reached.

## Current prerequisites

Required: Python 3, CMake 3.21+, a C++20 compiler, Java/Ghidra, and a PS2Recomp
checkout. The bootstrap defaults to
`C:\ghidra_12.1.2_PUBLIC_20260605\ghidra_12.1.2_PUBLIC`; use `-GhidraDir` if it
moves.

The matching `ghidra-emotionengine-reloaded` extension is detected from
Ghidra's per-user extension directory under
`%APPDATA%\ghidra\<version>\Extensions`.

The build enables MSVC `/MP` and uses all detected processor cores by default.
Override with `-Jobs N` if memory pressure becomes a problem.
