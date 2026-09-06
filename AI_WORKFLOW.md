# OpenRatchet AI Engineering Workflow

This document defines how AI agents should work on OpenRatchet. It complements
`MILESTONES.md` and `ARCHITECTURE.md`; those files remain authoritative for
project state and architecture.

## Optimize for progress, not patch count

The goal is the largest amount of **verified architectural progress per change**,
not the largest number of fixes. Prefer one root-cause correction that removes a
class of failures over many local workarounds.

1. **Find the highest-leverage boundary first.** Before changing code, decide
   whether the observed failure belongs to retail game semantics, an already
   planned native platform boundary, or PS2Recomp/runtime infrastructure.
2. **Root cause before symptom fixes.** Never hide a runtime/recompiler bug with
   a game HLE. Never fake game globals merely to move the sampled PC.
3. **Two-fix stop rule.** If two consecutive fixes merely reveal another startup
   blocker of the same kind, stop before a third. Re-evaluate the abstraction
   level and search for a shared cause or a higher native boundary.
4. **Sampled PC is evidence, not an oracle.** A stable debug PC does not prove
   that execution below nested guest dispatch is stalled. Prefer direct semantic
   checkpoints, producer/consumer writes and proved call/return paths.
5. **Use direct phase gates.** Instrument the exact state the milestone requires
   (for example authentic Moby-pool publication) instead of expanding generic
   startup logging.
6. **Minimize scope.** Touch the fewest subsystems and files necessary. Do not
   bundle unrelated cleanup, refactors or speculative future infrastructure.
7. **No fake progress.** Do not synthesize Moby pools, game globals, controller
   input, save data or success states unless the exact higher-level contract and
   corresponding retail outcome are proved.
8. **Prefer semantic HLE boundaries over transport emulation.** When a PS2-only
   transport exists beneath an already-understood game/platform API, replace the
   API boundary. Do not recreate SIF/IOP packet machinery solely to pass startup.
9. **Prove critical runtime fixes counterfactually.** Where practical, add a
   regression that fails on the pre-fix runtime and passes after the fix. A game
   merely running farther is supporting evidence, not the only proof.
10. **Temporary probes never ship.** Watchpoints, verbose tracing and analysis
    shims belong only in the investigation environment unless they are promoted
    into a deliberate diagnostic feature with tests.
11. **Stop archaeology when the gate is reached.** Once the current milestone's
    authentic semantic invariant is satisfied, move to the next milestone. Do
    not keep replacing unrelated startup infrastructure "while we are here".
12. **Keep docs synchronized with verified reality.** Update milestone claims
    only after the corresponding build/test/runtime evidence exists.
13. **Third-party checkouts are immutable inputs.** Do not hand-edit repositories
    under `third_party/` to make OpenRatchet work. If a genuine upstream/runtime
    defect must be carried temporarily, keep an upstream-ready patch owned by
    OpenRatchet and apply it only to a deterministic build-local copy. The pinned
    third-party checkout must remain clean so dependency state is reproducible.
14. **Do not manufacture rebuild work.** Build helpers may compensate for stale
    archive timestamps, but only when file content actually changed. Never touch
    unchanged dirty headers on every validation cycle; that can invalidate the
    PCH and needlessly rebuild hundreds of generated Retail translation units.

## Decision checklist before every patch

- What exact invariant is currently false?
- Which layer owns that invariant?
- Is there a single upstream cause explaining multiple symptoms?
- Can the hypothesis be disproved before coding?
- What is the smallest sound boundary that fixes the cause?
- What exact test will fail before and pass after?
- Does this change advance the current milestone directly?

If the last answer is no, do not make the change unless it is a prerequisite
proved necessary for the milestone.
