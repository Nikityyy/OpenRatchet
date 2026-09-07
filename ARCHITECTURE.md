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

## Native resource-stash boundary

Phase-12 runtime evidence exposed a custom service dependency immediately before
the original controller update becomes reachable. This is not 989snd: generated
`sub_00232D00` binds its own client at `0x1DD1A8` to SIF RPC service ID `0x11`,
then waits in `0x232D60`. The sole startup caller cannot continue to
`sub_00202A98` and the later `FUN_00217A10 -> sub_002170C8` controller update
until that bind completes. OpenRatchet does not solve this by adding SID `0x11`
to the synthetic SIF resolver.

The surrounding Retail functions prove a higher resource-staging contract.
`0x232E40` allocates one of 64 monotonically numbered slots, stages bytes from EE
memory with `sceSifSetDma`, and records lengths in exact 16-byte units.
`0x232F20` retrieves a requested slot range through RPC function 1, and
`0x233038` returns the slot length. The initialization RPC function 2 supplies
only the remote IOP buffer base/capacity and initializes the private transport
table. All known game store callers copy the same number of 16-byte units they
reserve, while `FUN_00226848` retrieves a full slot immediately before the
existing decompressor consumer. These producer/consumer relations make the
stash API—not its SIF packets—the native ownership boundary.

`rac1_native_stash` therefore keeps the staged bytes in real host memory and
reproduces the game-visible 64-slot, 16-byte-unit, subrange-read and error
contract. It snapshots source bytes at store time and writes them back to the
requested guest destination at read time. It does not manufacture an IOP address,
remote capacity, service bind or completion packet. Guest ranges are validated
before mutation; unsupported/unmaterialized ranges fail closed. This removes a
PS2-only resource transport prerequisite while retaining the original game asset
callers and downstream decompressor.

## Native input boundary

Phase 12 owns controller input above libdbc/DBCMAN rather than extending the
temporary SIF transport. Generated Retail code proves the semantic cut:
`sub_002170C8` obtains a raw controller report from `0x124BD8` and passes that
report directly to `FUN_00217328`. The latter remains authoritative for button
inversion/mapping, press/release edges, stick deadzones/history and all game-side
controller state. OpenRatchet therefore must never bypass it by writing the
controller object's parsed fields (`+0x1A0`, `+0x1A4`, etc.) from host input.

The Phase-12 native input module owns the four libdbc-facing transactions used
by that Retail state machine (`0x124BD8`, `0x124CB0`, `0x124DA0`, `0x1250D8`).
The transport-neutral host contract is deliberately only the portion proved by
the consumer: a six-byte report containing active-low buttons followed by
right-X/right-Y/left-X/left-Y, all centered at `0x7F`. Retail itself gates
pressure parsing on report length `>=0x12`, so the native backend reports six
bytes until pressure semantics receive their own proof. The two connection
transition queries expose no optional PC-specific metadata, and device status
selects the existing Retail connected result because the native keyboard input
device is always available. No DBCMAN service, captured RPC row, fake pad global
or host-authored Ratchet movement is permitted.

Host input is sampled by the native presentation owner and published to the guest
read boundary as one atomic report snapshot. The original EE simulation consumes
it on its next controller update. This one-frame handoff is intentional: it keeps
Raylib polling on the host/UI side and prevents the fallback execution path from
calling window-system APIs directly. An unfocused window publishes the neutral
report.

Step 12.2 observes the other side of that same semantic boundary without taking
ownership away from Retail. `rac1_native_input` exposes a read-only inspector for
the exact fields written by `FUN_00217328` in controller state `0x13C940`:
right-X/right-Y/left-X/left-Y at `+0x100/+0x104/+0x108/+0x10C`, current logical
buttons at `+0x1A0`, pressed edges at `+0x1A4`, and released edges at `+0x1A8`.
`OpenRatchetRuntime` reads those fields only while holding the existing
`PS2Runtime::GuestExecutionScope` and emits `component=parsed-live` diagnostics
after releasing the guest-execution handoff. The inspector cannot write guest
memory; it is an acceptance probe proving that host reports passed through the
unchanged parser rather than a second input implementation.

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

Post-stash Phase-12 execution exposes one additional call that belongs to this
same already-deferred audio boundary. `sub_0012E1A8` is proved—not named by
inference—to be a zero-payload wrapper for 989snd command `8`: it enters
`FUN_0012E6E0` with command `8` and all payload arguments zero, which in turn
submits through the `0x15EBC0` client bound by `FUN_0012DA28` to service
`0x123456`. The startup producer at `0x1E99B0` sits immediately after the native
`sub_0022D708` level-bank boundary. Its later consumer/wait at `0x1EB968` calls
`FUN_0012DC80`, whose `FUN_0012DE70` branch checks that same client. Because
Phase 18 already owns the omitted 989snd operation, OpenRatchet replaces
`sub_0012E1A8` at the EE audio-API boundary and returns the exact successful RPC
submission result `0` without creating client busy state, an IOP service,
response data or any game-visible audio state. `FUN_0012DC80` itself remains
unchanged; it becomes idle naturally because the deferred PS2 audio transaction
was never started.

Later Phase-12 execution proves the same architectural escape at the shared
989snd EE submitter itself. `FUN_0012E6E0` receives command id in `a0`, payload
byte count in `a1`, payload pointer in `a2` and an auxiliary submission value
in `a3`, then queues the request in 989snd-private buffers and pumps those buffers through RPC
function `0x4D`. With startup already native-owned, that client is intentionally
unbound; once the private queue fills, Retail loops at `0x12E820` while
`FUN_0012DC80 -> sub_0012E9D8` keeps the `0x4D` request busy. OpenRatchet therefore
owns `FUN_0012E6E0` as the shared **audio command-submission boundary**, not as a
SIF transport shim. Deferred commands are accepted without materializing 989snd
queue entries, client/busy state, response buffers, IOP state or sound output.
The replacement preserves guest registers/memory and only returns through the
original RA; in particular it preserves incoming `v0` instead of inventing a
replacement result for an internal pump value that no reviewed game caller
consumes. Command-specific meanings remain unguessed and actual audio behavior
remains Phase 18. The lower `FUN_0012DC80` and `sub_0012E9D8` code
is not patched and no synthetic function-`0x4D` response is added.

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
carry compatibility corrections. `patches/ps2recomp-synchronous-dmac-interrupt-stack.patch`
is an upstream-ready compatibility patch against the exact revision recorded in
`patches/ps2recomp-base-revision.txt`. The shared preparation helper requires that
checkout to be clean, archives committed `HEAD`, and applies the patch only to a
disposable build-local copy. Native runtime configuration uses
`build/native/_openratchet/PS2Recomp`; static regeneration uses
`build/tooling/_openratchet/PS2Recomp`. Revision drift, a dirty checkout, or a
failed patch context is a hard error. This keeps third-party source state
immutable and makes temporary upstream corrections explicit, reproducible and
removable when upstream contains equivalent fixes.

### PS2Recomp REGIMM branch-and-link resume boundary

Phase-12 execution exposed a second genuine recompiler defect. R5900
`BLTZAL/BGEZAL/BLTZALL/BGEZALL` are control-flow operations with a link return at
`PC+8`. PS2Recomp already records that continuation for `JAL` and `JALR`, but its
control-flow analyzer did not record it for REGIMM link branches. This becomes a
hard runtime failure when a generated owner contains both the branch site and the
callee-like target: the target's `JR $ra` returns through the global dispatcher,
which cannot re-enter the owner unless that PC+8 address is registered as a
resumable entry and emitted as an owner label.

The concrete R&C1 failure is `0x22BE00 BGEZAL -> 0x22BEC4` with `$ra=0x22BE08`.
The generated `sub_0022BBA0` contains the instruction at `0x22BE08`, but the
pre-fix function table omits that address, producing the deterministic
`No exact recompiled function for guest PC 0x22be08` loop. Three additional
BGEZAL sites in `sub_00217C18` have the same structural requirement. The
compatibility patch therefore fixes the analyzer generically: all four REGIMM
link variants promote `PC+8` through the existing resume-entry mechanism. It does
not special-case Ratchet addresses, alter game state, or add a native HLE. An
OpenRatchet regression fails against the pristine pinned analyzer and passes with
the build-local patch, and the static recompile stage now consumes that same
patched source so regenerated read-only fallback C++ and its dispatch table carry
the corrected metadata.

Ghidra's exported `game.toml` contains absolute filesystem locations and is
therefore not portable across checkout renames or moves. Static recompilation
treats those locations as export-time metadata: immediately before invoking the
recompiler, `tools/bootstrap.ps1` canonicalizes exactly one `input`, `output` and
`ghidra_output` entry to the active checkout (`-Elf`, `generated/`, and
`build/game.csv`). Missing or duplicate path keys are hard errors. This prevents
a valid analyzer/runtime correction from being tested against a stale ELF or
another working copy while keeping the generated fallback itself read-only.

### PS2Recomp synchronous normal SPR-DMA fallback boundary

Phase-12 continuation repair lets Retail progress into `sub_00235BE8`, where the
still-active EE fallback uses real scratchpad as transient render workspace. This
is distinct from OpenRatchet's native compressed-asset boundary below: native WAD
decompression must never regain an SPR/DMAC dependency. The requirement exists
only because unconverted Retail EE code is still executing.

The concrete producer/consumer is proved. `sub_00235BE8` starts normal-mode DMAC
channel 9 (`0x1000D400`, TO_SPR) with scratchpad offset `0x2000` and immediately
consumes the resulting 32-byte records at `0x70002000`. Its caller subsequently
uses `FUN_001F9928` to start normal-mode channel 8 (`0x1000D000`, FROM_SPR) and
copy `0x40` qwords from scratchpad offset `0x3600` back to guest `0x1E3200`.
PS2Recomp's pinned `PS2Memory` stores those channel registers but has no channel
8/9 data mover; its generic CHCR read clears `STR` anyway. The fallback can
therefore report completion without materializing the bytes Retail consumes.

This defect is repaired at the PS2Recomp compatibility boundary rather than
hidden with a Ratchet render HLE. The build-local patch implements only the
proved synchronous **normal-mode** SPR contract for contiguous RDRAM: channel 9
copies RDRAM to the 16-KiB scratchpad, channel 8 copies scratchpad to RDRAM,
`QWC` means 16-byte units, scratchpad `SADR` wraps to 14 bits, MADR/SADR advance,
QWC becomes zero, CHCR `STR` clears, D_STAT records the channel completion, and
the matching DMAC cause is queued. FROM_SPR writes also mark the direct RDRAM
range modified. Unsupported chain/interleave/MFIFO or non-RDRAM behavior is not
invented.

`ps2recomp_spr_dma_tests` is a counterfactual ownership gate, not an emulator
feature test: it reproduces the exact Retail TO_SPR and FROM_SPR transfer shapes
and a scratchpad-boundary wrap. The pristine pinned runtime fails at the first
byte comparison because no transfer occurs; the patched runtime passes. Because
`ps2_runtime` references runner-owned generated dispatch-table globals even when
a test exercises only `PS2Memory`, this standalone regression links the pinned
PS2Recomp unit-test shim `ps2xTest/src/test_function_table.cpp` rather than any
R&C1 generated runner/table. This correction remains removable with PS2Recomp
fallback retirement and does not create a native OpenRatchet DMAC subsystem.

### PS2Recomp DECI2 registered-handler completion boundary

After the SPR transfer correction, Retail reaches its own DECI2 producer/consumer
at `FUN_001197A8`. That routine sets callback state `0x154A50+0x0C`, requests a
send through `FUN_00119468 -> Deci2Call(code=3)`, then polls through
`sub_00119498 -> Deci2Call(code=4)` until the same word becomes zero. The session
initializer `sub_001199C8` opens protocol `0x210` with `opt=0x154A50` and handler
`FUN_00119610` (`0x119610`). The handler's event-3 branch feeds the queued bytes
through `sceDeci2ExSend`; event 4 reaches Retail instruction `0x119788`, which is
the authoritative writer that clears `state+0x0C`.

Pinned PS2Recomp violates that contract in two runtime-level ways: Release builds
compile the entire `Deci2Call` body out unless `_DEBUG` or `RUNTIME_DECI2CALL` is
defined, and the enabled implementation treats request-send/poll as immediate
success without invoking the registered handler. OpenRatchet carries the minimal
compatibility correction in the same immutable build-local patch as the earlier
runtime fixes. DECI2 semantics remain active in Release; the open-time `opt` and
handler are preserved; request-send synchronously dispatches event 3 and records
one pending completion; the next poll dispatches event 4 exactly once. The guest
callback receives the stored `opt` in `a2`, uses a zero return-address sentinel,
and inherits the interrupted guest SP, matching the synchronous callback-stack
rule already required for DMAC handlers. The runtime does not clear R&C1 state on
the host side; completion exists only if the unchanged registered Retail handler
runs and performs its own write.

The `ps2recomp_deci2_tests` counterfactual gate uses the exact R&C1-shaped
protocol/state/callback relation. It fails on pristine Release-style DECI2 before
any event-3 callback is observed and passes after the compatibility correction,
proving payload drain via `sceDeci2ExSend`, event-4 completion, callback `a2`/SP/RA
semantics, and absence of duplicate completion dispatch. This is temporary
fallback-runtime correctness for an existing Sony EE API, not a new debugger
transport subsystem and not an excuse to HLE unrelated 989snd traffic.

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
(`+0xC0/+0xD0/+0xE0`). Step 11.5 keeps camera ownership separate again: the
camera is an independent Retail global, not a field inferred from the Moby pool.

During Phase 11, `OpenRatchetRuntime` is the owner of the temporary attachment
to the still-running EE fallback. It obtains the actual 32 MiB RDRAM pointer
through `PS2Runtime::memory().getRDRAM()` and snapshots it under
`PS2Runtime::GuestExecutionScope`. That scope uses PS2Runtime's existing
function-boundary handoff, so host reads do not race guest writes. Snapshot
formatting/logging is done only after releasing the execution scope.

Through Step 11.5, PS2Runtime's host-presentation callback is only the coherent
host/guest handoff clock while the fallback runtime owns the executable loop.
It never becomes part of R&C1 game semantics: live pool/animation/transform/
camera state is decoded from Retail RDRAM, and only diagnostics are throttled.
Phase 11.2 keeps the pool bridge read-only and never fabricates RDRAM pool
globals, transforms, or camera state. After the proved platform/transport
prerequisites above were lifted to their native boundaries, Retail itself reaches
`sub_001E9B10`, publishes `0x15FF18` and `0x15FF20`, and the decoder reports
`[OpenRatchet:live:moby] ... status=ok` with `unaccounted=0`.

Step 11.6 deliberately reuses that same callback as a *host presentation
ownership boundary*, not as a game-semantic dependency. PS2Runtime may still run
its internal GS compatibility work for fallback execution, but its queued final
frame is flushed before the OpenRatchet callback draws and is then cleared away.
The visible window frame from that point is OpenRatchet-owned. The callback may
therefore host the native renderer while PS2Runtime still owns the temporary
window/event loop; moving the application loop itself remains a later ownership
cleanup and is not required to reintroduce GS presentation.

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

Step 11.5 introduces `game::inspectRac1LiveCamera` as an independent, fixed-global
camera contract. Retail `sub_001E9B10` clears exactly 0x3A0 bytes rooted at
`0x00186F40`; no Moby pointer is involved. `FUN_0020D868` loads
`0x00187080 = state+0x140` and subtracts it from Moby world position, proving the
camera/observer world-position role. Camera producers `FUN_001ED2B0` and
`sub_001EDAA8` copy the selected camera transform into position `+0x140` and
three orientation vectors at `+0x350/+0x360/+0x370`. `FUN_00218D10`
independently initializes those fields to position `(256,256,64)` and identity
xyz orientation, while `sub_001EAF88` materializes the same nine orientation
floats through Retail's own matrix helpers.

OpenRatchet does not reverse that state into guessed Euler angles, target/up or
FOV. The stronger renderer oracle is the matrix Retail already materializes:
`FUN_001F2260` consumes the selected orientation and builds the downstream camera
matrices. `FUN_0022BF94` then loads `state+0x100/+0x110/+0x120/+0x130` and
computes each camera-relative clip vector as exactly
`clipX*x + clipY*y + clipZ*z + clipW*w` before `vclipw` and reciprocal-W
perspective division. Later Phase-12 presentation proof refines the input domain
without changing those stored columns: `FUN_001F2260` explicitly leaves the
underlying homogeneous rotation translation at `(0,0,0,1)` and never consumes
`state+0x140` while materializing the clip block, whereas `FUN_001F7D30` loads
that camera world position and subtracts it from absolute world xyz before
applying another camera matrix built from the same rotation block.
`FUN_00227A08` independently consumes the neighboring `+0xC0..+0xF0` camera
matrix together with `+0x140` position. The native bridge therefore preserves
the four Retail clip vectors in exact consumer order and keeps camera world
translation independent. All-zero
orientation or clip blocks keep explicit construction-state statuses, while
non-finite values fail closed. Runtime reads happen under the same coherent
`GuestExecutionScope` as 11.3/11.4, but the camera decoder itself remains
PS2Runtime- and renderer-free. Windows acceptance is complete: Release links,
21/21 CTests pass, the Phase-10 native viewer remains regression-free,
`third_party/PS2Recomp` stays clean, runtime replacements remain 21/21 with zero
install errors, and the 20-second live run observes the authentic zeroed camera
construction state as `orientation-not-materialized`. That sampled state is not
promoted into a startup blocker and is never replaced with an invented identity
camera/FOV.

Step 11.6 is the renderer-ownership boundary. Step 11.6A moves final
presentation to OpenRatchet at the existing post-GS/pre-`EndDrawing` callback:
the compatibility draw is flushed first, then cleared, so a delayed GS batch
cannot regain visible ownership after native rendering starts. The old GS path
may remain internally while still needed by fallback execution, but it no longer
defines the intended final window image.

OpenRatchet has one **intended native rendering implementation**, but two distinct
frontends consume it: the standalone `native_level_viewer` diagnostic frontend and
the live `openratchet.exe` runtime frontend. They already share
`render/native_mesh_renderer.*` (and the shared skinned-Moby renderer below), but
shared helpers alone are not sufficient proof that every renderer-facing semantic
is identical. Phase 12 therefore makes the permanent architectural rule explicit:
**different state acquisition/orchestration; shared rendering semantics/backend for
equivalent canonical inputs.**

Intentional frontend differences are allowed and must remain explicit: extracted
or static WAD state versus live Retail state, viewer free camera versus Retail
camera ownership, viewer demo animation clock versus guest animation state, debug
visibility versus Retail object lifecycle/visibility, eager viewer loading versus
runtime streaming/lifecycle, and viewer controls/debug UI versus game runtime
orchestration. Those differences must not be "fixed" by forcing the runtime to use
viewer-owned state.

Equivalent render operations must not drift into separate viewer/runtime
implementations without a documented architectural reason. For the same canonical
input, geometry/topology conversion, texture/material interpretation, vertex/index
buffer contents, world-transform math, primitive topology, cull/depth/blend state,
batching and native draw submission are required to be equivalent and should
converge on shared code. Deterministic parity diagnostics must be able to compare at
least vertex/index counts and hashes, texture/material identity, transform hashes,
primitive topology, render state and draw ordering. A parity mismatch in canonical
CPU-side data localizes the defect to scene materialization/preparation; matching
canonical data with divergent pixels localizes it to GPU upload/state, resource
lifetime or draw submission.

The runtime feeds the shared renderer authoritative native tfrag/tie/shrub data in
untouched Retail world coordinates. Scene activation is tied only to proved Retail
resource identity: currently exactly a successful full `wads2[69]` (`0x38F6`,
`0x834` -> `0x01654000`) transfer maps to native Level 0. Neighboring WADs,
partial ranges and different destinations remain unmapped. Camera conversion uses
the already materialized Step-11.5 clip columns in their proved X/Y/Z/W
**column-major storage order**; that storage proof does not imply that Retail's
post-divide GS screen convention is identical to OpenGL's viewport convention.
Native terrain, static geometry and live Ratchet vertices are absolute Retail world
coordinates. Because Retail keeps `state+0x140` out of the materialized clip block
and subtracts that world position before camera multiplication, the runtime first
composes exactly `Mclip * T(-cameraPosition)`. Windows proves this composition
reaches real native Level-0 framebuffer pixels. `FUN_0022BF94` further proves the
next stage: after canonical `vclipw` clipping and reciprocal-W divide, Retail
applies its GS screen transform from `state+0x190/+0x1A0`. GS visible Y increases
downward, whereas OpenGL window Y increases upward. The native presentation bridge
therefore converts only clip Y with `diag(1,-1,1,1)` before handing the matrix to
rlgl; X, Z and W remain Retail values. Windows validation closes both the former
black-frame defect and the pure vertical-inversion defect. This remains an
algebraic API-convention bridge, not a reconstructed host camera: no host
`Camera3D`, FOV, target/up or guessed camera orientation is introduced.

The post-fix runtime still shows large stretched/crossing triangles that do not
appear in the standalone viewer. That evidence is now classified as a Phase-12
**renderer-parity defect**, not a reason to duplicate the viewer path wholesale and
not a reason to replace live Retail state with viewer state. Step 12.3B must isolate
which supposedly equivalent renderer-facing invariant first diverges and then move
that invariant to the shared implementation boundary. Only intentional frontend
state/orchestration differences may remain afterwards.

Static scene ownership can therefore move before all dynamic state is available.
Live Ratchet/Moby animation and transforms are consumed only when their proved
materialization and identity contracts allow it; sky rendering waits for its
Retail runtime transform. Explicit `endpoints-not-materialized`,
`basis-not-materialized` and `orientation-not-materialized` states remain
`deferred` and must never be converted into guessed host values merely to produce
a prettier frame. Renderer accounting keeps mapped, materialized, rendered,
deferred and unaccounted counts explicit. Step 11.6A is Windows-validated:
Release links, 22/22 CTests pass, the Phase-10 viewer is regression-free,
`third_party/PS2Recomp` remains clean, runtime replacements stay 21/21, and the
20-second runtime proves Level 0 mapped/materialized with `unaccounted=0`. The
sampled camera is still the authentic `orientation-not-materialized` state, so
the native-owned user-visible frame is intentionally black with
`rendered=0 deferred=1` until Retail materializes the clip transform.

Step 11.6B1 closes Ratchet's renderer identity without treating the live class
pointer as a relocation oracle. Retail `FUN_0020C5F0` writes its incoming
`oClass` unchanged to `moby+0xA6`; the live animation and transform bridges both
select the unique traversed `oClass==0` object, while the native Level-0 Moby
scene independently contains exactly one rendered `oClass==0` topology. Runtime
mapping therefore requires that exact class identity and the same guest-Moby
address on both live bridges. The separately relocated `moby+0x24` pointer is
not part of the mapping: native class offset `0x739540` plus the observed
Level-0 core destination `0x654000` gives `0xD8D540`, disproving a direct relation
to the sampled live class pointer `0xA9E340`.

The dynamic Ratchet render path remains the Phase-10 path rather than a runtime
facsimile. `render/native_skinned_moby_renderer.*` owns the shared
`skinVertexIndex`-mapped dynamic VBO implementation used by both the standalone
viewer and `Rac1RuntimeRenderer`. Level 0 pins Ratchet to four material batches,
6,856 triangles and 4,026 skin vertices. Runtime GPU buffers are created or
updated only from a materialized Step-11.3 packet-to-packet pose plus an
`status=ok` Step-11.4 world transform. Raw skinned positions are converted with
the proved `position + basis * (raw * scale/1024)` equation. Until then the
identity can be mapped while materialization/rendering remains explicitly zero;
no Phase-10 demo animation, static gameplay transform, bind inverse or relocated
class pointer is substituted. Windows acceptance of this B1 boundary is green:
Release builds, 22/22 CTests pass, the Phase-10 viewer remains exact, and the
stable 20-second runtime reports `liveMobyMapped=1`, `ratchetIdentity=ok`,
`liveMobyUnaccounted=0`, `poolUnaccounted=0`, `status=ok` while the authentic pose,
basis and camera pre-materialization statuses remain deferred. PS2Recomp remains
clean.

Step 11.6B2A generalizes only the **class-identity** half of that join. Its first
Windows gate disproved an over-strong assumption: a stable active live Moby had
`oClass=1905`, while Level 0's class-index table contains 125 unique IDs and does
not contain 1905. The level-core table is therefore a native topology/catalog
domain, not the complete live runtime-class domain.

The stronger Retail oracle is explicit in generated code, but the constructor
contains two distinct slot tables and they must not be conflated. `FUN_0020C5F0`
first resolves
`slot = *(u8 *)(0x001B3AC0 + oClass)`. It then loads
`classDataPtr = *(u32 *)(0x001B3200 + slot*4)` and stores that exact pointer at
`moby+0x24`. Separately it loads `*(u32 *)(0x001B3580 + slot*4)` and stores that
word at `moby+0x74`; this second word is not the class-data pointer and its
higher-level semantic role is intentionally left uninterpreted. The constructor
immediately dereferences `moby+0x24` for class fields (`+0x0E`, `+0x44`, `+0x24`,
`+0x40`, `+0x48`), while `sub_00203640` is the producer that publishes loaded
class-data pointers into `0x001B3200`.

OpenRatchet therefore snapshots this runtime registry together with the live Moby
pool under the same `GuestExecutionScope`. Every active record is identity-valid
only when its oClass is within the Retail 0x800-entry domain, its registry slot is
not `0xFF`, and `0x001B3200[slot]` equals the class-data pointer already stored at
`moby+0x24`. A previous comparison against `0x001B3580` was disproved by the
Windows gate (5/5 pointer mismatches) and is now pinned against regression with a
conflicting auxiliary-table test value. This is not the rejected B1 relocation
heuristic: both compared class-data pointers are live Retail values from the exact
producer/consumer path.
Only after that proof does the native level-core catalog classify topology. Level
0 still proves 125 unique native classes = 14 renderable + 7 intentionally
invisible + 104 class-only. A registry-valid identity absent from those 125 is a
distinct **runtime-only** class with native topology still deferred, not an
identity/accounting error. Negative traversal slots remain inactive. Registry
unmapped/out-of-range classes, pointer mismatches, duplicate native classes and
counter mismatches remain hard failures. No runtime object is matched to a
Phase-10 static instance. Windows acceptance of this B2A boundary is green:
Release builds, 22/22 CTests pass, the viewer remains regression-free, and the
stable runtime accounts all five sampled active Mobys as 3 renderable-topology +
1 class-only + 1 runtime-only with every Retail-registry failure counter zero,
`liveMobyUnaccounted=0`, `poolUnaccounted=0`, `liveMobyClassMap=ok` and
`status=ok`. PS2Recomp remains clean.

Step 11.6B2B closes the **sky renderer-transform** boundary without borrowing the
viewer placement. Retail `FUN_0022B288` reads the live sky pointer from
`0x0016045C`, shell count from `sky+0x6`, shell pointers from `sky+0x20`, the
base animation angle at `0x00160404`, and the translation qword at `0x00160460`.
For each shell it calls `FUN_001FA070` to materialize an object matrix at
`0x001D96E0`, scales its basis columns, copies the translation qword into its
fourth column, and immediately calls `FUN_0022B690`. Downstream
`FUN_0022BF94` multiplies that object-space result by the already-proved
Step-11.5 clip columns. The `0x001D96E0` block is transient renderer scratch,
not an authoritative global to sample; OpenRatchet reconstructs it from stable
Retail inputs instead.

`FUN_001FA070` applies Z, then Y, then X rotations. The sky passes X=0, so the
host bridge reproduces `Ry(Y) * Rz(Z)` and the exact per-shell scale constants.
Retail's single-wrap angle helper and sine polynomial are transcribed using the
original binary32 constants and separately rounded arithmetic stages rather than
`std::sin/cos`. Counterfactual tests compare every matrix float bit-for-bit with
independently calculated R5900 oracle values and deliberately corrupt
`0x001D96E0` to prove that scratch contents are irrelevant.

The native renderer distinguishes two authentic Retail sky resources instead of
merging them. The Phase-8 standalone viewer continues to consume the level-core
sky (Level 0: 5 shells / 118 clusters / 2,366 triangles / 8 textures / 7 native
batches). Permanent Windows provenance proved that the gameplay runtime instead
points at `0x714640` and exposes 4 shells with header signatures
`(38,1),(25,0),(3,0),(24,0)`. This is not an index convention: it is a different
resource inside the exact indexed WAD2 load.

The active Retail Level-0 loader `sub_001EA830` reads `wads2[69]` (`0x38F6`,
`0x834` sectors) to input `output + 0x01000000`, then `FUN_0020B618` decompresses
it to the gameplay output base. The authentic asset is `0x41A000` bytes, its WAD
header reports encoded size `0x419F6E`, and native decompression produces exactly
`0x7318C0` bytes, matching the live runtime trace. After decompression Retail
forms `s5 = wadBase + *(u32 *)(wadBase+0x04)` and calls `FUN_002028E0` with
`s5 + *(u32 *)(wadBase+0x14)`. For WAD2/69 those offsets are `0x5C000` and
`0x64640`, hence gameplay-sky offset `0x0C0640`; with the observed output base
`0x654000` this relocates exactly to `0x714640`. Decoding that pre-relocation
block natively yields **4 shells / 90 clusters / 1,472 triangles / 8 textures /
5 shell-separated GPU batches** and the same four live signatures.

Accordingly, the runtime renderer receives the exact WAD2 asset from the proved
indexed Level-0 transfer, natively decompresses it, resolves the Retail
`+0x04 + +0x14` source, and uses that topology for gameplay sky. The shared sky
decoder preserves each shell's source index, source offset, cluster count and
flags; batches are keyed by `(shellIndex, materialIndex)`. Decoded XYZ remains
signed-16 divided by 1024, so the runtime-only Retail path applies the exact
inverse power-of-two before the shell object matrix. The render bridge validates
shell count, `liveSkyBase + nativeSourceOffset == liveShellGuestPointer`, and every
`(clusterCount,flags)` pair as soon as the live header is materialized, **before**
accepting `translation-not-materialized` or another legitimate deferred transform.
Thus a deferred transform cannot hide a wrong sky resource. Sky accounting stays
independent from static-world ownership, and no viewer camera/topology or guessed
4-to-5 remap enters the gameplay renderer.

Windows acceptance closes this renderer boundary. Release builds, **23/23 CTests**
pass, the standalone viewer remains exact on the independent 5-shell core-sky
resource, and the mandatory 20-second runtime remains alive with graphics
activity and harness exit 0. The stable live gameplay sky remains the 4-shell
resource at `0x714640`, and the render bridge reaches
`skyMap=live-transform-deferred` with `skyMapped=1`, `skyUnaccounted=0` and
overall `status=ok`; that state is reachable only after all live shell pointers,
cluster counts and flags agree with the native WAD2/69 source offsets. The live
Moby pool is likewise fully accounted at 5/5 identities. Camera, sky translation,
Ratchet pose and Ratchet basis remain fail-closed when Retail has not materialized
them. This run also satisfies the final Phase-11 regression gate, completing the
live game-state/camera renderer bridge without adding Phase-12 input semantics.


Wrench/noclip are reverse-engineering references only; OpenRatchet's parsers are
independent implementations of the retail structures.
