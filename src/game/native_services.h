#pragma once

namespace ratchet::platform {
class NativeVfs;
}

namespace ratchet::game {

// Bridge from PS2Recomp's raw guest-function callback ABI to services owned by
// OpenRatchetRuntime.  The host owns every pointed-to object and unbinds the
// bridge before those objects are destroyed.
struct NativeGameServices {
    platform::NativeVfs* vfs = nullptr;
};

void bindNativeGameServices(NativeGameServices services) noexcept;
void unbindNativeGameServices() noexcept;
const NativeGameServices& nativeGameServices() noexcept;

} // namespace ratchet::game
