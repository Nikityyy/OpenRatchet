import os
import re
import glob

original_patches = """// runtime_patches.cpp -- Register function-table entries that ps2xRecomp missed.
//
// When the game reports "MISSING-TARGET: target=0x..." at runtime, add a new
// patch block below (copy the pattern), rebuild, and re-run to find the next
// missing target.  Also add the address to tools/analyze.py manual exceptions
// so the next full recompile will generate the real function body.
#include "ps2_runtime.h"
#include "ps2_stubs.h"
#include <cstdint>

// Symbols from register_functions.cpp
extern PS2Runtime::RecompiledFunction g_ps2RecompiledFunctionTable[];
extern const uint32_t g_ps2RecompiledFunctionTableBase;
extern const uint32_t g_ps2RecompiledFunctionTableEnd;
extern const uint32_t g_ps2RecompiledFunctionTableSlotCount;

// Helper: safe write into dispatch table
static void patch_register(uint32_t addr, PS2Runtime::RecompiledFunction fn) {
    if (addr < g_ps2RecompiledFunctionTableBase || addr >= g_ps2RecompiledFunctionTableEnd)
        return;
    const uint32_t slot = (addr - g_ps2RecompiledFunctionTableBase) / 4u;
    g_ps2RecompiledFunctionTable[slot] = fn;
}

static void sub_0011DB38_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_001192A0_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_123008_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_120EB0_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_11CF48_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_11CF10_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_11AB20_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_11C840_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_121388_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_11BC48_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_121450_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }
static void sub_122298_stub(uint8_t*, R5900Context* ctx, PS2Runtime*) { (void)ctx; }

"""

def main():
    src_dir = os.path.join("src", "recompiled")
    
    # 1. Parse register_functions.cpp to find already registered addresses
    registered = set()
    register_file = os.path.join(src_dir, "register_functions.cpp")
    with open(register_file, "r") as f:
        content = f.read()
        for match in re.finditer(r'sub_[0-9A-Fa-f]+_0x([0-9A-Fa-f]+)', content):
            addr = int(match.group(1), 16)
            registered.add(addr)

    # 2. Find all sub_*.cpp files
    all_files = glob.glob(os.path.join(src_dir, "sub_*.cpp"))
    missing = []
    
    for path in all_files:
        name = os.path.basename(path).replace(".cpp", "")
        m = re.match(r'sub_[0-9A-Fa-f]+_0x([0-9A-Fa-f]+)', name)
        if m:
            addr = int(m.group(1), 16)
            if addr not in registered:
                missing.append((addr, name))
                
    # 3. Append to original patches
    out_lines = [original_patches]

    # Generate externs
    for addr, name in missing:
        out_lines.append(f"extern void {name}(uint8_t*, R5900Context*, PS2Runtime*);")

    out_lines.append("")
    out_lines.append("void InitRuntimePatches() {")
    out_lines.append("    patch_register(0x0011DB38u, sub_0011DB38_stub);")
    out_lines.append("    patch_register(0x001192A0u, sub_001192A0_stub);")
    out_lines.append("    patch_register(0x123008u, sub_123008_stub);")
    out_lines.append("    patch_register(0x120EB0u, sub_120EB0_stub);")
    out_lines.append("    patch_register(0x11CF48u, sub_11CF48_stub);")
    out_lines.append("    patch_register(0x11CF10u, sub_11CF10_stub);")
    out_lines.append("    patch_register(0x11AB20u, sub_11AB20_stub);")
    out_lines.append("    patch_register(0x11C840u, sub_11C840_stub);")
    out_lines.append("    patch_register(0x121388u, sub_121388_stub);")
    out_lines.append("    patch_register(0x11BC48u, sub_11BC48_stub);")
    out_lines.append("    patch_register(0x121450u, sub_121450_stub);")
    out_lines.append("    patch_register(0x122298u, sub_122298_stub);")
    
    for addr, name in missing:
        out_lines.append(f"    patch_register(0x{addr:X}u, {name});")
    out_lines.append("}")

    patches_file = os.path.join(src_dir, "runtime_patches.cpp")
    with open(patches_file, "w") as f:
        f.write("\n".join(out_lines) + "\n")
        
if __name__ == "__main__":
    main()
