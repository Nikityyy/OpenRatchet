# OpenRatchet

**Native PC port of Ratchet & Clank (PS2, 2002) via static binary recompilation.**

OpenRatchet translates the original MIPS R5900 game executable into native C++20 code using the [PS2Recomp](https://github.com/ran-j/PS2Recomp) toolchain, then runs it against a custom Hardware Abstraction Layer (HAL) built on SDL2 and Vulkan. No emulator. No dynamic translation. The original game logic executes natively on your PC.

## Legal Notice

This project does **not** contain any copyrighted game code, assets, or data. You must supply your own legally dumped PS2 ISO of *Ratchet & Clank* (SCUS-97199). The ISO is never committed to the repository.

## Architecture Overview

```
┌─────────────────────────────────────────────────┐
│               OpenRatchet Native                │
├─────────────┬───────────────────┬───────────────┤
│  Recompiled │   HAL Services    │   Renderer    │
│  Game Logic │  (EE Memory,      │  (Vulkan /    │
│  (C++20     │   BIOS/Kernel,    │   Compute     │
│   from MIPS │   IOP/SIF, DMA,   │   Shaders)    │
│   R5900)    │   Timers, Pad)    │               │
├─────────────┼───────────────────┼───────────────┤
│         SDL2 (Window, Input, Audio)             │
├─────────────────────────────────────────────────┤
│              Host OS (Windows/Linux)            │
└─────────────────────────────────────────────────┘
```

## Status

🚧 **Early Development** — See [CLAUDE.md](CLAUDE.md) for the full milestone plan.

## Prerequisites

- Python 3.10+
- CMake 3.22+, Ninja
- Visual Studio 2022 (MSVC v143) or Clang 16+
- Vulkan SDK 1.3+
- SDL2 2.28+
- [Ghidra](https://ghidra-sre.org/) 11+ with [ghidra-emotionengine-reloaded](https://github.com/chaoticgd/ghidra-emotionengine-reloaded)
- A legally dumped R&C1 PS2 ISO (`SCUS_971.99`)

## Quick Start

```powershell
# 1. Clone with submodules
git clone --recursive https://github.com/YourUser/OpenRatchet.git
cd OpenRatchet

# 2. Place your ISO
cp /path/to/ratchet.iso games/

# 3. Follow the milestone steps in CLAUDE.md
```

## Project Structure

```
OpenRatchet/
├── CLAUDE.md           # Detailed milestone plan (step-by-step build guide)
├── README.md           # This file
├── .gitignore
├── games/              # Place your ISO here (ignored)
├── data/               # Extracted runtime data (ignored, generated)
├── src/
│   ├── hal/            # Hardware Abstraction Layer (EE memory, MMIO, timers)
│   ├── renderer/       # Vulkan renderer, GS emulation, VU1 compute shaders
│   ├── kernel/         # PS2 BIOS/kernel syscall implementations
│   ├── iop/            # IOP processor HLE (SIF, CDVD, SPU2, PAD)
│   ├── recompiled/     # Auto-generated recompiled C++ (from PS2Recomp)
│   └── main.cpp        # Native entry point and main loop
├── include/            # Public headers
├── shaders/            # GLSL/HLSL compute and fragment shaders
├── tools/
│   ├── extract.py      # ISO extraction and asset unpacking
│   ├── analyze.py      # Ghidra integration and function map generation
│   └── build.py        # Build automation
├── third_party/        # Git submodules (PS2Recomp, SDL2, etc.)
├── tests/              # Verification and ground-truth comparison
└── docs/               # Technical documentation
```

## Contributing

See [CLAUDE.md](CLAUDE.md) for the implementation roadmap. Each milestone is a self-contained, committable step.

## License

This project is licensed under the MIT License. Game assets are not included and remain the property of Sony Interactive Entertainment / Insomniac Games.
