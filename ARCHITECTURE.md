# OpenRatchet native-port architecture

OpenRatchet is a native PC port, not a title-specific PS2 emulator.

## AI engineering workflow

AI agents working on this repository must also follow [`AI_WORKFLOW.md`](AI_WORKFLOW.md).
That document governs investigation strategy and patch scope; it does not override
this architecture or `MILESTONES.md`. In particular, prefer root-cause fixes and
high-level native boundaries over serial transport/symptom patches.

## Ownership rule

The native OpenRatchet host owns the application. PS2Recomp is a fallback
executor for original EE game logic that has not yet been replaced. It is not
the long-term owner of filesystem, rendering, input, audio, saves, or other
platform services.

```text
user-owned Ratchet & Clank ISO
            |
      extraction/indexing
            |
   +--------+---------+
   |                  |
recompiled EE      native assets
 game logic           |
   |                  |
   +--------+---------+
            |
      OpenRatchet host
   +--------+---------+-------------------+
   |        |         |         |         |
 native   native    native    native    native
  VFS    renderer   input     audio     saves
```

## Recompiled game-function boundary

A game function has two possible implementations:

1. a project-owned native replacement, when its semantics are understood; or
2. the PS2Recomp-generated implementation as the fallback.

All address-based replacements are declared through
`runtime::NativeReplacementRegistry`. Generated output remains read-only.
Legacy compatibility wrappers are temporarily declared through the same
boundary so they can be removed subsystem-by-subsystem instead of being hidden
inside startup code.

## Migration rules

- Preserve original game logic unless a native replacement is deliberate and
  evidence-backed.
- Prefer replacing a PS2-facing game/platform API over emulating the hardware
  below it.
- Filesystem work migrates to a native ISO/extracted-data VFS; do not add new
  sector-specific startup injections.
- Rendering migrates to Ratchet-aware native asset/render paths; a working PS2
  GS framebuffer is not a prerequisite for the PC renderer.
- PCSX2/PS2 hardware implementations may be reference or differential oracles,
  but are not shipping architecture.
- Do not hand-edit generated PS2Recomp output to fix a game function. Fix the
  recompiler/metadata or install a root-owned native replacement.
- Every migration phase must leave a deterministic build/test boundary and be
  independently commit-safe.

## Native storage boundary

`platform::NativeVfs` indexes extracted WAD/WAD2 resources from
`build/toc.json` and reconstructs the game's in-memory disc TOC. The game-facing
synchronous sector reader at `0x12f208` is owned by OpenRatchet: ranges backed
by indexed extracted resources are read directly into guest memory from host
files. The TOC loader at `0x12f2b8` is also native and copies the host-derived
table directly to the game's fixed TOC region instead of asking the IOP for the
0x2960-byte blob. Unknown/raw disc ranges still fall back to the generated
EE/CDVD path until their semantics are migrated.

The retail TOC's final 0x98 bytes are preserved exactly as 19 raw level
`SectorRange` entries for the game-visible TOC. Those raw pairs are **not** used
as host-file extents. Native level extraction independently scans TOC sector
references for an authentic `0x2434` amalgamated level header, validates its
level-data/gameplay/occlusion ranges, and records the resulting contiguous span
under `native_levels`. `tools/extract-native-levels.ps1` can therefore extract a
selected level directly from the user's ISO without trusting malformed or
non-file-size values in the retail tail. Older `toc.json` files remain usable:
their 0x28c8-byte known prefix is reconstructed exactly, and the earlier
temporary `leveldirs` representation is accepted during migration.

This boundary is intentionally above CDVD/SIF hardware. New known resources
should be added to the VFS/resource layer rather than implemented as synthetic
CDVD responses or sector-specific guest overrides.

Phase 11 extends the same ownership rule to both game-facing sector-loader
boundaries built on Sony 989snd: `FUN_00216788` (`0x216788`) starts an
asynchronous load and `sub_00216828` (`0x216828`) wraps that helper in a
synchronous wait. Both expose the same `(destination, sourceSector, sectorCount)`
contract and the helper's successful result is `sectorCount << 11`. The first
startup request is `wads[0]` (`0x5E2`, 9 sectors) into `0x1AABC0`; the later
level-init request is exactly TOC `wads2[69]` (`0x38F6`, `0x834` sectors) into
`0x01654000`, matching the extracted `0x41A000`-byte file byte-for-byte.
OpenRatchet therefore owns both `0x216788` and `0x216828` through the existing
`NativeVfs::readSectors` boundary rather than implementing 989snd transport.
Complete indexed ranges are copied atomically and return the same
`sectorCount * 0x800` byte count; unresolved ranges return retail failure `0`
without partial guest-memory mutation or hidden 989snd/SIF fallback. The
transport-private 989snd manager state at `0x1516D0` is deliberately untouched.

The boot WAD is also an authoritative forensic source for game-shipped IOP
modules when a PS2-facing API boundary must be characterized. WAD2/0 descriptor
17 contains this build's `PsIIdbcman  2500` IOP ELF. Static analysis of that
module proved the successful DBC initialization work snapshot and first-free
16-slot link allocator. Phase 11 uses those facts to move the boundary *above*
SIF: DBC startup is now native HLE, not a permanent implementation of service
`0x80000900`. The IOP module remains an oracle, not shipping architecture.

## Temporary legacy layer

`src/guest_overrides.cpp` is retained only to preserve the current verified
boot while native subsystems are introduced. It still contains known technical
debt: SIF response synthesis, address-specific control-flow repair, callback
bridges, and graphics diagnostics. New platform services must not be added to
that table one RPC at a time. Once a service is identified as controller/input,
save, audio, video, filesystem, or another host-owned subsystem, migration must
move upward to the narrowest proved EE/game API boundary and remove the
corresponding synthetic SIF rows.

Phase 11 applies that rule to DBCMAN and MCSERV. Their startup bind/response rows
have been removed from `sif_startup_responses` / `sif_rpc_transport`; future
calls to those services therefore cannot be silently fabricated. The WAD
decompressor's scratchpad/SPR-DMAC bridge was already deleted, and host WAD file
I/O/decompression likewise no longer belongs in this compatibility layer.

## Native platform-bootstrap boundary

`game::declareNativePlatformBootstrapReplacements` owns the Phase-11 bootstrap
handoff for platform APIs that should never become full IOP emulation:

- `sub_00124510` (`0x124510`) is the retail DBC initialization wrapper. Static
  EE + retail DBCMAN analysis proves that its successful game-visible state is
  the initial 0x80-byte zero work snapshot at `0x15B480` followed by the
  16-word EE state table at `0x15B500..0x15B53F`. The native replacement writes
  exactly this contiguous 0xC0-byte zero state, resets the native 16-slot link
  allocator, returns the retail success value `1`, and performs no SIF bind or
  RPC.
- `FUN_00124718` (`0x124718`) is only the synchronous DBC link-allocation RPC
  wrapper. The surrounding generated `sub_00124A88` continues to perform all
  original EE-side descriptor/link-object initialization. The native boundary
  therefore replaces only the platform transaction and returns the retail
  first-free slot from a 16-entry allocator.
- `FUN_0020AC58` (`0x20AC58`) is the game's memory-card startup wrapper. Its
  successful path has no game-side state effect beyond allowing startup to
  continue, and its caller ignores the result. Phase 11 bypasses that PS2
  memory-card initialization explicitly; actual save/load semantics remain a
  Phase-19 `NativeSave` responsibility rather than growing an MCSERV emulator.
- `sub_00209168` (`0x209168`) is the later game-level memory-card preflight.
  Retail issues libmc `sceMcGetInfo`, `sceMcSync` and `sceMcGetDir` for the
  Ratchet save directory; its own result `0` is the existing non-blocking path,
  while warning states eventually wait for a new controller-button edge. Since
  persistent saves remain Phase 19, Phase 11 selects that proved retail `0`
  outcome at the wrapper boundary without fabricating card data or pad input.
- `FUN_0023A3B8` (`0x23A3B8`) is the complete synchronous movie/MPEG wrapper
  (including `sceMpegInit`/teardown). Movie playback belongs to Phase 17, so
  Phase 11 takes the wrapper's proved completed result `0` with no guest-memory
  side effects, MPEG state, input consumption or SIF fallback.
- `libgraph::checkModelVersion` (`0x121A18`) is not a general filesystem
  request: Retail opens `rom0:ROMVER` only to decide whether kernel syscall
  `0x80` (`_GetGsDxDyOffset`) exists. The bundled fallback runtime does not
  implement syscall `0x80`, so the native host-capability probe returns false
  and preserves libgraph's own older-kernel fallback without inventing a BIOS
  image or FILEIO service.
- `libscf::IsT10K` (`0x12D200`) likewise opens `rom0:ROMVER` only through
  `GetRomName` to test whether byte four is `T`, the Sony DTL-T10000 development
  TOOL profile. OpenRatchet runs the retail-game host profile, so the native
  platform probe returns false. This bypasses a platform-identification file
  read at its semantic API boundary instead of implementing FILEIO service
  `0x80000001` through SIF.

This is intentionally different from accepting arbitrary SIF calls. If later
gameplay reaches a real controller read or save/load operation before its native
subsystem exists, that becomes the ownership boundary for Phase 12 or Phase 19;
it is not answered by another captured packet row.

## Native audio-bootstrap boundary

The same ownership rule applies to Sony 989snd. Generated `FUN_0012DA28` is not
a game protocol: it is the bundled EE 989snd library, proven by its embedded
`/usr/local/989snd/ee/989snd.c` source path and its binds to the custom
`0x00123456` / `0x00123457` RPC services. OpenRatchet therefore does not add
those services to the legacy SIF transport.

Phase 11 owns the game-level startup wrapper `sub_0022C8D0` (`0x22C8D0`) through
`game::declareNativeAudioBootstrapReplacements`. The native replacement
reproduces the direct EE/game-visible state mutations performed by that wrapper
and by its game-side manager initializer `FUN_00215390`, including the existing
configuration-derived mixer fields. The later level-init wrapper `sub_0022D708`
is also owned at this same boundary: retail synchronizes CD then enters
`snd_BankLoadByLoc`; Phase 11 selects its existing no-bank result `0` while
actual bank loading/streaming remains Phase 18. Calls into 989snd, SPU state, IOP
sound banks and sound output are deliberately absent. This lets original game
initialization proceed without turning the temporary SIF compatibility layer
into an audio emulator.

The legacy bind rows for `0x00123456` and `0x00123457` are removed. Tests assert
that both startup bind synthesis and direct RPC synthesis remain unsupported, so
a future audio dependency must be handled at the native audio API boundary rather
than reintroduced as packet-specific behavior.

## PS2Recomp synchronous-DMAC interrupt-stack boundary

Phase-11 startup exposed a runtime defect below the game: Ratchet configures its
main-thread stack as `[0x01FFC000, 0x02000000)`, while PS2Recomp's synthetic
`reserveAsyncCallbackStack(0x4000)` selected exactly that same top-of-RDRAM
range for synchronous DMAC handlers. A legitimate `_SifCmdIntHandler` frame
therefore overwrote the interrupted game's saved return address. This is a
PS2Recomp/runtime defect and is **not** hidden with a Ratchet HLE. Synchronous
DMAC completions raised from guest execution must inherit the interrupted guest
SP while retaining an isolated register context; genuinely asynchronous paths
keep their existing managed-stack behavior when no interrupted context exists.
The SIF-DMA regression carried with the fix fails before the correction and
passes after it, verifying that the handler frame lies below the interrupted SP
without corrupting the caller frame.

OpenRatchet does not modify the pinned checkout under `third_party/PS2Recomp` to
carry that correction. `patches/ps2recomp-synchronous-dmac-interrupt-stack.patch`
is an upstream-ready compatibility patch against the exact revision recorded in
`patches/ps2recomp-base-revision.txt`. CMake requires that checkout to be clean,
archives its committed `HEAD` into `build/native/_openratchet/PS2Recomp`, applies
the patch only to that disposable build-local copy, and builds `ps2_runtime`
there. A revision drift, dirty checkout, or failed patch context is a hard
configuration error. This keeps third-party source state immutable and makes the
temporary runtime correction explicit, reproducible and removable when upstream
contains an equivalent fix.

## Native compressed-asset boundary

`assets::decompressWad` is a PS2-independent implementation of the R&C1 WAD
stream semantics used by game function `0x20b618`. It operates only on byte
spans: no scratchpad, SPR DMA, CHCR polling, or PS2Runtime device state is part
of the decoder API.

OpenRatchet now owns `0x20b618` through `game::declareNativeAssetReplacements`.
The replacement copies the encoded stream to host memory, decodes directly into
the caller's guest-RAM output region, returns the decompressed byte count in
`v0`, and jumps back to the original caller. Copying the encoded source before
decoding deliberately preserves correctness for overlapping guest input/output
ranges, matching the old staging behavior without emulating the staging device.

The removed legacy bridge manually filled PS2 scratchpad, synthesized SPR DMA
completion, polled generated wait PCs up to 200,000 times, and produced a known
incorrect boot-WAD result. It is no longer reachable or registered. Correctness
is instead anchored to an independent boot-WAD oracle and a regression manifest
covering all 249 compressed streams across the extracted 165-file WAD2 corpus.

## Native R&C1 level boundary

`assets::loadRac1LevelCore` is the renderer-facing entry point for a validated
R&C1 native level span. It locates the original 0x2434 on-disc level header at
its preserved absolute header sector inside that span, converts the header's
absolute sector ranges to offsets in the extracted level file, reads the
level-data byte-range header, parses the level-core index, and returns the
natively decompressed core bytes. `assets::inspectRac1Level` remains the
metadata-only convenience wrapper.

The LevelCoreHeader table pairs are represented correctly as `{count, offset}`
on disc and normalized to `{offset, count}` in host types. This matters for the
renderer: class/texture table offsets were previously being displayed as counts.
Core offsets such as `tfrags == 0` are valid; R&C1 can place the tfrag block at
the beginning of the decompressed core.

## Native visual boundary

`assets::decodeRac1Collision` remains an independent decoder for the level
collision octree and provides a useful geometry oracle without touching VIF,
VU, GIF, GS or guest memory.

The visual scene path is renderer-owned. `assets::decodeRac1TfragTerrain`
parses the retail tfrag block, walks its five embedded VIF command buffers as
serialized asset packets, reconstructs LOD0 vertex/position/strip streams, and
emits ordinary host triangles grouped by texture material. It does not execute
VU microcode or emulate VIF state beyond the packet layout required to read the
stored data.

`assets::decodeRac1PaletteTextures` decodes any R&C1 LevelCore paletted texture
table (tfrag/tie/moby/shrub) from core pixel indices plus the extracted GS-RAM
CLUT blob into ordinary RGBA8 host images. `loadRac1LevelCore` returns the
complete core index and GS-RAM blobs in addition to the decompressed core.

Phase 8 also makes the retail NTSC gameplay WAD renderer-owned. The level loader
natively decompresses it into a host buffer. `assets::decodeRac1StaticScene`
joins the LevelCore tie/shrub class geometry to the gameplay instance blocks,
applies their retail 4x4 matrices, resolves class-local texture slots through
the class texture maps, and emits world-space host triangles. Tie/shrub packet
formats are decoded as serialized asset data; no VU/GS execution is involved.

`assets::decodeRac1Sky` parses the level-core sky header, shells and clusters,
including camera-relative geometry, vertex colours, and the sky block's own
paletted textures. The resulting shell meshes and RGBA8 images are native
renderer resources.

`assets::decodeRac1MobyScene` owns the first dynamic-object model boundary. It
reads R&C1 moby class entries, LOD0 packet VIF storage, the persistent 512-slot
vertex cache, duplicate-cache references and packed material/index streams,
then lazily decodes the classes referenced by gameplay and joins those meshes
to moby scale/rotation/position/colour instances. Output is ordinary world-space
host triangles grouped by the global moby texture table. Phase 9 intentionally
emits the stored bind pose. Phase 10 adds a separate native animation layer:
`assets::inspectRac1MobyAnimationMetadata` validates each referenced class's
rig, sequence/frame layout and packed skinning program; the pose evaluator then
decodes dense/sparse joint transforms, applies the retail class+0x14
post-compose, and executes the R&C1 matrix-transfer / 2-way / 3-way / main
skinning semantics directly on the CPU with persistent matrix-register state.
The renderer therefore consumes ordinary host vertices rather than emulating
VU0. Ratchet (`oClass 0`) is a storage exception, not a codec exception:
LevelCoreHeader `+0x78` selects a core-index-resident table of external sequence
pointers, while the pointed-to sequences use the same pose/skinning pipeline.

`native_level_viewer` links to raylib/OpenGL and combines tfrags, ties, shrubs,
bind-pose mobys and sky into one PC-native scene. The PS2 GS framebuffer, VU
execution and GIF stream are not part of this path. The viewer remains a
development tool rather than a shipping frontend; after native moby animation,
the architectural boundary is feeding these renderers from the running
recompiled game's camera and object state. Exact lighting, LOD, CLAMP, fog and
transparency refinements can be layered on without changing native asset
ownership.

## Live simulation-state bridge boundary

`game::inspectRac1LiveMobyPool` is deliberately runtime-agnostic: it accepts a
read-only guest-RDRAM byte span and decodes only retail-proved R&C1 Moby-pool
fields. The decoder owns no PS2Runtime object or renderer. Step 11.4 extends the
raw contract with the proved live world-position (`+0x10`), raw model scale
(`+0x2C`), Retail rotation input (`+0x40`) and cached basis columns
(`+0xC0/+0xD0/+0xE0`); camera interpretation remains outside this boundary.

During Phase 11, `OpenRatchetRuntime` is the owner of the temporary attachment
to the still-running EE fallback. It obtains the actual 32 MiB RDRAM pointer
through `PS2Runtime::memory().getRDRAM()` and snapshots it under
`PS2Runtime::GuestExecutionScope`. That scope uses PS2Runtime's existing
function-boundary handoff, so host reads do not race guest writes. Snapshot
formatting/logging is done only after releasing the execution scope.

PS2Runtime's host-presentation callback is used temporarily as the host/guest
handoff clock while the fallback runtime still owns the executable loop. This
is not a new rendering dependency and does not make the callback part of R&C1
game semantics. Phase 11.3 refreshes the semantic live-animation selection on
every coherent callback; only stderr diagnostics are throttled. The callback
exists solely to observe live guest state until the native application loop
takes over later phases. Phase 11.2 keeps the pool bridge read-only: it never
fabricates RDRAM pool globals, transforms, or camera state. After the proved
platform/transport prerequisites above were lifted to their native boundaries,
Retail itself reaches `sub_001E9B10`, publishes `0x15FF18` and `0x15FF20`, and
the decoder reports `[OpenRatchet:live:moby] ... status=ok` with
`unaccounted=0`.

Completed Phase 11.3 adds a separate runtime-agnostic animation contract above that pool.
`game::inspectRac1LiveRatchetAnimation` requires exactly one traversed
`oClass==0` record and preserves Ratchet's two proved sequence-storage domains.
Phase 10 established that the immutable player bank contains 134 external
sequences while all 134 class-local `class+0x48` entries are zero.
`sub_00204790` proves that Retail can extend that class at runtime: it increments
`class+0x0c`, assigns the old count to the live sequence IDs, writes the new
sequence pointer at `class+0x48+oldCount*4`, and converts that appended sequence's
frame entries to absolute pointers. The live class table is therefore interpreted
as a leading zero external-bank prefix followed by a runtime-local suffix; the
native external bank provides an independent count/frame oracle for that prefix.

`sub_0020C880` remains one producer for runtime-local endpoints and proves the
`sequenceA==0xFF` transition cache at
`0x1AABC0 + frameA*0x800` (`0x1B0000 + sign_extend(0xABC0)`). It is not a
universal Ratchet resolver. `FUN_0020C5F0` skips the initial call to
`sub_0020C880` when the first class-local sequence pointer is zero, and
`sub_00204790` appends sequence metadata without writing `moby+0x68/+0x6c`.
Thus a coherent live record may already expose valid sequence/frame identity
while both consumed endpoint pointers are still zero. OpenRatchet records that
exactly as `endpoints-not-materialized`; it is not fabricated into `status=ok`
and no native-bank fallback is synthesized. Windows Step-11.3 acceptance proved
this state directly with `sequenceCount=135`, an external prefix of 134 and one
runtime-local appended sequence. Timing samples observed the appended ID 134 on
A and on B as either external ID 0 or appended ID 134 while both endpoint
pointers remained zero. The bridge treats those as coherent identity-only
construction states rather than pointer failures.

Once both pointers are materialized, `FUN_0020EDE8` is the stronger consumer
oracle: it loads `moby+0x68/+0x6C` directly and blends those two packets. Other
proved producers (`FUN_00224B70`, `FUN_00224D28`, `FUN_00224E18`) may repoint
materialized packets, so local/cache pointer mismatches remain explicit
`direct-guest-packet` provenance only after the observed Phase-10 packet extent
is fully bounded in guest RDRAM. One-sided zero pointers, malformed packets and
invalid local metadata still fail closed. `game::decodeRac1LiveRatchetPose`
validates the immutable external-prefix contract against the Phase-10 Ratchet
bank, then decodes the two materialized packets Retail actually supplied instead
of reconstructing a different pose from IDs. `moby+0x54` is carried unchanged as
the Retail blend alpha; `moby+0x70` remains raw. Release/19-CTest/viewer and
20-second Windows runtime acceptance are green for this contract; materialized
packet-to-packet paths are covered by direct GCC/Clang regressions even though
the sampled fallback checkpoint is still pre-materialization. Camera, input and
continuous rendered-instance ownership remain later gates.

Step 11.4 establishes the world-transform bridge directly from Retail consumers,
without reusing the Phase-9 static-instance layout or guessing a live Euler
order. `FUN_0020D868` loads `moby+0x10` as an xyz world-position vector for
spatial subtraction, and `FUN_0021E230` independently writes spawn xyz to those
three floats. `FUN_0020C5F0` initializes the float at `moby+0x2C` from the
class's raw model scale at `class+0x24`; both `FUN_0020CCA8` and
`sub_0020CD48` multiply the live value by the literal float `1/1024`.

For orientation, `FUN_0020DEF8` is the authoritative transform consumer. Unless
its proved cached-basis flag requests reuse, it loads the vector at `moby+0x40`,
runs VU0 microprogram `0xD18`, and stores `vf20`, `vf21`, `vf22` at
`moby+0xC0`, `+0xD0`, `+0xE0`. It then uses those three vectors explicitly as
columns (`vf20*x + vf21*y + vf22*z`) after multiplying local xyz by raw
`moby+0x2C`, multiplies world position by 1024, and adds it. Dividing that
Retail output domain by 1024 gives the native Phase-9/10 formula exactly:
`world = position + basis * (rawSkinnedPosition * mobyScale/1024)`. OpenRatchet
therefore consumes the Retail-cached basis itself; it never needs to infer the
VU0 Euler order or introduce an axis conversion. Because `FUN_0020C5F0` zeroes
the entire 0x100-byte Moby before initialization and `FUN_0020DEF8` is the proved
writer of the three cached vectors, an exactly all-zero basis remains an explicit
`basis-not-materialized` state rather than a host-generated rotation.

`game::inspectRac1LiveRatchetWorldTransform` validates exactly one traversed
Ratchet and the pool's independent candidate count, then exposes the coherent
Retail position/scale/rotation-input/basis snapshot.
`game::transformRac1LiveMobyRawPositionToWorld` is a direct host transcription of
the formula above for Phase-10 raw skinned positions. Runtime refresh happens in
the same `GuestExecutionScope` handoff as live pool/animation state; only
`[OpenRatchet:live:ratchet-transform]` diagnostics are throttled. Step 11.4 is
Windows-accepted: Release build/link is green, 20/20 CTests pass, the Phase-10
viewer is regression-free, `third_party/PS2Recomp` remains clean, runtime
replacements remain 21/21 with zero install errors, and the 20-second live run
observes the authentic all-zero cached basis as `basis-not-materialized` while
Moby accounting remains exact. That sampled construction state is preserved as
evidence rather than promoted into a startup blocker or replaced with a host
rotation. Step 11.4 does not transfer renderer ownership; that remains Step 11.6.
Step 11.5 is the next active ownership boundary and must independently prove the
retail gameplay camera/view state.

Wrench/noclip are reverse-engineering references only; OpenRatchet's parsers are
independent implementations of the retail structures.
