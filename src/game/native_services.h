#pragma once

#include <cstdint>

namespace ratchet::platform {
struct NativeAssetLocation;
class NativeVfs;
}

namespace ratchet::game {

// Bridge from PS2Recomp's raw guest-function callback ABI to services owned by
// OpenRatchetRuntime.  The host owns every pointed-to object and unbinds the
// bridge before those objects are destroyed.
using NativeIndexedAssetReadObserver = void (*)(
    void* userData,
    const platform::NativeAssetLocation& asset,
    std::uint32_t sourceSector,
    std::uint32_t sectorCount,
    std::uint32_t destination);

struct NativeGameServices {
    platform::NativeVfs* vfs = nullptr;
    NativeIndexedAssetReadObserver indexedAssetReadObserver = nullptr;
    void* indexedAssetReadUserData = nullptr;
};

void bindNativeGameServices(NativeGameServices services) noexcept;
void unbindNativeGameServices() noexcept;
const NativeGameServices& nativeGameServices() noexcept;

} // namespace ratchet::game
