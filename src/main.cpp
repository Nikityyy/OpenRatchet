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
#include "ps2_runtime.h"

#include "openratchet/context_bridge.h"
#include "openratchet/kernel_state.h"
#include "openratchet/runtime_dispatch.h"

namespace OpenRatchet {
namespace Debug {
    void RenderOverlay(VulkanRenderer& renderer, uint32_t call_count);
}
}

uint32_t CountGeneratedFunctions() {
    uint32_t count = 0;
    for (uint32_t i = 0; i < g_ps2RecompiledFunctionTableSlotCount; ++i)
        if (g_ps2RecompiledFunctionTable[i]) ++count;
    return count;
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

        MIPS_EE_Context ee_ctx;
        if (ELFLoader::LoadELF(arg, g_ee_memory, ee_ctx)) {
            std::cout << "Successfully loaded ELF: " << arg << std::endl;
            std::cout << "Entry point (PC): 0x" << std::hex << ee_ctx.pc << std::dec << std::endl;
            
            R5900Context r5900 = {};
            copyContextToR5900(ee_ctx, r5900);
            PS2Runtime runtime = {};
            std::cout << "Registered " << CountGeneratedFunctions() << " guest functions" << std::endl;
            uint64_t tick_count = 0;
            
            bool running = true;
            bool ps2_running = true;
            auto next_tick = std::chrono::steady_clock::now();
            const auto target_frame_time = std::chrono::nanoseconds(1000000000 / 60);
            int exit_code = 0;

            while (running) {
                OpenRatchet::HAL::PollEvents(running);
                const auto now = std::chrono::steady_clock::now();
                uint32_t catch_up_ticks = 0;
                while (running && ps2_running && now >= next_tick && catch_up_ticks < 4) {
                    OpenRatchet::IOP::UpdatePAD();
                    OpenRatchet::Kernel::TickTimers();

                    if (r5900.pc == 0) {
                        std::cerr << "FATAL: guest stopped at PC=0 after " << tick_count << " ticks and "
                                  << OpenRatchet::Runtime::GetGuestDispatchCount() << " branch dispatches\n";
                        ps2_running = false;
                        exit_code = 2;
                        break;
                    }

                    const uint32_t pc = r5900.pc;
                    auto function = runtime.lookupFunction(pc);
                    if (!function) {
                        runtime.reportMissingFunction(g_ee_memory.GetRamPointer(0), &r5900, pc, pc,
                                                      PS2Runtime::GuestBranchKind::DirectJump, "frame-dispatch");
                        ps2_running = false;
                        exit_code = 2;
                        break;
                    }
                    if (tick_count < 1000) {
                        std::cout << "[GUEST-TICK] tick=" << tick_count << " pc=0x" << std::hex << pc
                                  << std::dec << std::endl;
                    }
                    OpenRatchet::Runtime::SetGuestDeadline(std::chrono::steady_clock::now() + target_frame_time);
                    function(g_ee_memory.GetRamPointer(0), &r5900, &runtime);
                    OpenRatchet::Runtime::ClearGuestDeadline();
                    ++tick_count;
                    ++catch_up_ticks;
                    next_tick += target_frame_time;
                    if (runtime.isStopRequested()) {
                        ps2_running = false;
                        exit_code = 2;
                    }
                }
                if (catch_up_ticks == 4 && now >= next_tick) next_tick = now + target_frame_time;

                renderer.BeginFrame();
                OpenRatchet::Debug::RenderOverlay(renderer, static_cast<uint32_t>(tick_count));
                renderer.EndFrame();
                if (!ps2_running) running = false;
                SDL_Delay(1);
            }
            std::cout << "Guest execution stopped: ticks=" << tick_count << " dispatches="
                      << OpenRatchet::Runtime::GetGuestDispatchCount() << " pc=0x" << std::hex
                      << r5900.pc << std::dec << std::endl;
            renderer.Shutdown();
            OpenRatchet::HAL::Shutdown();
            return exit_code;
        } else {
            std::cerr << "Failed to load ELF." << std::endl;
        }
        
        renderer.Shutdown();
        OpenRatchet::HAL::Shutdown();
        return 1;
    }

    std::cout << "Status: Milestone 7 — HAL and Main Loop" << std::endl;
    return 0;
}
