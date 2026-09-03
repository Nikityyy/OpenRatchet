# OpenRatchet native-port architecture

OpenRatchet is a native PC port, not a title-specific PS2 emulator.

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

## Temporary legacy layer

`src/guest_overrides.cpp` is retained only to preserve the current verified
boot while native subsystems are introduced. It contains known technical debt:
SIF response synthesis, CDVD/WAD injection, DMA/scratchpad completion bridges,
address-specific control-flow repair, and graphics diagnostics. New platform
features must not be added there unless required solely to keep the verified
fallback baseline alive during a bounded migration.
