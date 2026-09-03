#pragma once

#include <filesystem>
#include <memory>

namespace ratchet {

// Top-level native PC runtime. The PS2Recomp runtime is deliberately hidden
// behind this class and acts only as the current fallback executor for original
// EE game logic while platform services migrate to native OpenRatchet systems.
class OpenRatchetRuntime final {
public:
    OpenRatchetRuntime();
    ~OpenRatchetRuntime();

    OpenRatchetRuntime(const OpenRatchetRuntime&) = delete;
    OpenRatchetRuntime& operator=(const OpenRatchetRuntime&) = delete;

    bool initialize(const std::filesystem::path& elf);
    void run();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ratchet
