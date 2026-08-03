#include <iostream>
#include <string>
#include <cassert>
#include <chrono>
#include "openratchet/ee_memory.h"
#include "openratchet/float_mode.h"
#include "openratchet/elf_loader.h"
#include "openratchet/iop.h"
#include "openratchet/syscalls.h"

#include "openratchet/native_hal.h"
#include "openratchet/vulkan_renderer.h"
#include <unordered_map>
#include "ps2_runtime.h"

#include "openratchet/context_bridge.h"

std::unordered_map<uint32_t, PS2Runtime::RecompiledFunction> g_dispatch_table;

namespace OpenRatchet {
namespace Debug {
    void RenderOverlay(VulkanRenderer& renderer, uint32_t call_count);
}
}

void BuildDispatchTable() {
    for (uint32_t i = 0; i < g_ps2RecompiledFunctionTableSlotCount; i++) {
        if (g_ps2RecompiledFunctionTable[i] != nullptr) {
            uint32_t pc = g_ps2RecompiledFunctionTableBase + (i * 4);
            g_dispatch_table[pc] = g_ps2RecompiledFunctionTable[i];
        }
    }
}

void run_self_test() {
    EE_Memory mem;
    mem.Init();

    // Test main RAM
    mem.Write<uint32_t>(0x00100000, 0xDEADBEEF);
    assert(mem.Read<uint32_t>(0x00100000) == 0xDEADBEEF);

    // Test scratchpad
    mem.Write<uint32_t>(0x70000000, 0xCAFEBABE);
    assert(mem.Read<uint32_t>(0x70000000) == 0xCAFEBABE);

    // Test KSEG0 translation
    mem.Write<uint32_t>(0x80100000, 0x12345678);
    assert(mem.Read<uint32_t>(0x00100000) == 0x12345678);

    // Test MMIO routing
    mem.Write<uint32_t>(0x10000000, 0x11112222);
    assert(mem.Read<uint32_t>(0x10000000) == 0x11112222);

    std::cout << "Memory tests pass!" << std::endl;
}

int main(int argc, char* argv[]) {
    std::cout << "OpenRatchet — Ratchet & Clank Native PC Port" << std::endl;
    InitPS2FloatMode();

    if (argc > 1) {
        std::string arg = argv[1];
        if (arg == "--self-test") {
            run_self_test();
            return 0;
        }

        if (!OpenRatchet::HAL::Init()) {
            std::cerr << "Failed to init HAL" << std::endl;
            return 1;
        }

        VulkanRenderer renderer;
        if (!renderer.Initialize(OpenRatchet::HAL::GetWindow())) {
            std::cerr << "Failed to initialize Vulkan renderer" << std::endl;
            // Continuing anyway since it's just a stub currently
        }

        g_ee_memory.Init();
        OpenRatchet::Kernel::InitSyscalls();
        OpenRatchet::IOP::InitIOP();
        BuildDispatchTable();

        MIPS_EE_Context ee_ctx;
        if (ELFLoader::LoadELF(arg, g_ee_memory, ee_ctx)) {
            std::cout << "Successfully loaded ELF: " << arg << std::endl;
            std::cout << "Entry point (PC): 0x" << std::hex << ee_ctx.pc << std::dec << std::endl;
            
            R5900Context r5900 = {};
            copyContextToR5900(ee_ctx, r5900);
            PS2Runtime runtime = {};
            uint32_t call_count = 0;
            
            bool running = true;
            bool ps2_running = true;
            auto last_time = std::chrono::high_resolution_clock::now();
            const auto target_frame_time = std::chrono::nanoseconds(1000000000 / 60);

            while (running) {
                // Poll events
                SDL_Event event;
                while (SDL_PollEvent(&event)) {
                    if (event.type == SDL_QUIT) {
                        running = false;
                    }
                }

                auto current_time = std::chrono::high_resolution_clock::now();
                auto elapsed = current_time - last_time;

                if (elapsed >= target_frame_time) {
                    last_time = current_time;
                    OpenRatchet::HAL::PollEvents(running);
                }

                if (ps2_running && r5900.pc != 0) {
                    uint32_t pc = r5900.pc;
                    if (g_dispatch_table.find(pc) != g_dispatch_table.end()) {
                        if (call_count < 1000) {
                            std::cout << "[DISPATCH] Calling function at 0x" << std::hex << pc << std::dec << std::endl;
                        }
                        // We must call the recompiled function using the generated signature
                        // The generated function takes: uint8_t* mem, R5900Context* r5900, PS2Runtime* runtime
                        g_dispatch_table[pc](g_ee_memory.GetRamPointer(0), &r5900, &runtime);
                        call_count++;
                    } else {
                        std::cerr << "MISSING-TARGET: 0x" << std::hex << pc << std::dec << std::endl;
                        ps2_running = false;
                    }
                } else if (ps2_running) {
                    std::cout << "Main loop exited. running = " << running << ", r5900.pc = 0x" << std::hex << r5900.pc << std::dec << std::endl;
                    ps2_running = false;
                }

                renderer.BeginFrame();
                // Call debug overlay
                OpenRatchet::Debug::RenderOverlay(renderer, call_count);
                renderer.EndFrame();
                SDL_Delay(1);
            }
        } else {
            std::cerr << "Failed to load ELF." << std::endl;
        }
        
        OpenRatchet::HAL::Shutdown();
        return 0;
    }

    std::cout << "Status: Milestone 7 — HAL and Main Loop" << std::endl;
    return 0;
}
