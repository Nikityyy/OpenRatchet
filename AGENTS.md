# OpenRatchet agent instructions

These are the persistent operating rules for OpenRatchet. Keep them stable and
short enough to load in every task. `MILESTONES.md` is the single source of
truth for current progress, the active divergence, and the next experiment.

## Mission

OpenRatchet is a native PC port of Ratchet & Clank for PS2. Progress from the
current native state to an authentic first frame, then a playable slice, full
game completion, and release-quality operation.

- Repository root: `C:\Users\berge\Downloads\OpenRatchet`.
- Run repository commands from that directory.
- Preserve authentic guest behavior. A build, surviving process, fallback
  image, forced frame, or synthetic data does not prove game functionality.
- Prefer reusable subsystem behavior over address-specific patches. A small
  correct fix in SIF, CDVD, DMA, interrupts, recompilation, or GS is more
  valuable than bypassing the next visible wait.
- Work on one bounded, evidence-backed iteration at a time. Complete its stated
  acceptance delta or produce a concise handoff before changing scope.

## Sources of truth and startup

At the start of a task:

1. Read this file.
2. Read only the `Active work` section and milestone statuses in
   `MILESTONES.md`; consult older Git history only when the active investigation
   requires it.
3. Inspect `git status --short` and preserve all existing work.
4. Inspect the newest relevant logs, not every historical log.
5. Verify required build/cache/runtime inputs only when the planned action uses
   them.

Do not recursively inventory the repository, reread all generated output, or
repeat an unchanged build/test merely to reconstruct context. Use `rg` and
address-targeted reads first.

If `MILESTONES.md` is stale or contradictory, establish the current state from
the repository and newest verified logs, repair its `Active work` section, and
then proceed.

## Reference tools

- Generated PS2Recomp output is the first reference for emitted native control
  flow and function/address lookup.
- Ghidra is the static reference for the original `PS2_MAIN.ELF`: callers,
  dataflow, structures, constants, and instructions not represented correctly
  in generated output.
- PCSX2 PINE is the live game/memory reference.
- PCSX2 DebugServer is the execution reference for breakpoints, watchpoints,
  registers, stepping, disassembly, and backtraces.

Verify a connection once before relying on it. A listening TCP port is not a
successful MCP handshake.

- PCSX2 DebugServer normally uses port `21512`; PINE normally uses `28011`.
- Both must report connected to Ratchet & Clank, including running/paused state,
  before reference-dependent runtime work.
- If PINE is disconnected or its handshake fails, stop and ask the user. Do not
  substitute DebugServer, Ghidra, or a listening port for PINE evidence.
- Ghidra normally uses port `8193`. Prefer a direct status query for that known
  instance; perform full instance discovery only if the direct query fails.
- If formatted GSPRIV output is blank, use the raw read-only DebugServer result
  and its `.value` fields. Never infer omitted values.

Use tools as bounded experiments:

- Map a PC through PS2Recomp/generated output before broad Ghidra exploration.
- Reuse one fresh PCSX2 boot across sequential forward-reachable captures.
  Before asking for a reset, verify both MCP handshakes and arm the mapped
  breakpoint; after a capture, leave the reference session paused and arm the
  next proven target before resuming. Reset only when the target has already
  executed or the required reference state is incompatible.
- Prefer Ghidra dataflow/callgraph and focused decompilation over repeatedly
  decompiling neighboring functions.
- Prefer conditional breakpoints, contiguous bulk reads, and PCSX2 memory diffs
  over repeated `continue -> status -> read` polling.
- Define the breakpoint, registers, memory ranges, expected transition, and
  cleanup before running a reference capture.
- Persist useful Ghidra names/comments when doing so will prevent rediscovery.
- Clear temporary breakpoints and watchpoints when the capture is complete.

Routine read-only queries, builds, tests, breakpoint management, and emulator
pause/resume needed by the active experiment are authorized. Ask only when a
required tool, MCP, permission, external artifact, destructive action, or
material user decision is missing.

## Iteration contract

Before editing, state internally or in a short progress update:

- native divergence: the exact PC, state, packet, buffer, or visible failure;
- reference behavior: what the original does instead;
- hypothesis: one proposed missing behavior;
- owner: one subsystem or source layer;
- acceptance delta: one measurable post-fix transition.

Do not edit until local/generated evidence and, when behavior matters, Ghidra
or PCSX2 evidence support the hypothesis. Never invent reference results.

Default investigation budget for one iteration:

- one unchanged native baseline, only if existing logs are insufficient;
- one reference capture;
- up to three directly relevant functions and three memory regions;
- one temporary instrumentation change;
- no more than two edit/build/test cycles.

Exceed a default only when new evidence gives a concrete reason. If the work
branches into a second subsystem or stops converging, end with an evidence
handoff instead of making a speculative patch.

## Correct implementation layer

Determine ownership before changing code:

- Recompiler/control-flow defects belong at the root-owned dispatch or override
  boundary available to this repository.
- SIF/RPC behavior must transport real request state and payloads, not only
  return captured pointer values.
- CDVD behavior must support general sector/file reads, not a named startup WAD
  injected from a guest-function hook.
- DMA/interrupt behavior must model transfer and completion state, not clear
  whole registers or repeatedly call guest code until a wait disappears.
- GS behavior must consume authentic guest packets and VRAM data; presentation
  code must not fabricate a framebuffer.

Hard-coded guest addresses are acceptable only when they are stable addresses
from this exact ELF and the implementation reproduces verified original
semantics. Every temporary compatibility shim must document:

- the reference evidence;
- why the value/address is invariant;
- the subsystem behavior it stands in for;
- the condition for replacing or removing it.

Do not add another command-specific response, forced return PC, asset injection,
or generated-call correction merely because it advances execution.

## Change boundaries

- Keep `third_party\PS2Recomp` read-only. Do not edit, reset, or clean it.
- Keep generated output read-only unless evidence proves generation itself is
  the correct fix location and the user approves the exception.
- Make game-specific behavior in root-owned code through public runtime APIs.
- Preserve unrelated user changes; never reset or overwrite them.
- Avoid broad refactors during diagnosis. Introduce a subsystem module when the
  behavior is proven or a second similar shim would otherwise be added.
- Keep diagnostic logging bounded and category-specific. Remove investigation-
  only logging before handoff unless it is a durable test signal.
- Never create commits. The user reviews and creates one commit per accepted
  iteration.

## Build and native verification

Visual Studio 2026 Build Tools with C++ is installed at
`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`.

The shell can expose both `Path` and `PATH`; raw CMake/MSBuild invocation may
fail with duplicate environment keys. Use the verified helper:

```powershell
.\tools\build-native.cmd -Configuration Release
```

Before building, verify the existing cache points to this repository, its
read-only `third_party\PS2Recomp`, `build\extracted\PS2_MAIN.ELF`, generated
output, and the installed Visual Studio generator. Do not delete/reconfigure
the build tree, fetch dependencies, or extract assets silently.

Use the compact native diagnostic when a runtime run is required:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\tools\diagnose-native.ps1 -DurationSeconds 10
```

Use `-Build` only when source changed or the executable is stale. Use the
shortest duration that can observe the target transition. Do not repeat an
unchanged timed run without a new observation goal.

For each verification, report the exact command, build/launch result, target
transition, relevant counters/state, log paths, regression result, and whether
the iteration acceptance delta passed.

## Milestone acceptance

`MILESTONES.md` defines current acceptance criteria. For M2, acceptance requires
all of the following in one reproducible native run:

- authentic guest DMA/GIF/GS work;
- nonzero guest-produced VRAM/frame content;
- verified display/framebuffer registers and addresses;
- a host-presented frame sourced from that guest content;
- continuous frame updates for at least five seconds;
- comparison with the equivalent PCSX2 state.

Nonzero counters alone, a black buffer, the magenta fallback, a hard-coded test
image, or host-injected pixels do not pass M2.

After M2, continue subsystem-first through title/input, the first playable area,
the core gameplay loop, content/system coverage, complete-game progression, and
release hardening. Do not trade long-term correctness for a screenshot.

## Documentation and handoff

Keep `MILESTONES.md` concise. After an iteration, update only:

- milestone status if acceptance changed;
- `Active work`: verified state, active divergence, next experiment;
- latest evidence/log row;
- temporary-debt list when a shim is added or removed.

Do not append a chronological narrative or preserve obsolete next steps in the
active file; Git history already retains prior states.

At completion or a stopping point, report:

- what was proven and what changed;
- exact verification and acceptance result;
- regressions/limitations;
- the single next experiment;
- one accurate suggested commit message, or state that no commit is warranted.

Then stop for user review. Do not begin a different iteration or major
milestone in the same task.
