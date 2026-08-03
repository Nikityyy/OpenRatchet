#include "Memory.h"

#include <limits>

namespace ratchet {

GuestMemory::GuestMemory() : bytes_(kSize) {}

bool GuestMemory::contains(std::uint32_t address, std::size_t length) const {
    if (address < kBase || length > kSize) {
        return false;
    }
    const auto offset = static_cast<std::size_t>(address - kBase);
    return offset <= kSize - length;
}

std::byte* GuestMemory::data(std::uint32_t address, std::size_t length) {
    return contains(address, length) ? bytes_.data() + (address - kBase) : nullptr;
}

const std::byte* GuestMemory::data(std::uint32_t address, std::size_t length) const {
    return contains(address, length) ? bytes_.data() + (address - kBase) : nullptr;
}

} // namespace ratchet
