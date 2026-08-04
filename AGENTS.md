# OpenRatchet agent instructions

These are the persistent project rules. The agent should inspect the current
repository and determine the active milestone and next step without requiring
per-task fields in the chat prompt.

## Scope and tools

OpenRatchet is a native PC port of a PS2 game. Work on one meaningful milestone
at a time and make the game progressively functional and playable.

- Repository root: `C:\Users\berge\Downloads\OpenRatchet`.
- Run commands from the repository root.
- Ghidra is the static reference for the original `PS2_MAIN.ELF`.
- PCSX2 PINE is the live memory/game reference.
- PCSX2 DebugServer is for breakpoints, registers, stepping, disassembly, and
  backtraces.
- Verify an MCP connection before relying on its results. Use Ghidra and PCSX2
  whenever they provide useful evidence; host build diagnosis does not require
  them.
- If the PCSX2 PINE MCP is unavailable, disconnected, or its handshake fails,
  stop and ask the user before continuing. A listening PINE TCP port does not
  count as a connection, and DebugServer or Ghidra availability does not make
  it safe to continue without PINE.
- If runtime/reference verification requires an unavailable MCP, report that
  and stop. Never invent MCP results.
- Use existing shell commands and available MCP tools autonomously. This
  includes verifying connections, reading memory, inspecting the original
  game, setting/clearing routine debug state, running builds/tests, and
  pausing or resuming the reference emulator when required by the task.
- Ask before obtaining a genuinely new tool, MCP, plugin, permission, external
  artifact, or user-provided runtime result. Do not ask permission for a simple
  command that an already available tool can perform.

## Stop-and-ask boundary

Keep working autonomously unless one of these applies:

- a required tool, MCP, plugin, permission, or external capability is missing;
- a required file, dump, save, game asset, or runtime result cannot be obtained
  from the repository or available tools;
- an irreversible/destructive action or external coordination is required; or
- an answer is needed to resolve ambiguity that would materially change the
  implementation.

Routine repository commands, builds, tests, MCP connection checks, Ghidra
queries, PCSX2 reads, breakpoints, stepping, and emulator control do not need
user confirmation when they are within the task's scope.

## Fast diagnostic path

Start native investigation with the compact diagnostic unless a more targeted
read-only check is clearly faster:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\tools\diagnose-native.ps1 -DurationSeconds 10
```

It wraps the verified harness and reports process lifetime, SIF completions,
DMA/GIF/GS/VIF counters, recent GS/GIF/frame evidence, diagnostics, log paths,
and repository state. Use `-Build` only when a rebuild is needed. Keep source
inspection focused on the suspected file/path, batch local read-only checks,
and do not repeat an unchanged build or timed run without a reason.

## Build and launch

Visual Studio 2026 Build Tools with C++ is installed at:
`C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools`.

Do not infer that Visual Studio is missing from `PATH`. The shell may contain
both `Path` and `PATH`; raw parallel CMake builds can fail with duplicate
environment-key errors. Always use:

```powershell
.\tools\build-native.cmd -Configuration Release
```

The helper validates inputs, uses `vswhere.exe`/MSBuild, normalizes duplicate
environment keys only for its child process, and writes the full log to
`build\native\build-Release.log`.

Before building, verify these exist:

- `build\native\CMakeCache.txt`
- `build\native\openratchet.vcxproj`
- `build\extracted\PS2_MAIN.ELF`
- `generated`
- `third_party\PS2Recomp`

Inspect the cache and stop on any mismatch. It must point to this repository,
its `third_party\PS2Recomp` checkout, the extracted ELF, and the installed
Visual Studio generator. Do not delete the build directory, reconfigure
CMake, fetch dependencies, extract assets, or change tool paths silently.

Launch command:

```powershell
.\build\native\Release\openratchet.exe .\build\extracted\PS2_MAIN.ELF
```

The executable, ELF, generated output, and required runtime files/directories
must exist before launch.

## Native testing

The underlying harness command is:

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass `
    -File .\tools\run-native-test.ps1 -DurationSeconds 10
```

Use `-Build` to build first or `-KeepProcess` to leave the process running.
Report the working directory, exact command, build result, launch result,
runtime counters, relevant log lines/paths, acceptance result, regressions, and
remaining blockers for every native test.

The harness is evidence, not milestone acceptance. A surviving process, an open
window, nonzero DMA/GIF counters, a synthetic image, or the magenta fallback is
not an authentic rendering result.

## Milestone workflow

For exactly one milestone:

1. Inspect repository changes, build/cache state, generated output, logs, and
   runtime state before editing.
2. Reproduce the native symptom.
3. Compare the equivalent original behavior in Ghidra/PCSX2 when relevant.
4. Trace callers, state changes, synchronization, and related subsystems.
5. Identify the root cause; do not suppress warnings, bypass waits, or force
   fake output.
6. Implement the smallest correct fix in the proper layer.
7. Rebuild with the verified helper.
8. Run the native executable/harness and test related behavior for regressions.
9. Update `MILESTONES.md` with status, symptom, root cause, files changed,
   exact commands, tests/results, logs/counters, limitations, and the next
   concrete milestone.
10. Stop after the milestone is complete or at a clearly documented handoff.

A milestone is complete only when its acceptance criteria pass reproducibly.
For M2, that means guest-produced authentic framebuffer output, not a
hard-coded or forced framebuffer.

## Change boundaries

- Preserve existing user work; never reset, overwrite, or remove unrelated
  changes.
- Keep fixes minimal and avoid speculative features, broad refactors, new
  abstractions, and unrelated cleanup.
- `third_party\PS2Recomp` is read-only for OpenRatchet work. Do not edit it.
  Keep game-specific behavior in this repository using root-owned code and
  public runtime APIs. If a defect appears to require a third-party edit, stop
  and ask before changing it.
- Do not edit generated output unless investigation proves it is the correct
  fix location.
- Never create commits. The user reviews and creates one commit containing the
  changes from the chat/milestone.

## Handoff

At the end of a milestone or stopping point, state what changed, how to test it,
what remains, and one accurate suggested commit message. Do not start another
major milestone until the user has reviewed/committed the current work.
