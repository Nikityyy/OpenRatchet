#include "game/native_services.h"

namespace ratchet::game {
namespace {
NativeGameServices g_services{};
}

void bindNativeGameServices(NativeGameServices services) noexcept {
    g_services = services;
}

void unbindNativeGameServices() noexcept {
    g_services = {};
}

const NativeGameServices& nativeGameServices() noexcept {
    return g_services;
}

} // namespace ratchet::game
