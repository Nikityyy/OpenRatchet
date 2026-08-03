#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

namespace ratchet {

// The EE's low 32 MiB is contiguous from the guest's point of view.
class GuestMemory final {
public:
    static constexpr std::uint32_t kBase = 0x00000000;
    static constexpr std::size_t kSize = 32u * 1024u * 1024u;

    GuestMemory();

    [[nodiscard]] bool contains(std::uint32_t address, std::size_t length = 1) const;
    [[nodiscard]] std::byte* data(std::uint32_t address, std::size_t length = 1);
    [[nodiscard]] const std::byte* data(std::uint32_t address, std::size_t length = 1) const;

private:
    std::vector<std::byte> bytes_;
};

} // namespace ratchet
