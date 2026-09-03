#include "runtime/openratchet_runtime.h"

#include <cstdlib>
#include <filesystem>

int main(int argc, char** argv) {
    const std::filesystem::path elf = argc > 1
        ? argv[1]
        : std::filesystem::path(RATCHET_BOOT_ELF);

    ratchet::OpenRatchetRuntime runtime;
    if (!runtime.initialize(elf)) {
        return EXIT_FAILURE;
    }

    runtime.run();
    return EXIT_SUCCESS;
}
