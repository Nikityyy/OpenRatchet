# OpenRatchet — Master Implementation Plan

> **Strategy**: Binary Static Recompilation (PS2Recomp) + Native C++20 HAL (SDL2/Vulkan)
>
> **Target**: Ratchet & Clank (PS2, 2002) — `SCUS_971.99` — MIPS R5900 EE CPU
>
> **Goal**: Native PC executable with exact physics parity, no emulation overhead, uncapped framerate

---

## How To Use This File

This file is the **single source of truth** for building OpenRatchet. It is organized into
**Milestones** (large, committable steps). Each milestone contains **Tasks** (individual units of work).

### Rules for the AI Agent

1. **Work one milestone at a time.** Do not skip ahead.
2. **When a milestone is complete**, stop coding. Report:
   - What you did
   - Why you did it
   - How you tested it
   - A suggested git commit message (format: `milestone-N: description`)
3. **Do NOT commit.** The human reviews the code and commits manually.
4. **Only proceed to the next milestone when the human says "okay" or "proceed".**
5. **Mark completed tasks** with `[x]` and in-progress tasks with `[/]`.
6. **Never commit ISOs, extracted assets, generated C++ sources, build artifacts, or log files.**
7. **Keep the original translated game logic intact.** Implement host PS2 services that it calls.
8. **Do not manually rewrite game logic** (weapons, enemies, physics, animations).
9. **When something fails**, record the exact error, guest PC address, and smallest reproducer.
10. **Test after every file you create.** At minimum, verify it compiles or passes a self-test.
11. **Always test on the real ISO file** in the `games/` folder (or `data/raw/` extracted files) if one is available. Never just assume fallback logic is enough; if you do not test on real data, it will never work.
12. **Ensure dependencies are built and functional.** If a tool like `ps2xAnalyzer.exe` or `ps2xRecomp.exe` is missing from the PATH, you must fetch it (e.g., clone into `third_party/`), build it, and place it in the PATH to perform a real test.
### Rules for the Human

1. After each milestone, review the diff and commit with the suggested message.
2. Push to GitHub so progress is saved.
3. If a milestone needs changes, tell the AI what to fix before approving.

---

## Architecture Reference

### PS2 Hardware Components We Must Abstract

| PS2 Component | Address Range / Role | Our Abstraction |
|---|---|---|
| **Emotion Engine (EE) CPU** | MIPS R5900, 128-bit GPRs, MMI | Recompiled to native C++20 by PS2Recomp |
| **EE Main RAM** | `0x00000000` – `0x01FFFFFF` (32 MB) | Contiguous `uint8_t[32MB]` array |
| **EE Scratchpad** | `0x70000000` – `0x70003FFF` (16 KB) | Separate `uint8_t[16KB]` array |
| **EE MMIO Registers** | `0x10000000` – `0x1000FFFF` | Intercepted write handlers |
| **VU0 (Macro Mode)** | COP2 instructions in EE stream | Translated inline by PS2Recomp |
| **VU1 (Micro Mode)** | Custom microcode programs | Offline-translated to Vulkan Compute Shaders |
| **GS (Graphics Synthesizer)** | `0x12000000` – `0x12001FFF` | Vulkan renderer translating GS register state |
| **GIF (GS Interface)** | `0x10003000` – `0x10003FFF` | GIF packet parser → Vulkan draw calls |
| **VIF0 / VIF1** | `0x10003800` – `0x10003FFF` | VIF unpack → GPU buffer uploads |
| **DMA Controller** | `0x10008000` – `0x1000EFFF` | DMA chain walker dispatching to GIF/VIF/SIF |
| **IOP (I/O Processor)** | Separate R3000A CPU | HLE module system (CDVD, PAD, SPU2, SIF) |
| **SIF (Sub-CPU Interface)** | RPC bridge EE ↔ IOP | Direct function call dispatch |
| **CDVD** | Disc read subsystem | Native async file I/O on extracted assets |
| **SPU2** | Sound processor | SDL2 audio with ADPCM decoding |
| **PAD (Controller)** | SIO2 → pad buffers | SDL2 GameController → guest pad memory |
| **Timers** | EE Timer 0–3, VBlank INT0 | `std::chrono` high-resolution timers |
| **INTC** | `0x1000F000` | Software interrupt dispatch table |

### Floating-Point Compatibility (CRITICAL)

The PS2 EE FPU does **not** conform to IEEE 754:
- **No NaN/Infinity**: overflows clamp to `FLT_MAX` (`0x7F7FFFFF`)
- **Flush-to-Zero (FTZ)**: denormals become `+0.0f`
- **Truncation rounding**: round-toward-zero, not round-to-nearest-even

We MUST configure the host x86 MXCSR register at thread entry:
```cpp
#include <xmmintrin.h>
#include <pmmintrin.h>

void InitPS2FloatMode() {
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
    _MM_SET_ROUNDING_MODE(_MM_ROUND_TOWARD_ZERO);
}
```

And for overflow-prone paths:
```cpp
inline float ClampPS2Float(float value) {
    if (std::isinf(value)) return (value > 0.0f) ? 3.402823466e+38f : -3.402823466e+38f;
    if (std::isnan(value)) return 0.0f;
    return value;
}
```

### R&C1 Geometry Types (from Insomniac's Engine)

| Type | Description | VU1 Program | Purpose |
|---|---|---|---|
| **tfrag** | Terrain fragment | Custom vertex transform | Planetary terrain chunks, streamed |
| **tie** | Tied instance | Instanced transform | Buildings, architecture, rigid statics |
| **shrub** | Shrub instance | Alpha-blend transform | Vegetation, detail foliage |
| **moby** | Mobile object | Skeletal/rigid transform | Actors, enemies, weapons, collectibles |

---

## External Tools & Dependencies

| Tool | Repository / Source | What We Use It For |
|---|---|---|
| **PS2Recomp** | `https://github.com/ran-j/PS2Recomp` | Static MIPS→C++20 recompilation (ps2xAnalyzer, ps2xRecomp, ps2xRuntime) |
| **Ghidra** | `https://ghidra-sre.org/` | PS2 ELF disassembly and function boundary analysis |
| **ghidra-emotionengine-reloaded** | `https://github.com/chaoticgd/ghidra-emotionengine-reloaded` | MIPS R5900/MMI/VU0 instruction support for Ghidra |
| **ExportPS2Functions.java** | Part of PS2Recomp repo | Ghidra script to export function map as TOML |
| **Wrench** | `https://github.com/chaoticgd/wrench` | R&C level archive inspection and asset extraction |
| **rac-dvd-toc-parser** | `https://github.com/maikelwever/rac-dvd-toc-parser` | Hidden file/sector extraction from R&C PS2 ISOs |
| **SDL2** | `https://libsdl.org/` | Window, input, audio, Vulkan surface creation |
| **Vulkan SDK** | `https://vulkan.lunarg.com/` | GPU rendering, compute shaders for VU1 translation |
| **VulkanMemoryAllocator** | `https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator` | Efficient GPU memory management |

---

## Milestones

---

### Milestone 0: Clean Repository Foundation

**Goal**: Establish the project skeleton with proper directory structure, build system, documentation, and legal boundaries so everything built afterwards has a home.

**Commit name**: `milestone-0: clean repository foundation with build system and project structure`

#### Tasks

- [x] **0.1** Create the directory tree:
  ```
  OpenRatchet/
  ├── src/
  │   ├── hal/           # Hardware Abstraction Layer
  │   ├── renderer/      # Vulkan renderer
  │   ├── kernel/        # PS2 kernel/BIOS syscalls
  │   ├── iop/           # IOP HLE modules
  │   ├── recompiled/    # Will hold generated C++ (empty placeholder)
  │   └── main.cpp       # Native entry point (stub)
  ├── include/
  │   └── openratchet/   # Public headers
  ├── shaders/           # GLSL compute/fragment shaders (empty)
  ├── tools/             # Python build/extract scripts
  ├── third_party/       # Git submodules
  ├── tests/             # Verification tests
  ├── docs/              # Technical docs
  └── games/             # User places ISO here (ignored)
  ```
- [x] **0.2** Create `CMakeLists.txt` at the project root:
  - Set `cmake_minimum_required(VERSION 3.22)`
  - Set `project(OpenRatchet LANGUAGES CXX)`
  - Set `CMAKE_CXX_STANDARD 20`, `CMAKE_CXX_STANDARD_REQUIRED ON`
  - Add `src/main.cpp` as the executable target `openratchet`
  - Add placeholder `find_package(Vulkan REQUIRED)` and `find_package(SDL2 REQUIRED)` (can be skipped initially with a CMake option `OPENRATCHET_SKIP_DEPS`)
  - Add `include_directories(include/)`
  - Set up `add_subdirectory()` for `src/hal`, `src/renderer`, `src/kernel`, `src/iop`
  - Each subdirectory gets its own `CMakeLists.txt` creating a static library
- [x] **0.3** Create `src/main.cpp` with a minimal stub:
  ```cpp
  #include <iostream>
  
  int main(int argc, char* argv[]) {
      std::cout << "OpenRatchet — Ratchet & Clank Native PC Port" << std::endl;
      std::cout << "Status: Milestone 0 — Foundation" << std::endl;
      return 0;
  }
  ```
- [x] **0.4** Create placeholder `CMakeLists.txt` files in each `src/` subdirectory:
  - `src/hal/CMakeLists.txt` → library `openratchet_hal` (no sources yet, just a placeholder `.cpp`)
  - `src/renderer/CMakeLists.txt` → library `openratchet_renderer`
  - `src/kernel/CMakeLists.txt` → library `openratchet_kernel`
  - `src/iop/CMakeLists.txt` → library `openratchet_iop`
- [x] **0.5** Verify the project configures and builds:
  ```powershell
  cmake -S . -B build -G Ninja -DOPENRATCHET_SKIP_DEPS=ON
  cmake --build build
  ./build/openratchet.exe
  # Should print the status message
  ```
- [x] **0.6** Ensure `.gitignore` covers: `build/`, `data/`, `games/*.iso`, `mc0/`, `mc1/`, `imgui.ini`, `*.spv`, `__pycache__/`, IDE files.
- [x] **0.7** Ensure `README.md` documents the project, architecture, and prerequisites.
- [x] **0.8** Create empty placeholder files:
  - `games/.gitkeep`
  - `shaders/.gitkeep`
  - `tests/.gitkeep`
  - `docs/.gitkeep`
  - `src/recompiled/.gitkeep`

#### Acceptance Criteria

```powershell
cmake -S . -B build -G Ninja -DOPENRATCHET_SKIP_DEPS=ON
cmake --build build
.\build\openratchet.exe    # prints status message
git status --short          # no untracked generated files
```

---

### Milestone 1: ISO Ingestion and Asset Extraction Pipeline

**Goal**: Build a Python tool that mounts a PS2 R&C1 ISO, parses the ISO9660 filesystem, extracts `SCUS_971.99` (the boot ELF), locates the R&C1 table of contents, and extracts all level WADs and associated binary ranges into `data/raw/`. This provides all game data the recompiled native executable will need.

**Commit name**: `milestone-1: ISO ingestion and asset extraction pipeline`

#### Background

The R&C1 PS2 ISO uses standard ISO9660 for the filesystem layer, but Insomniac hid level data in raw sectors outside the filesystem. The game's hidden table of contents starts at **LSN 1500** and contains sector offsets and sizes for each level's assets. Each level entry has a 0x2434-byte header containing offsets to `data.bin`, `gameplay_ntsc.bin`, `gameplay_pal.bin`, and `occlusion.bin`.

#### Tasks

- [x] **1.1** Create `tools/extract.py` implementing an `ISO9660` class:
  - Open the ISO file in binary read mode
  - Parse the Primary Volume Descriptor at sector 16 (magic `\x01CD001`)
  - Extract the root directory record (offset 156, length 34 bytes in the PVD)
  - Root directory extent LSN is at byte offset 2 (little-endian uint32)
  - Root directory size is at byte offset 10 (little-endian uint32)
  - Recursively walk directory records:
    - Each record starts with a length byte; if 0, skip to next sector boundary
    - Record structure: `[length:1][ext_attr:1][extent_lsn:4+4][size:4+4][date:7][flags:1][unit:1][gap:1][volseq:2+2][name_len:1][name:var]`
    - Little-endian values are at offsets 2 (extent), 10 (size)
    - Flag bit 1 (0x02) = directory
    - Skip entries with name `\x00` (self) and `\x01` (parent)
    - Strip `;1` version suffix from filenames
  - Provide methods: `records() → List[FileRecord]`, `copy(record, dest_path)`, `copy_range(lsn, size, dest_path)`
  - Each `FileRecord` has: `path: str`, `lsn: int`, `size: int`

- [x] **1.2** Implement R&C1 hidden TOC parsing in `tools/extract.py`:
  - Read 8 bytes at `LSN 1500 * 2048`: expect `uint32 magic = 1`, `uint32 toc_size`
  - Read `toc_size` bytes from that same offset
  - Walk entries every 8 bytes starting at offset 8: `uint32 header_lsn`, `uint32 header_sectors`
  - For each non-zero entry, read the 0x2434-byte level header at `header_lsn * 2048`
  - Validate: `uint32` at offset 4 must equal `0x2434`
  - Extract sub-ranges from the header:
    - `data.bin`: offset 8 → `(uint32 start_sector, uint32 sector_count)`
    - `gameplay_ntsc.bin`: offset 16
    - `gameplay_pal.bin`: offset 24
    - `occlusion.bin`: offset 32
  - Level ID is `int32` at offset 0 of the header
  - Compute the overall level range: `low = min(start)`, `high = max(start + count)`
  - Return a list of `(level_id, low_sector, high_sector, [(name, start, count)])`

- [x] **1.3** Implement the ELF parser function `parse_elf(path)`:
  - Validate ELF magic (`\x7fELF`), class 1 (32-bit), little-endian, machine 8 (MIPS)
  - Read entry point (offset 24, uint32), program header offset (28, uint32), phentsize (42, uint16), phnum (44, uint16)
  - For each program header of type `PT_LOAD` (1): extract `p_vaddr`, `p_filesz`, `p_memsz`
  - Return `(entry_point, phnum, [(vaddr, filesz, memsz)])`

- [x] **1.4** Implement the `extract` CLI command:
  - Accept `--iso PATH`, `--out PATH` (default `data`), `--all` flag
  - Without `--all`: extract only `SYSTEM.CNF`, `SCUS_971.99`, `IOPRP243.IMG`
  - With `--all`: extract all ISO files + all discovered level WAD ranges
  - Write a `data/manifest.txt` with: ISO path, sector size, boot ELF path/LSN/size/SHA256, entry point, segment info, and a file table
  - Level WADs go to `data/raw/levels/{level_id}/level.wad` and sub-ranges go to `data/raw/levels/{level_id}/{name}`

- [x] **1.5** Implement `verify` command: re-parse the boot ELF, compare SHA256 against manifest
- [x] **1.6** Implement `toc` command: print the R&C1 TOC summary (level IDs, sectors)
- [x] **1.7** Implement `self-test` command: create a synthetic ELF in memory, run `parse_elf`, verify results. Create a synthetic ISO range, verify `copy_range` works.
- [x] **1.8** Implement `locate_iso(requested, root)`: if `--iso` given, use it; otherwise find the single `.iso` in `games/`.

#### File Details

**`tools/extract.py`** — approximately 350–400 lines. Standalone script, no external dependencies beyond Python stdlib. All struct operations use `struct.unpack_from` with little-endian format strings.

#### Acceptance Criteria

```powershell
python tools/extract.py self-test
# "self-test: PASS"

python tools/extract.py extract --iso games/Ratchet.iso --all
# Should create data/raw/SCUS_971.99, data/raw/levels/0/level.wad, data/manifest.txt

python tools/extract.py verify --data data
# "verified: data/raw/SCUS_971.99"

python tools/extract.py toc --iso games/Ratchet.iso
# Should list level IDs and sector ranges

Test-Path data/raw/SCUS_971.99         # True
Test-Path data/raw/levels/0/level.wad  # True
Test-Path data/manifest.txt            # True
```

---

### Milestone 2: Ghidra Analysis and Function Map Generation

**Goal**: Set up the disassembly pipeline that takes the extracted `SCUS_971.99` ELF, runs it through Ghidra with the Emotion Engine plugin, exports function boundaries, and generates the TOML/CSV configuration file that PS2Recomp needs to translate MIPS assembly into C++20.

**Commit name**: `milestone-2: Ghidra analysis pipeline and function map generation`

#### Background

The R&C1 ELF is a stripped binary — no debug symbols, no symbol table. Ghidra with the `ghidra-emotionengine-reloaded` plugin can still identify function boundaries through control flow analysis, prologue/epilogue detection, and cross-reference analysis. The `ExportPS2Functions.java` Ghidra script (from the PS2Recomp repo) exports these boundaries as a structured file.

PS2Recomp's `ps2xAnalyzer` can also do a preliminary analysis pass that generates a TOML config. Then `ps2xRecomp` reads that config and produces C++20 source files.

#### Tasks

- [x] **2.1** Create `tools/analyze.py` — orchestrates the analysis pipeline:
  - Check that `data/raw/SCUS_971.99` exists (run extract first if not)
  - Locate or configure Ghidra installation path (environment variable `GHIDRA_HOME` or command-line arg)
  - Run Ghidra in headless mode to analyze the ELF:
    ```
    {GHIDRA_HOME}/support/analyzeHeadless <project_dir> <project_name>
        -import data/raw/SCUS_971.99
        -processor MIPS:LE:32:R5900
        -postScript ExportPS2Functions.java <output_path>
    ```
  - Parse the Ghidra export output into a normalized function map

- [x] **2.2** Implement fallback analysis using `ps2xAnalyzer`:
  - If Ghidra is not available, use PS2Recomp's own analyzer:
    ```
    ps2xAnalyzer.exe data/raw/SCUS_971.99 data/analysis/rc1.toml
    ```
  - This generates a TOML config with auto-detected function boundaries
  - The TOML config format:
    ```toml
    [general]
    input = "data/raw/SCUS_971.99"
    output = "data/analysis/output/"
    single_file_output = false
    patch_syscalls = false
    patch_cop0 = true
    patch_cache = true
    ```

- [x] **2.3** Implement `make_function_map()`:
  - After the auto-analysis pass, parse the generated C++ files to extract function boundaries
  - Each generated `.cpp` file has a header comment: `// Function: <name>\n// Address: 0x<start> - 0x<end>`
  - Parse these with regex: `r"(?m)^// Function: (.+)\n// Address: 0x([0-9a-f]+) - 0x([0-9a-f]+)"`
  - Known manual exceptions for the R&C1 stripped ELF (functions the auto-scanner misses):
    - `0x0011DC18` — `0x0011DCC8` (size `0xB0`) — `sub_0011DC18`
    - `0x001E9488` — `0x001E9658` (size `0x1D0`) — `sub_001E9488`
    - `0x001E9658` — `0x001E9AB8` (size `0x460`) — `sub_001E9658`
  - Write the combined map as a CSV: columns `name, address, end, size`
  - Save to `data/analysis/auto-map.csv`

- [x] **2.4** Implement `prepare_recomp_config()`:
  - Read the TOML config generated by ps2xAnalyzer
  - Inject the `ghidra_output` field pointing to the auto-map CSV
  - Remove any stale function overrides (e.g., `InitExecPS2@0x0011D9B8`)
  - Write the updated config back

- [x] **2.5** Document the complete analysis pipeline in `docs/analysis.md`:
  - Ghidra setup instructions (install EE plugin, locate `analyzeHeadless`)
  - How to run headless analysis
  - How the function map is generated
  - Known manual exceptions and why they exist
  - How to add new exceptions when encountered

#### Acceptance Criteria

```powershell
# With ps2xAnalyzer available:
python tools/analyze.py --elf data/raw/SCUS_971.99
Test-Path data/analysis/rc1.toml       # True
Test-Path data/analysis/auto-map.csv   # True

# The auto-map.csv should contain the known exceptions
Select-String "0x0011DC18" data/analysis/auto-map.csv  # Found
```

---

### Milestone 3: Static Recompilation — MIPS R5900 to C++20

**Goal**: Run PS2Recomp's `ps2xRecomp` to translate the entire `SCUS_971.99` ELF into native C++20 source files using the function map from Milestone 2. Set up the build system to compile these generated sources alongside the HAL.

**Commit name**: `milestone-3: static recompilation of MIPS R5900 to C++20`

#### Background

PS2Recomp reads the TOML configuration (which points to the ELF binary and the function map), decodes each MIPS R5900 instruction, and emits equivalent C++20 code. Each MIPS function becomes a C++ function that takes a `MIPS_EE_Context*` and `EE_Memory*` parameter. Instructions like `addiu $a0, $a0, 0x20` become `ctx->r[4] = ADD32(ctx->r[4], 0x20);`.

The generated code lives in `data/analysis/output/` (ignored by git). We copy it into the build tree.

#### Tasks

- [x] **3.1** Create `tools/build.py` — the master build script:
  - Step 1: Ensure extraction is done (call `tools/extract.py extract --all` if needed)
  - Step 2: Ensure analysis is done (call `tools/analyze.py` if needed)
  - Step 3: Run `ps2xRecomp` with the prepared TOML config:
    ```
    ps2xRecomp.exe data/analysis/rc1.toml
    ```
  - Step 4: Copy generated `.cpp` and `.h` files from `data/analysis/output/` into `src/recompiled/`
  - Step 5: Configure and build the native executable with CMake
  - Accept `--ps2recomp-dir PATH` to locate pre-built ps2xRecomp tools

- [x] **3.2** Update the root `CMakeLists.txt`:
  - Add `src/recompiled/` as a source directory
  - Create library `openratchet_recompiled` from all `.cpp` files in `src/recompiled/`
  - Link it against `openratchet_hal` (for memory access macros and context struct)
  - Add include path for generated headers

- [x] **3.3** Create the context header `include/openratchet/ee_context.h`:
  ```cpp
  #pragma once
  #include <cstdint>
  
  // 128-bit register for PS2 MMI and Vector Unit operations
  struct uint128_t {
      uint64_t lo;
      uint64_t hi;
  };
  
  // Emotion Engine guest CPU register context
  struct MIPS_EE_Context {
      uint64_t  r[32];      // General Purpose Registers ($r0 = 0 always)
      uint128_t mmi[32];    // 128-bit Multimedia Registers
      float     f[32];      // FPU Registers (COP1)
      uint32_t  pc;         // Program Counter
      uint32_t  hi, lo;     // HI/LO multiply/divide registers
      uint64_t  hi1, lo1;   // Pipeline 1 HI/LO (for MMI)
      uint32_t  sa;         // Shift Amount register
  };
  ```

- [x] **3.4** Create `include/openratchet/ee_memory.h` — the memory interface header that generated code will use. This must match the API that PS2Recomp's generated code expects:
  - `template<typename T> T MEM_READ(MIPS_EE_Context* ctx, uint32_t addr)`
  - `template<typename T> void MEM_WRITE(MIPS_EE_Context* ctx, uint32_t addr, T val)`
  - For now, these can be thin wrappers around the `EE_Memory` class

- [x] **3.5** Verify that the recompiled sources compile:
  ```powershell
  python tools/build.py --ps2recomp-dir third_party/PS2Recomp
  # Should produce build/openratchet.exe
  ```

- [x] **3.6** Create a `tools/smoke_test.py` that:
  - Launches the built executable
  - Waits N seconds (default 5)
  - Checks it doesn't crash immediately
  - Reports exit code and any stderr output

#### Acceptance Criteria

```powershell
python tools/build.py --ps2recomp-dir third_party/PS2Recomp
Test-Path build/openratchet.exe              # True
python tools/smoke_test.py --seconds 5       # Survives without crash
```

---

### Milestone 4: EE Memory System and MMIO Framework

**Goal**: Implement the full Emotion Engine memory map with 32 MB main RAM, 16 KB scratchpad, and the MMIO register interception framework. This is the foundation that all recompiled code reads/writes through.

**Commit name**: `milestone-4: EE memory system with MMIO interception framework`

#### Tasks

- [x] **4.1** Implement `src/hal/ee_memory.cpp` and `include/openratchet/ee_memory.h`:
  - Allocate 32 MB contiguous array for main RAM (`0x00000000` – `0x01FFFFFF`)
  - Allocate 16 KB scratchpad (`0x70000000` – `0x70003FFF`)
  - Implement address translation: strip KSEG0/KSEG1 bits with `address & 0x1FFFFFFF`
  - Template read/write functions for `uint8_t`, `uint16_t`, `uint32_t`, `uint64_t`, `uint128_t`
  - MMIO region detection: `0x10000000` – `0x1000FFFF` → route to MMIO handlers
  - GS region detection: `0x12000000` – `0x12001FFF` → route to GS handlers

- [x] **4.2** Implement the MMIO register dispatch table in `src/hal/mmio.cpp`:
  - Create an `MMIO_Handler` interface: `virtual void Write32(uint32_t addr, uint32_t val)`, `virtual uint32_t Read32(uint32_t addr)`
  - Register handlers by address range:
    - `0x10000000` – `0x100003FF`: Timer registers (EE Timer 0–3)
    - `0x10000800` – `0x10000BFF`: IPU (Image Processing Unit)
    - `0x10001000` – `0x10001FFF`: GIF registers
    - `0x10002000` – `0x100023FF`: Reserved
    - `0x10003000` – `0x100037FF`: VIF0 registers
    - `0x10003800` – `0x10003FFF`: VIF1 registers
    - `0x10004000` – `0x10007FFF`: VU0/VU1 code/data memory
    - `0x10008000` – `0x1000DFFF`: DMA channel registers
    - `0x1000E000` – `0x1000EFFF`: DMA control registers
    - `0x1000F000` – `0x1000F5FF`: INTC, SBUS, timer control
  - For now, handlers log accesses and return safe defaults (0 or ignored writes)

- [x] **4.3** Implement ELF loading in `src/hal/elf_loader.cpp`:
  - Parse the ELF header (same logic as the Python version)
  - Load each `PT_LOAD` segment into guest EE memory at the correct virtual address
  - Set the initial program counter from the ELF entry point
  - Initialize `$r0 = 0` (hardwired), `$sp` to top of RAM (`0x01FFFFF0`), `$gp` to a sensible default

- [x] **4.4** Write unit tests in `tests/test_memory.cpp`:
  - Test read/write to main RAM
  - Test scratchpad access
  - Test KSEG0/KSEG1 address translation
  - Test that MMIO writes are routed to handlers
  - Test ELF loading with a synthetic MIPS ELF

- [x] **4.5** Set up the PS2 floating-point mode:
  - Create `src/hal/float_mode.cpp` with `InitPS2FloatMode()` and `ClampPS2Float()`
  - Call `InitPS2FloatMode()` at the start of `main()` and at the entry of any new thread

#### Acceptance Criteria

```powershell
cmake --build build
.\build\openratchet.exe --self-test   # Memory tests pass
# ELF loads SCUS_971.99 into guest memory without error
# MMIO writes produce log output (not crashes)
```

---

### Milestone 5: PS2 Kernel and BIOS Syscall Implementation

**Goal**: Implement the PS2 kernel services that the recompiled game code calls. This includes thread management, semaphores, event flags, alarms, DMA control, and the BIOS syscall dispatch table. Without this, the recompiled code will crash on the first `syscall` instruction.

**Commit name**: `milestone-5: PS2 kernel and BIOS syscall implementations`

#### Background

PS2 games call kernel services through the `syscall` MIPS instruction, which traps to a handler indexed by the value in `$v1` (register 3). The PS2 BIOS provides ~120 syscalls. R&C1 uses a subset of these. PS2Recomp translates `syscall` instructions into function calls, so we need to provide C++ implementations for each used syscall.

#### Tasks

- [x] **5.1** Create `src/kernel/syscall_table.cpp`:
  - Define a dispatch table: `std::array<SyscallHandler, 256>` where `SyscallHandler = void(*)(MIPS_EE_Context*, EE_Memory*)`
  - Implement `DispatchSyscall(ctx, mem)`: read `ctx->r[3]` (which holds the syscall number), call the corresponding handler
  - Log unimplemented syscalls with the syscall number and guest PC for debugging

- [x] **5.2** Implement critical libc-like syscalls in `src/kernel/libc_syscalls.cpp`:
  - `FlushCache` (syscall 0x64): no-op on host (we don't have an instruction cache to flush)
  - `memcpy`, `memset`, `strlen`, `strcmp` guest-memory-aware wrappers
  - `printf` / `scePrintf`: redirect to host stdout (useful for game debug prints)

- [x] **5.3** Implement thread management in `src/kernel/threads.cpp`:
  - `CreateThread(entry, stack, priority, attr)` → create a guest thread descriptor
  - `StartThread(tid, args)` → mark thread as runnable
  - `ExitThread()`, `ExitDeleteThread()`, `TerminateThread()`, `DeleteThread()`
  - `SleepThread()`, `WakeupThread(tid)`, `iWakeupThread(tid)`
  - `RotateThreadReadyQueue(priority)`
  - `GetThreadId()` → return current thread ID
  - `ReferThreadStatus(tid)` → return thread state
  - **Implementation**: cooperative scheduling on the host. Maintain a list of guest thread descriptors. The "current thread" runs until it yields, sleeps, or a higher-priority thread is woken. Use host `std::thread` or fibers for actual context switching if needed, or start with single-threaded cooperative dispatch.

- [x] **5.4** Implement synchronization primitives in `src/kernel/sync.cpp`:
  - **Semaphores**: `CreateSema(attr)`, `SignalSema(id)`, `WaitSema(id)`, `PollSema(id)`, `DeleteSema(id)`
  - **Event Flags**: `CreateEventFlag(attr)`, `SetEventFlag(id, bits)`, `ClearEventFlag(id, bits)`, `WaitEventFlag(id, mode, bits)`, `PollEventFlag(id)`, `DeleteEventFlag(id)`
  - **Alarms**: `SetAlarm(time, callback, arg)`, `iSetAlarm(...)`, `ReleaseAlarm(id)`
  - These must be thread-safe if we use host threads for guest threads

- [x] **5.5** Implement DMA control syscalls in `src/kernel/dma.cpp`:
  - `DmaHandlerVIF0`, `DmaHandlerVIF1`, `DmaHandlerGIF`, `DmaHandlerSIF0`, `DmaHandlerSIF1`
  - `EnableDmac(channel)`, `DisableDmac(channel)`
  - `SetDma(channel, madr, qwc, chcr)` → initiates a DMA transfer
  - For now, DMA transfers are synchronous: copy data from guest memory to the appropriate subsystem handler immediately

- [x] **5.6** Implement timer and VBlank interrupt stubs in `src/kernel/timers.cpp`:
  - `SetVSyncCallback(mode, callback)` → register a callback called once per vblank
  - `SetTimer(id, compare, callback)` → register a timer callback
  - The main loop will call these callbacks at the appropriate rate

- [x] **5.7** Implement GS syscalls in `src/kernel/gs_syscalls.cpp`:
  - `GsPutIMR(imr)`: set GS interrupt mask
  - `SetGsCrt(interlace, mode, ffmd)`: set display mode (store parameters for renderer)
  - `GsSetDefDispEnv(...)`: set default display environment
  - `GsSetDefDrawEnv(...)`: set default draw environment

#### Acceptance Criteria

```powershell
cmake --build build
.\build\openratchet.exe data/raw/SCUS_971.99
# Should load ELF, begin executing recompiled code, and hit syscalls
# Unimplemented syscalls produce log messages with syscall number and PC
# No hard crashes on known R&C1 startup syscall sequence
```

---

### Milestone 6: IOP HLE — CDVD, SIF, PAD, and SPU2 Stubs

**Goal**: Implement High-Level Emulation of the IOP (Input/Output Processor) subsystems that R&C1 uses during startup and level loading. The original game sends RPC calls across the SIF bus to IOP modules. We intercept these at the EE side and handle them directly.

**Commit name**: `milestone-6: IOP HLE modules for CDVD, SIF, PAD, and SPU2`

#### Background

On real hardware, the IOP is a separate R3000A CPU running its own RTOS with loadable modules (IRX files). The EE communicates with the IOP via the SIF (Sub-CPU Interface) using RPCs. Rather than emulating the entire IOP CPU, we implement HLE versions of the IOP modules the game uses.

R&C1's critical IOP modules:
- **CDVD** (`sceCdRead`, `sceCdSeek`, `sceCdSync`): disc access
- **SIF** (`sceSifBindRpc`, `sceSifCallRpc`, `sceSifSetDma`): RPC transport
- **PAD** (`scePadInit`, `scePadRead`): controller input
- **SPU2** (`sceSdInit`, `sceSdSetParam`): sound

#### Tasks

- [x] **6.1** Create `src/iop/sif.cpp` — SIF RPC dispatch:
  - Implement `sceSifInitRpc()`: no-op (RPC system is always ready)
  - Implement `sceSifBindRpc(client, server_id, mode)`: register an RPC binding
  - Implement `sceSifCallRpc(client, func, mode, send, ssize, recv, rsize)`:
    - Look up the server module by `server_id`
    - Dispatch to the appropriate HLE handler
    - Copy result data into `recv` buffer in guest memory
  - Implement `sceSifSetDma(dma_desc, count)`: direct memory copy operations
  - Maintain a server registry: `std::unordered_map<uint32_t, IOP_Module*>`

- [x] **6.2** Create `src/iop/cdvd.cpp` — CDVD file I/O replacement:
  - RPC server ID for CDVD: look up from game's binding calls
  - Implement `sceCdRead(lsn, sectors, buffer, mode)`:
    - **Do NOT read from a disc.** Instead, read from `data/raw/` extracted files
    - Map the requested LSN to the correct file using the manifest or a sector→file lookup table
    - Read the data from host filesystem and copy into guest memory at `buffer`
    - Use asynchronous I/O (`std::async`) for non-blocking reads
  - Implement `sceCdSeek(lsn)`: no-op (no seek latency needed)
  - Implement `sceCdSync(mode)`: wait for pending async reads to complete
  - Implement `sceCdGetError()`: return 0 (no error)
  - Implement `sceCdInit(mode)`: initialize the file lookup table from `data/manifest.txt`

- [x] **6.3** Create `src/iop/pad.cpp` — Controller input:
  - Implement `scePadInit()`: initialize SDL2 GameController subsystem
  - Implement `scePadRead(port, slot, buffer)`:
    - Poll SDL2 controller state
    - Map SDL2 buttons/axes to PS2 pad format:
      - PS2 pad data is 32 bytes at the destination buffer
      - Byte 0: always 0x00, Byte 1: pad status (0x70 = pad present)
      - Bytes 2-3: digital buttons (16 bits, active low):
        - Bit 0: Select, Bit 1: L3, Bit 2: R3, Bit 3: Start
        - Bit 4: Up, Bit 5: Right, Bit 6: Down, Bit 7: Left
        - Bit 8: L2, Bit 9: R2, Bit 10: L1, Bit 11: R1
        - Bit 12: Triangle, Bit 13: Circle, Bit 14: Cross, Bit 15: Square
      - Bytes 4-5: right stick X/Y (0x80 = center)
      - Bytes 6-7: left stick X/Y (0x80 = center)
      - Bytes 8-19: pressure-sensitive button values (0–255)
    - Write the mapped data into guest memory at `buffer`
  - Implement `scePadGetState(port, slot)`: return pad state

- [x] **6.4** Create `src/iop/spu2.cpp` — SPU2 audio stubs:
  - For now, all SPU2 functions are no-ops that log their call
  - `sceSdInit(flag)`: log and return 0
  - `sceSdSetParam(entry, value)`: log and ignore
  - `sceSdVoiceTrans(channel, mode, addr, size, start)`: log and ignore
  - These will be implemented properly in a later milestone (audio)

- [x] **6.5** Create `src/iop/mc.cpp` — Memory card stubs:
  - `sceMcInit()`: log and return 0
  - `sceMcOpen()`, `sceMcRead()`, `sceMcWrite()`, `sceMcClose()`: log and return error/empty
  - These are deferred until save/load is needed

- [x] **6.6** Wire the IOP modules into the SIF dispatch:
  - In `sif.cpp`, register CDVD, PAD, SPU2, and MC modules with their server IDs
  - Any unknown server ID logs a warning with the ID

#### Acceptance Criteria

```powershell
cmake --build build
.\build\openratchet.exe data/raw/SCUS_971.99
# Game code should now get past initial IOP setup
# CDVD reads should load data from data/raw/ without error
# Pad polling should not crash (even if no controller connected)
# SPU2 calls should log but not crash
```

---

### Milestone 7: SDL2 Window, Input Loop, and Main Frame Loop

**Goal**: Create the native window, implement the frame-decoupled main loop, wire input polling, and get the game's startup sequence executing in real-time.

**Commit name**: `milestone-7: SDL2 window, main loop, and input integration`

#### Tasks

- [x] **7.1** Implement `src/hal/native_hal.cpp`:
  - Initialize SDL2 with `SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO`
  - Create a window: `SDL_CreateWindow("OpenRatchet — Ratchet & Clank", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE)`
  - Handle `SDL_QUIT` event to exit
  - Handle `SDL_WINDOWEVENT_RESIZED` for dynamic resolution

- [x] **7.2** Implement the main loop in `src/main.cpp`:
  ```
  1. Initialize HAL (SDL2 window)
  2. Initialize EE memory
  3. Load SCUS_971.99 ELF into guest memory
  4. Initialize PS2 float mode (MXCSR)
  5. Initialize kernel syscall table
  6. Initialize IOP modules (CDVD, SIF, PAD, SPU2)
  7. Set initial register state (PC = entry point, SP, GP, r0=0)
  8. MAIN LOOP:
     a. Calculate delta time using std::chrono::high_resolution_clock
     b. If elapsed >= 1/60s (fixed 60 Hz engine tick):
        - Poll SDL2 events (input, window, quit)
        - Update pad data in guest memory
        - Step recompiled game routines (dispatch to entry point)
        - Fire VBlank callbacks
        - Fire timer callbacks
     c. Present frame (Vulkan submit — or just SDL_Delay for now)
     d. If not running, break
  9. Shutdown HAL
  ```

- [x] **7.3** Build the recompiled function dispatch table (`g_ps2RecompiledFunctionTable`)
- [x] Integrate `tools/smoke_test.py` to verify the dispatch loop executes the entry point without a segfault (outputs `MISSING-TARGET: 0x...` and halts)

- [ ] **7.4** Handle the R&C1 startup sequence:
  - The game's entry point initializes the C runtime, then calls the game's main function
  - Log each function dispatch for the first 1000 calls to understand the call sequence
  - Identify and fix any missing function targets

- [ ] **7.5** Write a smoke test in `tools/smoke_test.py`:
  - Launch the executable, wait N seconds, kill it
  - Parse stderr for `MISSING-TARGET`, `FATAL`, `UNIMPLEMENTED`, `CRASH` keywords
  - Report pass/fail

#### Acceptance Criteria

```powershell
cmake --build build
.\build\openratchet.exe data/raw/SCUS_971.99
# Window opens, title shows "OpenRatchet — Ratchet & Clank"
# Game code begins executing (function dispatch log shows calls)
# Window stays open for 60+ seconds without crash

python tools/smoke_test.py --seconds 60
# PASS (no fatal errors)
```

---

### Milestone 8: GIF/GS Packet Decoding and Vulkan Renderer Foundation

**Goal**: Implement the GS (Graphics Synthesizer) register state machine, GIF packet parser, and a minimal Vulkan renderer that can interpret PS2 draw commands and present a frame. This is where the game first becomes *visible*.

**Commit name**: `milestone-8: GIF/GS packet decoding and Vulkan renderer foundation`

#### Background

The PS2 rendering pipeline works like this:
1. The EE CPU (or DMA) sends GIF packets to the GS
2. GIF packets contain register writes (A+D mode) or packed/reglist primitives
3. The GS maintains internal state (frame buffer, z-buffer, texture, alpha, scissor, etc.)
4. Drawing occurs when the GS receives primitive data with the current state

#### Tasks

- [ ] **8.1** Implement `src/renderer/gs_state.cpp` — GS register state machine:
  - Maintain all GS registers as a struct:
    ```cpp
    struct GS_State {
        // Frame buffer
        uint32_t FRAME_1;   // base pointer, width, PSM, FBMSK
        uint32_t ZBUF_1;    // z-buffer base, PSM, ZMSK
        uint32_t SCISSOR_1; // scissor rectangle
        uint32_t XYOFFSET_1;// drawing offset
        // Texture
        uint32_t TEX0_1;    // texture base, width, PSM, TW, TH, TCC, TFX, etc.
        uint32_t TEX1_1;    // LOD params
        uint32_t CLAMP_1;   // clamp/repeat/region modes
        // Alpha
        uint32_t ALPHA_1;   // alpha blending equation
        uint32_t TEST_1;    // alpha/z/destination test
        // Primitive
        uint32_t PRIM;      // primitive type, shading, texture, fog, alpha, etc.
        uint32_t PRMODECONT;
        // Colors
        uint32_t RGBAQ;     // current vertex color
        uint32_t FOG;
        // ... (full GS register set)
    };
    ```
  - Implement `WriteGSReg(uint8_t reg, uint64_t data)` — decode and store
  - Handle all GS register addresses (0x00–0x7F)

- [ ] **8.2** Implement `src/renderer/gif_parser.cpp` — GIF packet decoder:
  - Parse GIF tags (64-bit):
    - Bits 0–14: NLOOP (number of loops)
    - Bit 15: EOP (end of packet)
    - Bits 24–25: PRE (prim field enable)
    - Bits 26–36: PRIM (primitive setting if PRE)
    - Bits 46–47: FLG (format): 0=PACKED, 1=REGLIST, 2=IMAGE, 3=DISABLE
    - Bits 48–51: NREG (number of registers per loop)
    - Bits 52–63: REG (register descriptors, 4 bits each)
  - For PACKED format: decode register data pairs per descriptor
  - For REGLIST format: decode sequential register writes
  - For IMAGE format: copy raw pixel data to GS VRAM
  - Route decoded register writes to `WriteGSReg()`

- [ ] **8.3** Implement `src/renderer/vulkan_init.cpp` — Vulkan bootstrapping:
  - Create Vulkan instance with validation layers (debug mode)
  - Select physical device (discrete GPU preferred)
  - Create logical device with graphics + compute queue families
  - Create SDL2 Vulkan surface via `SDL_Vulkan_CreateSurface()`
  - Create swapchain (VK_PRESENT_MODE_FIFO_KHR for vsync, or MAILBOX for uncapped)
  - Create render pass, framebuffers, command pool, command buffers
  - Create descriptor set layout and pipeline layout
  - Use VulkanMemoryAllocator (VMA) for memory management

- [ ] **8.4** Implement `src/renderer/vulkan_draw.cpp` — primitive rendering:
  - Create vertex/fragment shader pair for PS2 primitive rendering:
    - Vertex shader: takes PS2 vertex data (position, color, UV, fog) and transforms to clip space
    - Fragment shader: samples texture (if enabled), applies alpha test, fog, and outputs color
  - Create a Vulkan graphics pipeline for triangle rendering
  - Implement `DrawPrimitive(type, vertices)`:
    - Type 0: Point, 1: Line, 2: Line Strip, 3: Triangle, 4: Triangle Strip, 5: Triangle Fan, 6: Sprite
    - Upload vertex data to a GPU staging buffer
    - Record draw commands into the current command buffer

- [ ] **8.5** Implement PS2 VRAM emulation in `src/renderer/gs_vram.cpp`:
  - Allocate a 4 MB buffer representing GS VRAM (2048x2048 pixels, various pixel formats)
  - Implement pixel format conversions:
    - PSMCT32: 32-bit RGBA
    - PSMCT24: 24-bit RGB (packed)
    - PSMCT16/PSMCT16S: 16-bit color (ABGR 1555)
    - PSMT8/PSMT4: 8-bit/4-bit indexed (CLUT)
  - Implement block/page layout (PS2 VRAM uses a swizzled memory layout, not linear)
  - Implement `UploadTexture(base_pointer, width, psm)` → create/update a Vulkan texture

- [ ] **8.6** Wire GIF packets to the Vulkan renderer:
  - When the GIF parser finishes a NLOOP with valid primitive data:
    - Build host vertex array from decoded GS vertex registers (XYZ, RGBAQ, ST/UV)
    - Look up or upload the current texture (TEX0 register)
    - Set blend state from ALPHA register
    - Set depth test from TEST register
    - Set scissor from SCISSOR register
    - Submit the draw call
  - At VBlank: present the swapchain image

- [ ] **8.7** Create debug overlay:
  - Show: FPS, frame time, number of GIF packets processed, number of draw calls, number of textures uploaded

#### Acceptance Criteria

```powershell
cmake --build build
.\build\openratchet.exe data/raw/SCUS_971.99
# Window shows SOMETHING — even if it's garbled/partial, we want to see pixels
# GIF packet log shows parsed packets with register writes
# Vulkan validation layer reports no errors (or only expected ones)
# FPS counter is visible in debug overlay
```

---

### Milestone 9: DMA Chain Walker and VIF/VU1 Geometry Pipeline

**Goal**: Implement the DMA controller, VIF packet unpacking, and translate VU1 microcode geometry programs into Vulkan compute shaders. This is what actually processes the game's 3D geometry (tfrag terrain, tie instances, shrubs, mobys).

**Commit name**: `milestone-9: DMA chains, VIF unpacking, and VU1 compute shader pipeline`

#### Background

The rendering pipeline is: EE builds DMA chains → DMA controller sends to VIF1 → VIF1 unpacks data into VU1 data memory → VU1 runs microcode to transform geometry → VU1 sends to GIF → GIF writes GS registers and primitives.

We intercept at the DMA level and process the entire chain on the host.

#### Tasks

- [ ] **9.1** Implement `src/hal/dma_controller.cpp`:
  - DMA channels: 0 (VIF0), 1 (VIF1), 2 (GIF), 3 (IPU FROM), 4 (IPU TO), 5 (SIF0), 6 (SIF1), 7 (SIF2), 8 (SPR FROM), 9 (SPR TO)
  - Each channel has registers: `CHCR` (control), `MADR` (memory address), `QWC` (quadword count), `TADR` (tag address)
  - Implement chain mode DMA:
    - Read DMA tag (128 bits) from `TADR` in guest memory
    - Tag format: `[QWC:16][pad:10][PCE:2][ID:3][IRQ:1][ADDR:31][SPR:1]`
    - Tag IDs: `refe`(0), `cnt`(1), `next`(2), `ref`(3), `refs`(4), `call`(5), `ret`(6), `end`(7)
    - Walk the chain, sending data to the destination channel handler
  - Trigger DMA when software writes to `CHCR` with STR bit set

- [ ] **9.2** Implement `src/renderer/vif_decoder.cpp` — VIF packet decoder:
  - VIF commands (upper 8 bits of the 32-bit command word):
    - `NOP` (0x00): skip
    - `STCYCL` (0x01): set cycle (CL, WL)
    - `OFFSET` (0x02): set offset
    - `BASE` (0x03): set base
    - `ITOP` (0x04): set ITOP
    - `STMOD` (0x05): set mode (add/normal)
    - `MSKPATH3` (0x06): mask GIF PATH 3
    - `MARK` (0x07): set mark
    - `FLUSHE` (0x10): wait for VU1 end
    - `FLUSH` (0x11): wait for VU1 end + PATH1/2
    - `FLUSHA` (0x13): wait for all
    - `MSCAL` (0x14): activate VU1 microcode at address
    - `MSCALF` (0x15): as above, with wait
    - `MSCNT` (0x17): continue VU1
    - `STMASK` (0x20): set mask register
    - `STROW` (0x30): set row registers
    - `STCOL` (0x31): set column registers
    - `MPG` (0x4A): load VU1 microcode
    - `DIRECT` (0x50): send data directly to GIF
    - `DIRECTHL` (0x51): as above, with stall
    - `UNPACK` (0x60–0x7F): unpack data into VU1 data memory
  - Unpack formats: V2-16, V2-32, V3-16, V3-32, V4-8, V4-16, V4-32, V4-5 (various vector sizes and bit depths)
  - Route `DIRECT`/`DIRECTHL` data to the GIF parser
  - Route `UNPACK` data to VU1 data memory
  - Route `MSCAL`/`MSCNT` to VU1 microcode execution

- [ ] **9.3** Implement `src/renderer/vu1_programs.cpp` — VU1 microcode translation framework:
  - Extract VU1 microcode programs from `MPG` VIF commands or from level WAD data
  - Create a microcode→compute shader translation table:
    - Hash the microcode binary to identify known programs
    - Map known hashes to pre-written Vulkan compute shaders
  - For unknown programs: log the microcode address and size for manual translation later
  - Create the shader dispatch function: `ExecuteVU1Program(program_hash, vu1_data_mem, output_buffer)`

- [ ] **9.4** Create initial Vulkan compute shaders in `shaders/`:
  - `shaders/vu1_tfrag.comp` — terrain fragment transform:
    - Input: vertex positions (model space), transform matrices, texture coordinates
    - Output: transformed vertices in clip space
    - This is the most common VU1 program in R&C1
  - `shaders/vu1_moby.comp` — moby (actor) transform:
    - Input: skinned vertex positions, bone matrices, normals
    - Output: transformed and lit vertices
  - `shaders/vu1_tie.comp` — instanced geometry transform
  - `shaders/vu1_shrub.comp` — vegetation transform with alpha

- [ ] **9.5** Wire the complete rendering pipeline:
  ```
  DMA Chain → VIF Decoder → VU1 Compute → GIF Parser → GS State → Vulkan Draw
  ```
  - Test with the game's first rendered frame

#### Acceptance Criteria

```powershell
cmake --build build
.\build\openratchet.exe data/raw/SCUS_971.99
# DMA chains are walked without errors
# VIF packets are decoded and routed
# At least some geometry is visible in the window
# Game reaches a recognizable visual state (even if incomplete)
```

---

### Milestone 10: First Rendered Frame — Ground Truth Verification

**Goal**: Get the native window to display a recognizable Ratchet & Clank frame. Implement the verification pipeline to compare our rendering output against PCSX2 reference captures.

**Commit name**: `milestone-10: first rendered frame with ground-truth verification`

#### Tasks

- [ ] **10.1** Capture a reference frame from PCSX2:
  - Run R&C1 in PCSX2 debug build
  - Capture: a screenshot of the first in-game frame (after loading)
  - Capture: a GS register dump at that frame
  - Capture: the EE register state at the frame boundary
  - Save these as `tests/reference/frame_001.png`, `tests/reference/frame_001_gs.json`, `tests/reference/frame_001_ee.json`

- [ ] **10.2** Implement register state comparison in `tests/verify_state.cpp`:
  - Load the reference EE register dump
  - At matching program counter addresses, compare:
    - All 32 GPRs ($r0–$r31)
    - All 32 FPRs ($f0–$f31)
    - PC, HI, LO
  - Report any divergences with the exact register, expected vs. actual values

- [ ] **10.3** Implement frame comparison:
  - Capture the current Vulkan framebuffer to a PNG
  - Compare against the reference frame using pixel-difference metrics
  - Report percentage of matching pixels

- [ ] **10.4** Debug and fix rendering issues:
  - Fix GS register interpretations that produce wrong colors/positions
  - Fix texture upload/format issues
  - Fix alpha blending equation mapping
  - Fix scissor/viewport calculations
  - Fix depth test configuration

- [ ] **10.5** Document the verification methodology in `docs/verification.md`

#### Acceptance Criteria

```powershell
.\build\openratchet.exe data/raw/SCUS_971.99 --capture-frame 1
# Produces build/capture_frame_001.png
# Frame is recognizably Ratchet & Clank (title screen, loading, or in-game)
# Register comparison reports <5% divergence on critical path
```

---

### Milestone 11: First Playable Vertical Slice

**Goal**: Make one complete level playable: Ratchet can walk, jump, attack, and interact with the environment. One enemy spawns, takes damage, and dies. One weapon fires. The camera works. The HUD shows health and ammo.

**Commit name**: `milestone-11: first playable vertical slice — one complete level`

#### Tasks

- [ ] **11.1** Load one complete level WAD through the translated game code path
- [ ] **11.2** Render: Ratchet model, terrain (tfrag), static geometry (tie), vegetation (shrub)
- [ ] **11.3** Implement camera system (follows Ratchet, translated from original code)
- [ ] **11.4** Implement controller input → character movement (walk, run, strafe)
- [ ] **11.5** Implement jump physics (uses original translated physics code)
- [ ] **11.6** Implement wrench attack (melee collision, damage application)
- [ ] **11.7** Spawn one enemy type (moby), AI runs through translated code
- [ ] **11.8** Enemy takes damage and dies (health system, death animation)
- [ ] **11.9** Implement one ranged weapon (Blaster) firing through translated logic
- [ ] **11.10** Render the HUD: health bar, bolt counter, ammo display
- [ ] **11.11** Implement pause menu (start button opens/closes)
- [ ] **11.12** Implement death → respawn at last checkpoint

#### Acceptance Criteria

```
- Player can start the level with a controller
- Walk/run/jump in 3D space
- Attack enemies with wrench
- Fire one weapon
- Enemy dies and drops bolts
- HUD shows health and ammo
- Death respawns at checkpoint
- Session lasts 5+ minutes without crash
```

---

### Milestone 12: Audio System — SPU2 ADPCM and Streaming

**Goal**: Implement sound effects, music, and voice playback by decoding PS2 SPU2 ADPCM audio and streaming it through SDL2's audio subsystem.

**Commit name**: `milestone-12: SPU2 audio system with ADPCM decoding`

#### Tasks

- [ ] **12.1** Implement SPU2 ADPCM decoder in `src/iop/spu2.cpp`:
  - PS2 audio is 4-bit ADPCM (Sony's proprietary codec)
  - Decode to 16-bit PCM
  - Support 24 voices per core (2 cores = 48 total voices)

- [ ] **12.2** Implement SDL2 audio output:
  - Open audio device: 48000 Hz, 16-bit signed, stereo, 1024-sample buffer
  - Mix active SPU2 voices into the SDL2 audio callback

- [ ] **12.3** Implement sound effect playback (triggered by translated game code)
- [ ] **12.4** Implement background music streaming
- [ ] **12.5** Implement voice/dialogue playback

#### Acceptance Criteria

```
- Sound effects play when Ratchet attacks, jumps, collects bolts
- Background music plays during gameplay
- Audio does not crackle or stutter at 60 FPS
```

---

### Milestone 13: Level Streaming and Transitions

**Goal**: Implement seamless level streaming (R&C1's signature feature) and level transitions. Replace the IOP disc-read pipeline with async host file I/O.

**Commit name**: `milestone-13: async level streaming and level transitions`

#### Background

R&C1 streams level sectors in the background while the player traverses the environment. On PS2, this uses IOP `sceCdRead` RPCs to read disc sectors into ring buffers in EE RAM. We replace this with multithreaded async file reads from `data/raw/levels/`.

#### Tasks

- [ ] **13.1** Implement the async streaming engine in `src/iop/cdvd.cpp`:
  - Use `std::async` / thread pool for non-blocking reads
  - Pre-fetch level sectors based on player position (translated game logic handles this)
  - Feed data into guest memory ring buffers at the addresses the game expects

- [ ] **13.2** Implement level transitions:
  - Game code triggers level load through translated logic
  - Flush current level data
  - Load new level WAD
  - Resume gameplay in new level

- [ ] **13.3** Implement loading screen rendering (translated from original)
- [ ] **13.4** Test at least 3 level transitions without crashes

#### Acceptance Criteria

```
- Moving to a new area triggers background asset loading
- Level transitions work (ship cutscene → new planet)
- No frame drops during streaming
- At least 3 transitions tested successfully
```

---

### Milestone 14: Core Game Systems — NPCs, Shops, Collectibles, Cutscenes

**Goal**: Enable all remaining game systems needed for campaign progression: NPCs, missions, shops, collectibles, gadgets, scripted events, and cutscenes.

**Commit name**: `milestone-14: core game systems — NPCs, shops, collectibles, and cutscenes`

#### Tasks

- [ ] **14.1** Ensure NPC interaction (talk, receive items/missions) works through translated code
- [ ] **14.2** Ensure shop system works (buy weapons/ammo with bolts)
- [ ] **14.3** Ensure collectible tracking works (gold bolts, skill points)
- [ ] **14.4** Ensure gadget usage works (swingshot, heli-pack, hydro-pack, etc.)
- [ ] **14.5** Ensure scripted events and triggers fire correctly
- [ ] **14.6** Implement cutscene playback (pre-rendered or in-engine cinematics)
- [ ] **14.7** Ensure all weapon types work (Blaster, Bomb Glove, Devastator, RYNO, etc.)
- [ ] **14.8** Ensure boss fights work (at least Drek final boss)
- [ ] **14.9** Test opening sequence through first three planets

#### Acceptance Criteria

```
- Can complete the opening sequence (Veldin → Novalis → Aridia or equivalent)
- Shops work, weapons can be purchased
- Cutscenes play
- Gadgets function
- Boss fights complete without crashes
```

---

### Milestone 15: Save System — Memory Card Emulation

**Goal**: Implement save/load functionality by emulating the PS2 memory card through native file I/O. Saves go to a per-user directory on the host.

**Commit name**: `milestone-15: save system with native memory card emulation`

#### Tasks

- [ ] **15.1** Implement `src/iop/mc.cpp` fully:
  - Map `mc0:` path to host directory: `{user_data}/OpenRatchet/saves/`
  - Implement `sceMcOpen(port, slot, name, mode)`: open host file
  - Implement `sceMcRead(fd, buf, size)`: read into guest memory
  - Implement `sceMcWrite(fd, buf, size)`: write from guest memory
  - Implement `sceMcClose(fd)`: close host file
  - Implement `sceMcMkDir(port, slot, name)`: create host directory
  - Implement `sceMcGetDir(port, slot, name, ...)`: list directory entries
  - Implement `sceMcDelete(port, slot, name)`: delete save file

- [ ] **15.2** Handle save data format:
  - R&C1 saves include: progress flags, bolt count, weapon inventory, planet unlock status
  - The translated game code handles all serialization; we just provide the file I/O

- [ ] **15.3** Test save/load cycle:
  - Play until a checkpoint/save point
  - Save the game
  - Exit and restart
  - Load the save
  - Verify progress is preserved

- [ ] **15.4** Test edge cases:
  - Missing save directory (auto-create)
  - Corrupted save file (game should handle gracefully)
  - Multiple save slots

#### Acceptance Criteria

```
- Save game works at save points
- Load game restores exact progress
- Saves survive process restart
- Save directory is auto-created on first run
```

---

### Milestone 16: Full Campaign Playthrough and Compatibility

**Goal**: Test every level, boss, weapon, gadget, menu, shop, challenge, and cutscene in a full campaign playthrough. Track and fix all remaining incompatibilities.

**Commit name**: `milestone-16: full campaign verification and compatibility fixes`

#### Tasks

- [ ] **16.1** Create a test checklist for all 18 planets/locations
- [ ] **16.2** Play through the entire campaign, noting issues
- [ ] **16.3** Fix all game-breaking bugs
- [ ] **16.4** Fix visual glitches (texture issues, Z-fighting, missing geometry)
- [ ] **16.5** Fix audio issues (missing sounds, wrong music)
- [ ] **16.6** Fix timing issues (animations too fast/slow)
- [ ] **16.7** Verify all weapons across all levels
- [ ] **16.8** Verify all gadgets in their required sections
- [ ] **16.9** Verify all bosses can be defeated
- [ ] **16.10** Verify gold bolts and skill points can be collected
- [ ] **16.11** Verify challenge mode (New Game+) works

#### Acceptance Criteria

```
- Full campaign playthrough from start to credits
- All planets accessible
- All weapons functional
- All bosses defeatable
- Save/load works throughout
- No game-breaking bugs remaining
```

---

### Milestone 17: Release Quality — Polish, Settings, and Distribution

**Goal**: Prepare the project for public release. Add user-facing settings, improve first-run experience, optimize performance, and document everything.

**Commit name**: `milestone-17: release quality — settings, optimization, and documentation`

#### Tasks

- [ ] **17.1** Implement first-run setup:
  - On first launch, prompt user for ISO path (SDL2 file dialog or CLI)
  - Auto-extract game data
  - Store ISO path in a config file

- [ ] **17.2** Implement settings menu:
  - Resolution selection (720p, 1080p, 1440p, 4K, custom)
  - Display mode (windowed, borderless, fullscreen)
  - V-Sync on/off
  - Frame rate target (60, 120, 144, uncapped)
  - Audio volume (master, SFX, music, voice)
  - Controller dead zone and sensitivity
  - Internal resolution multiplier

- [ ] **17.3** Performance optimization:
  - Profile hot paths with instrumentation
  - Optimize VU1 compute shader dispatch
  - Optimize texture upload pipeline
  - Optimize DMA chain walking
  - Target: stable 60 FPS at 1080p on mid-range hardware

- [ ] **17.4** Implement crash reporting:
  - On crash: dump guest PC, register state, last 100 function calls, GS state
  - Write to `crash_report.txt`

- [ ] **17.5** Write final documentation:
  - `docs/building.md`: complete build instructions for Windows and Linux
  - `docs/running.md`: how to set up and run
  - `docs/troubleshooting.md`: common issues and solutions
  - `docs/architecture.md`: technical architecture deep-dive

- [ ] **17.6** Ensure no copyrighted game data is in the repository
- [ ] **17.7** One-command build and run from clean checkout:
  ```powershell
  git clone --recursive https://github.com/User/OpenRatchet.git
  cd OpenRatchet
  python tools/build.py --iso path/to/ratchet.iso
  .\build\openratchet.exe
  ```

#### Acceptance Criteria

```
- New user can clone, build, and play with their own ISO
- No manual steps beyond providing the ISO
- Settings menu works
- Stable 60 FPS on target hardware
- No copyrighted data in repository
- All documentation complete
```

---

## Quick Reference: Build Commands

```powershell
# Step 1: Extract game data from ISO
python tools/extract.py extract --iso games/Ratchet.iso --all

# Step 2: Analyze ELF and generate function map
python tools/analyze.py --elf data/raw/SCUS_971.99

# Step 3: Recompile MIPS to C++20 and build native executable
python tools/build.py --ps2recomp-dir third_party/PS2Recomp

# Step 4: Run
.\build\openratchet.exe data/raw/SCUS_971.99

# Smoke test
python tools/smoke_test.py --seconds 60

# Self-test (no ISO needed)
python tools/extract.py self-test
```

---

## Architecture Decisions Log

| Decision | Rationale |
|---|---|
| Static recompilation over decompilation | 3–6 months vs 3–5 years; same logic fidelity |
| Static recompilation over HLE | No runtime JIT overhead; native compiler optimization |
| PS2Recomp over custom recompiler | Proven toolchain, active development, community support |
| Vulkan over OpenGL | Compute shader support for VU1 translation; modern API |
| SDL2 over GLFW | Built-in controller support, audio, Vulkan surface creation |
| Extracted files over virtual ISO | Simpler, faster, debuggable; disk space is cheap |
| Cooperative guest threading | Simpler than preemptive; R&C1 is mostly single-threaded |
| MXCSR for float compat | Hardware-level FTZ/DAZ/truncation; no per-instruction overhead |
| VU1 → compute shaders | GPU-parallel geometry processing; eliminates CPU bottleneck |
| Async file I/O for streaming | Eliminates disc seek latency; uses modern OS capabilities |

---

## Glossary

| Term | Definition |
|---|---|
| **EE** | Emotion Engine — the PS2's main CPU (MIPS R5900, 128-bit) |
| **IOP** | Input/Output Processor — secondary R3000A CPU handling I/O |
| **GS** | Graphics Synthesizer — the PS2's GPU/rasterizer |
| **GIF** | GS Interface — packet-based data path from EE to GS |
| **VIF** | VPU Interface — packet-based data path from EE to VU0/VU1 |
| **VU0/VU1** | Vector Units — SIMD coprocessors for geometry/physics |
| **DMA** | Direct Memory Access — hardware data transfer controller |
| **SIF** | Sub-CPU Interface — RPC bridge between EE and IOP |
| **CDVD** | CD/DVD drive subsystem on the IOP |
| **SPU2** | Sound Processing Unit 2 — audio DSP on the IOP |
| **MMI** | Multimedia Instructions — 128-bit SIMD extensions to MIPS |
| **MMIO** | Memory-Mapped I/O — hardware registers accessed as memory |
| **HAL** | Hardware Abstraction Layer — our host implementations |
| **HLE** | High-Level Emulation — implementing hardware behavior at API level |
| **PSM** | Pixel Storage Mode — GS texture/framebuffer pixel format |
| **CLUT** | Color Look-Up Table — palette for indexed textures |
| **tfrag** | Terrain fragment — R&C's terrain mesh type |
| **tie** | Tied instance — R&C's instanced static geometry type |
| **shrub** | Shrub instance — R&C's vegetation/detail mesh type |
| **moby** | Mobile object — R&C's interactive actor/entity type |
| **WAD** | Where's All the Data — R&C's level archive format |
| **FTZ** | Flush-to-Zero — denormals become zero |
| **DAZ** | Denormals-Are-Zero — treat denormal inputs as zero |
