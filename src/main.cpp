#include "Memory.h"

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <string>

#if defined(_M_X64) || defined(__SSE__)
#include <immintrin.h>
#endif

#include "ps2_runtime.h"

namespace {
void configureHostFloatingPoint() {
#if defined(_MSC_VER) && defined(_M_X64)
    _mm_setcsr(_mm_getcsr() | 0x8000u | 0x0040u);
#elif defined(__SSE__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif
}
}

int main(int argc, char** argv) {
    configureHostFloatingPoint();
    const std::filesystem::path elf = argc > 1
        ? argv[1]
        : std::filesystem::path(RATCHET_BOOT_ELF);

    if (!std::filesystem::is_regular_file(elf)) {
        std::cerr << "Missing guest ELF: " << elf << "\n"
                  << "Run tools/bootstrap.ps1 -Stage Extract first.\n";
        return EXIT_FAILURE;
    }

    ratchet::GuestMemory memory;
    PS2Runtime runtime;
    if (!runtime.initialize("OpenRatchet 2")) {
        std::cerr << "PS2 runtime initialization failed\n";
        return EXIT_FAILURE;
    }
    if (!runtime.loadELF(elf.string())) {
        std::cerr << "Could not load guest ELF: " << elf << "\n";
        return EXIT_FAILURE;
    }
    runtime.run();
    return EXIT_SUCCESS;
}
