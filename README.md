# OpenRatchet

Native PC recompilation work for the original Ratchet & Clank PS2 release.

## Build and run

Requirements: Python 3, CMake, Ninja, and a Visual Studio C++ toolchain.
Put a legally dumped ISO in `games/` or pass one explicitly to the extractor.

```powershell
python tools/openratchet.py self-test
python tools/openratchet.py extract --iso games\Ratchet.iso
python tools/native.py build
python tools/native.py smoke --seconds 15
python tools/native.py run
```

Pass `--iso path\to\game.iso` when the dump is not the only `.iso` in
`games/`. The runner receives the image directly for CDVD reads; it does not
copy the disc into the repository.

`tools/native.py` drives PS2Recomp, generates the stripped-ELF function map,
copies the generated C++ into the PS2 runtime, and builds
`build/ps2recomp-ninja/ps2xRuntime/ps2EntryRunner.exe`. Build dependencies are
expected under `third_party/PS2Recomp`; generated extraction and recompilation
files stay under ignored `data/`.

The current milestone is a native executable that initializes the PS2 runtime,
opens a Windows OpenGL window, and stays alive through the game startup path.
It is not yet a complete playable port: GS/VU behavior, asset loading, input,
audio, and remaining indirect control-flow cases still need implementation.

The practical route is static recompilation, not re-coding every weapon. Game
logic remains the original MIPS code translated to native C++; only PS2
hardware and OS services need host implementations.
