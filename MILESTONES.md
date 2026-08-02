# OpenRatchet Milestones

Native PC recompilation of the original Ratchet & Clank PS2 game.

This file is the project memory. Update the checkbox and status after each meaningful milestone. Do not claim a milestone complete until its acceptance criteria pass.

## Current status

- Current phase: Milestone 4 — correct CPU/control flow.
- Native runner builds and opens a Windows OpenGL/audio window.
- The current test runner survives startup without fatal or missing-target errors during short runs.
- The game is not playable yet.
- Weapons do not need to be manually rewritten; translated original game logic should drive them.

## Milestones

### 0. Repository hygiene and legal boundary — DONE

- [x] Keep the user ISO out of commits with `.gitignore`.
- [x] Keep extracted data, saves, builds, logs, and generated sources out of commits.
- [x] Keep PS2Recomp and Wrench as Git submodules rather than vendoring their source.
- [ ] Document that users must provide their own legally dumped ISO.

Acceptance: the main repository contains source/scripts/docs and submodule pointers, not the ISO or generated output.

### 1. Reproducible ISO extraction — DONE

- [x] Validate ISO9660 structure.
- [x] Locate `SYSTEM.CNF`, `SCUS_971.99`, and `IOPRP243.IMG`.
- [x] Extract the boot ELF and write a manifest with hashes and ELF metadata.
- [x] Provide `self-test` and `verify` commands.

Acceptance: `python tools/openratchet.py extract --iso <path>` and `python tools/openratchet.py verify` pass.

### 2. Reproducible native build pipeline — DONE

- [x] Run PS2 ELF analysis.
- [x] Generate an automatic function map.
- [x] Add the currently known stripped-ELF jump-table/function-boundary exceptions.
- [x] Generate native C++ with PS2Recomp.
- [x] Copy generated sources into the runtime and build with CMake/Ninja.
- [x] Provide `python tools/native.py build`.

Acceptance: a clean generated `data/` directory can be rebuilt from the extracted ELF.

### 3. Native runtime bootstrap — DONE

- [x] Load the PS2 ELF into guest memory.
- [x] Initialize translated R5900 state and guest dispatch.
- [x] Initialize host graphics, audio, and timing.
- [x] Open a native Windows window.
- [x] Keep the process alive through the current startup path.

Acceptance: the native runner starts without immediately crashing or entering a missing-function loop.

### 4. CPU and control-flow correctness — ACTIVE

- [x] Fix the initial stripped-function boundary at `0x11DC18`.
- [x] Fix the system-startup function boundary at `0x11D798`.
- [x] Fix the startup function boundary at `0x11D9B8`.
- [x] Add the indirect/jump-table entry at `0x1E9658`.
- [ ] Replace heuristic exceptions with a proper Ghidra/exported function map.
- [ ] Validate MIPS delay slots and indirect calls across the startup path.
- [ ] Remove remaining missing-target errors during longer runs.
- [ ] Add a repeatable startup smoke test with captured error output.

Acceptance: startup runs for an extended test period with no missing guest targets, fatal runtime errors, or uncontrolled dispatch loops.

### 5. PS2 kernel and OS services — TODO

- [ ] Implement guest threads and scheduling semantics.
- [ ] Implement semaphores, alarms, event flags, and synchronization.
- [ ] Implement interrupt and TLB behavior needed by the game.
- [ ] Complete common libc and runtime service handlers.
- [ ] Define deterministic PS2 timing behavior on the host.

Acceptance: game initialization completes consistently without state corruption from host stubs.

### 6. CDVD and virtual filesystem — TODO

- [ ] Expose the user-provided ISO as a virtual CDVD device.
- [ ] Implement file open/read/seek/close behavior used by the game.
- [ ] Implement directory and overlay lookup.
- [ ] Support loading the first real game asset from the ISO.

Acceptance: original game code loads an asset and an executable overlay through the native CDVD layer.

### 7. Ratchet & Clank asset pipeline — TODO

- [ ] Build/use Wrench for R&C1 asset inspection.
- [ ] Unpack textures, levels, meshes, overlays, and executable packs as needed.
- [ ] Identify the minimum runtime asset formats required for the first level.
- [ ] Add native asset loading without duplicating the whole disc unnecessarily.

Acceptance: one known level’s real assets can be identified, decoded, and loaded by the native runtime.

### 8. GS renderer — TODO

- [ ] Implement GIF packet decoding.
- [ ] Implement GS register state.
- [ ] Implement PS2 VRAM and framebuffer behavior.
- [ ] Implement textures, blending, depth, alpha test, and display modes.
- [ ] Translate the required GS operations to the host graphics API.

Acceptance: the native runner displays an actual Ratchet & Clank frame, not only runtime/debug textures.

### 9. DMA, VIF, and VU — TODO

- [ ] Implement the DMA chains used by startup and rendering.
- [ ] Implement the required VIF packet behavior.
- [ ] Implement VU0 operations used by gameplay.
- [ ] Implement or translate the VU1 geometry/animation paths.
- [ ] Add fallback diagnostics for unsupported vector operations.

Acceptance: real geometry, characters, animation, particles, and effects render in a controlled test scene.

### 10. Input and frame timing — TODO

- [ ] Implement the PS2 pad API.
- [ ] Map keyboard and XInput/controller input.
- [ ] Implement stable fixed-step/frame timing.
- [ ] Implement pause, focus loss, and controller disconnect behavior.

Acceptance: Ratchet can move, jump, aim, and respond consistently to a controller.

### 11. Audio — TODO

- [ ] Implement the audio calls used by startup and gameplay.
- [ ] Decode/stream game music and sound effects through a host backend.
- [ ] Support voice playback and volume settings.
- [ ] Prevent audio thread/timing failures from blocking gameplay.

Acceptance: music, effects, voice, and volume controls work in a playable scene.

### 12. First playable vertical slice — TODO

- [ ] Load one real level.
- [ ] Render Ratchet and the environment.
- [ ] Implement movement, camera, jumping, collision, and damage.
- [ ] Make one enemy behave correctly.
- [ ] Make one weapon fire and interact with the enemy.
- [ ] Render the HUD and health/ammo state.
- [ ] Support death and reload.

Acceptance: a player can start and finish one complete gameplay section. Do not manually rewrite every weapon; preserve translated original logic and implement the services it calls.

### 13. Campaign progression — TODO

- [ ] Implement loading screens and level transitions.
- [ ] Support NPCs, missions, collectibles, shops, and scripted events.
- [ ] Support cutscenes and in-game cinematics.
- [ ] Support all required overlays and packed executables.
- [ ] Test campaign progression from the opening through the ending.

Acceptance: the complete campaign can progress from level to level without emulator fallback.

### 14. Saves and memory cards — TODO

- [ ] Implement `mc0` save APIs.
- [ ] Map saves to a native user directory.
- [ ] Support save, load, autosave, and deletion.
- [ ] Handle missing and corrupted saves safely.

Acceptance: a complete save/load cycle works across process restarts.

### 15. Full-game compatibility — TODO

- [ ] Test every level and boss.
- [ ] Test every weapon and gadget.
- [ ] Test menus, cutscenes, challenges, shops, and optional content.
- [ ] Test controller, audio, saves, and resolution settings.
- [ ] Track every remaining incompatibility with a reproducible case.

Acceptance: a full campaign playthrough succeeds on a clean user setup.

### 16. Performance and stability — TODO

- [ ] Profile translated hot paths.
- [ ] Remove unnecessary interpreter/fallback paths.
- [ ] Batch and cache rendering work.
- [ ] Reduce loading times and memory overhead.
- [ ] Add crash logs and useful guest-PC diagnostics.

Acceptance: stable frame pacing at the target resolution with no known runaway CPU or memory behavior.

### 17. User-facing release — TODO

- [ ] Make first-run setup request the user’s ISO path.
- [ ] Ensure no ISO or generated game data is distributed.
- [ ] Provide one documented build/run command.
- [ ] Provide controller, graphics, audio, and save configuration.
- [ ] Document supported Windows/toolchain requirements.

Acceptance: a new user can provide their own dump, build, run, play, and save without hidden manual steps.

### 18. Final cleanup — TODO

- [ ] Replace temporary map exceptions with proper analysis data where possible.
- [ ] Remove dead bring-up code and unused generated artifacts.
- [ ] Keep submodules clean and pinned.
- [ ] Update this file with final limitations and test results.

Acceptance: no fake runtime, no undocumented build steps, no generated files in commits, and no unresolved known blocker hidden from the user.

## Non-negotiable architecture

- Use static recompilation of the original MIPS game logic.
- Implement PS2 hardware/OS services on the host only where translated code requires them.
- Do not rewrite every weapon or gameplay system by hand.
- Do not ship the user’s ISO or copyrighted extracted assets.
- Prefer the smallest runtime implementation that passes the next milestone.

## Detailed test plan

Every milestone has two acceptance lanes:

- **Codex test:** an automated, reproducible check I can run in the workspace.
- **User test:** a manual check you can run from PowerShell and/or the native window.

When a milestone passes, record the date, command, result, and any relevant log or screenshot in the milestone’s checkbox notes.

### Test 0 — Repository hygiene and legal boundary

**Codex test**

```powershell
git status --short --ignored
git ls-files -s third_party/PS2Recomp third_party/wrench
git check-ignore -v games/*.iso data build mc0 mc1
```

Expected evidence: the ISO, generated data, builds, and saves are ignored; PS2Recomp and Wrench appear as mode `160000` submodule entries; no generated runtime source is staged in the main repository.

**User test**

Open the source-control changes view and confirm that only intentional source/docs changes are listed. Confirm the ISO, `data/`, `build/`, and save directories do not appear as commit candidates.

### Test 1 — Reproducible ISO extraction

**Codex test**

```powershell
python tools/openratchet.py self-test
python tools/openratchet.py extract --iso games\Ratchet.iso
python tools/openratchet.py verify
```

Expected evidence: `self-test: PASS`, `SCUS_971.99` is found, the manifest contains a SHA-256 hash and ELF entry point, and `verify` reproduces the same hash.

**User test**

Run extraction against your own ISO path. Open `data/manifest.txt` and confirm it identifies the expected game boot ELF. Run `verify` a second time; it must pass without modifying the ISO.

### Test 2 — Reproducible native build pipeline

**Codex test**

```powershell
python tools/native.py build
Test-Path build\ps2recomp-ninja\ps2xRuntime\ps2EntryRunner.exe
Select-String data\analysis\rc1.toml -Pattern ghidra_output
```

Expected evidence: analyzer completes, PS2Recomp reports zero decode failures, CMake links `ps2EntryRunner.exe`, and the generated config points at `auto-map.csv`.

**User test**

Run the same command after extraction. A successful build ends with a linked `ps2EntryRunner.exe`; no manual copying of generated C++ files should be necessary.

### Test 3 — Native runtime bootstrap

**Codex test**

Launch the runner as a child process for a fixed interval, capture stderr, and assert:

- process stays alive for the interval;
- OpenGL initializes;
- audio initializes when available;
- no `fatal`, `No exact recompiled function`, `missing-target`, or `unimplemented` error appears.

The process may be terminated by the test harness after the interval; that is expected for a smoke test.

**User test**

```powershell
python tools/native.py run
```

Confirm a native window opens and remains responsive for at least one minute. At this stage a blank/debug frame is acceptable; a playable game is not yet required.

### Test 4 — CPU and control-flow correctness

**Codex test**

Run the startup smoke test repeatedly with a longer timeout and collect every guest-PC diagnostic. Verify that known entries such as `0x11DC18` and `0x1E9658` are present in the generated output. Fail the test on any missing target or dispatch loop.

```powershell
Select-String data\analysis\rc1.toml -Pattern ghidra_output
Get-ChildItem data\analysis\output -Filter '*11dc18*'
Get-ChildItem data\analysis\output -Filter '*1e9658*'
```

**User test**

Run the native build and startup repeatedly. Report the first guest PC if the window closes, freezes, or prints a `guest-branch` error. The same guest PC must not reappear after it is fixed.

### Test 5 — PS2 kernel and OS services

**Codex test**

Add focused runtime tests for each service group: thread creation, yield/sleep, semaphore wait/signal, alarm delivery, event flags, and memory allocation. Run them both in isolation and during game startup. Assert no deadlocks, negative timeout loops, or invalid guest-memory accesses.

**User test**

Let the game run through its longest available initialization path. Confirm it does not hang on a loading screen, spin at 100% CPU, or randomly reset. Repeat startup three times to catch timing-dependent failures.

### Test 6 — CDVD and virtual filesystem

**Codex test**

Create a virtual-disc test that opens, reads, seeks, and closes known files from the ISO. Compare returned bytes and sizes against direct ISO extraction. Then enable a load trace and assert that the game requests are satisfied without host-file fallbacks.

**User test**

Start from the user-provided ISO and watch the load path. The native runner must load data from the ISO configured by the user, not from an accidentally retained developer directory. A missing-file message must name the requested disc path.

### Test 7 — Ratchet & Clank asset pipeline

**Codex test**

Use Wrench to unpack one known R&C1 texture, mesh, level file, and overlay. Repack or round-trip each asset where supported, then compare metadata and dimensions. Add a native loader test for those same files.

**User test**

Open the selected extracted asset with an ordinary viewer or Wrench tooling. Confirm textures are not empty, meshes have sensible dimensions, and the asset corresponds to the expected R&C1 level rather than random ISO data.

### Test 8 — GS renderer

**Codex test**

Run deterministic renderer tests for: clear, textured quad, alpha blend, depth ordering, framebuffer copy, palette texture, and a known GS packet trace. Compare output against reference images with a defined pixel-difference threshold.

**User test**

The native window must show a real game frame containing recognizable Ratchet & Clank content. Check correct aspect ratio, no completely black frame, no corrupted textures, and no severe flickering while the camera or scene changes.

### Test 9 — DMA, VIF, and VU

**Codex test**

Run instruction-level tests for every VU/DMA operation used by the first real scene. Feed captured packet traces and compare output guest memory, GIF packets, and transformed vertices against reference results. Log unsupported operations with the exact microprogram and address.

**User test**

Look for actual geometry, animated characters, particles, and effects. Rotate or move the camera and confirm objects do not disappear, explode into triangles, or freeze in their bind pose.

### Test 10 — Input and frame timing

**Codex test**

Inject a deterministic input sequence and assert that the guest pad API receives the expected button, analog-stick, and trigger states at the expected frames. Measure frame intervals and reject runaway timing or input latency.

**User test**

Connect an XInput controller. Confirm movement, camera, jump, attack, weapon selection, pause, and analog controls work. Hold a button, release it, alt-tab, and reconnect the controller to check edge behavior.

### Test 11 — Audio

**Codex test**

Trigger a known sound effect, music stream, voice clip, pause, resume, and volume change. Assert that the audio queue drains without underrun storms, deadlocks, or crashes when the output device is unavailable.

**User test**

Confirm audible music, effects, and voice in a real scene. Test mute/volume changes, alt-tab, and starting the runner without an audio device. Graphics must remain alive if audio initialization fails.

### Test 12 — First playable vertical slice

**Codex test**

Use a deterministic input replay or scripted controller sequence to validate: level load, Ratchet spawn, movement, jump, camera, collision, one enemy, one weapon, HUD values, damage, death, and reload. Store a screenshot or state hash at each checkpoint.

**User test**

Play the selected section manually from start to finish. Verify that Ratchet moves, attacks, takes damage, dies, reloads, and can complete the objective. Test at least one weapon without expecting each weapon to have handwritten native code.

### Test 13 — Campaign progression

**Codex test**

Build a regression matrix with one smoke checkpoint per level, overlay, boss, cutscene, shop, and major scripted event. Automate loading each checkpoint and fail on missing assets, guest-PC errors, or corrupted rendering.

**User test**

Play through the campaign in order. Confirm level transitions, missions, NPCs, collectibles, shops, cutscenes, and boss encounters work. Record the first broken level rather than skipping it.

### Test 14 — Saves and memory cards

**Codex test**

Run save API tests for create, write, read, overwrite, delete, missing directory, full storage, and corrupted data. Start a fresh process between save and load to verify persistence.

**User test**

Save in-game, close the native runner, reopen it, and load the save. Test autosave, deletion, and a deliberately copied/invalid save. The game must not overwrite unrelated files.

### Test 15 — Full-game compatibility

**Codex test**

Run the complete regression matrix from Milestones 8–14. Record pass/fail by level, weapon, boss, cutscene, controller action, audio event, and save operation. No “mostly works” result qualifies as complete.

**User test**

Complete a full campaign playthrough on a clean user-provided ISO. Test optional content and every weapon/gadget category. Keep the native log enabled and report any first failure with its level and action.

### Test 16 — Performance and stability

**Codex test**

Run a fixed 10-minute route with profiling enabled. Record frame time percentiles, memory usage, translated/interpreted instruction counts, loading times, and crash count. Repeat the route three times and check for leaks or drift.

**User test**

Play for at least 30 minutes across multiple levels. Check frame pacing, audio synchronization, loading time, controller responsiveness, memory growth, and whether performance degrades over time.

### Test 17 — User-facing release

**Codex test**

Use a clean temporary checkout with no `data/` or build output. Provide an ISO path, run extraction, build, and launch. Assert that no ISO is present in the repository and that all generated files remain ignored.

**User test**

Follow only the README instructions on a separate Windows machine or user profile. Confirm the setup explains the ISO requirement, finds the toolchain, builds successfully, launches, and tells the user where saves/configuration live.

### Test 18 — Final cleanup

**Codex test**

```powershell
git status --short
git diff --check
python tools/openratchet.py self-test
```

Verify that submodules are pinned and clean, generated files are ignored, no fake runtime remains, and the documented commands still work.

**User test**

Review the final changes in the source-control view. Confirm every changed file is intentional, the milestones accurately describe reality, and a new chat can continue from the current active milestone without additional explanation.
