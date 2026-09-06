#include "game/native_io.h"

#include "game/native_services.h"
#include "platform/native_vfs.h"
#include "runtime/native_replacements.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

#include "ps2_runtime.h"
#include "ps2_runtime_macros.h"

namespace {

struct TestState {
    int failures = 0;

    void expect(bool condition, const char* message) {
        if (!condition) {
            ++failures;
            std::cerr << "FAIL: " << message << '\n';
        }
    }
};

void writeBytes(const std::filesystem::path& path,
                std::uint8_t value,
                std::size_t bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::vector<std::uint8_t> data(bytes, value);
    std::ofstream output(path, std::ios::binary);
    output.write(reinterpret_cast<const char*>(data.data()),
                 static_cast<std::streamsize>(data.size()));
}

const ratchet::runtime::NativeReplacement* findReplacement(
    const ratchet::runtime::NativeReplacementRegistry& registry,
    std::uint32_t address) {
    for (const auto& entry : registry.entries()) {
        if (entry.address == address) {
            return &entry;
        }
    }
    return nullptr;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    using ratchet::game::NativeGameServices;
    using ratchet::platform::NativeVfs;
    using ratchet::runtime::NativeReplacementRegistry;

    TestState test;
    const fs::path root = fs::temp_directory_path() / "openratchet-native-io-tests";
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    fs::create_directories(root / "extracted");

    const fs::path toc = root / "toc.json";
    {
        std::ofstream output(toc);
        output << R"({
  "version": 1,
  "toc_size": 40,
  "wads": [ { "num": 0, "start": 1506, "length": 9 } ],
  "wads2": [ { "num": 0, "start": 100, "length": 2 } ],
  "video": [], "vags": [], "vags2": [],
  "levels": [], "native_levels": []
})";
    }

    writeBytes(root / "extracted" / "wads" / "wad_0.wad",
               0x3cu,
               NativeVfs::kSectorBytes * 9u);
    writeBytes(root / "extracted" / "wads2" / "wad2_0.wad",
               0x22u,
               NativeVfs::kSectorBytes * 2u);
    {
        std::fstream boot(root / "extracted" / "wads2" / "wad2_0.wad",
                          std::ios::binary | std::ios::in | std::ios::out);
        const std::array<std::uint8_t, 4> magic{0x57u, 0x41u, 0x44u, 0x0fu};
        boot.write(reinterpret_cast<const char*>(magic.data()),
                   static_cast<std::streamsize>(magic.size()));
    }

    NativeVfs vfs;
    test.expect(vfs.initialize(root / "extracted", toc),
                "VFS fixture initializes");
    ratchet::game::bindNativeGameServices(NativeGameServices{&vfs});

    NativeReplacementRegistry registry;
    ratchet::game::declareNativeIoReplacements(registry);
    const auto* start = findReplacement(registry, 0x216788u);
    test.expect(start != nullptr && start->function != nullptr,
                "0x216788 game sector start is declared native");
    test.expect(start != nullptr && start->fallbackStorage == nullptr,
                "0x216788 cannot silently fall back into 989snd/SIF");

    const auto* wrapper = findReplacement(registry, 0x216828u);
    test.expect(wrapper != nullptr && wrapper->function != nullptr,
                "0x216828 game sector wrapper is declared native");
    test.expect(wrapper != nullptr && wrapper->fallbackStorage == nullptr,
                "0x216828 cannot silently fall back into 989snd/SIF");

    constexpr std::uint32_t kGuestRamBytes = 0x02000000u;
    constexpr std::uint32_t kDestination = 0x001aabc0u;
    constexpr std::uint32_t kManagerBase = 0x001516d0u;
    std::vector<std::uint8_t> rdram(kGuestRamBytes, 0xa5u);
    // The native audio bootstrap leaves the 989snd transport manager inactive.
    // Keep that realistic precondition and verify the native sector wrapper
    // does not manufacture the transport's source/count/destination bookkeeping.
    std::array<std::uint8_t, 0x90> managerBefore{};
    std::fill_n(rdram.begin() + kManagerBase, managerBefore.size(), 0u);

    if (start != nullptr && start->function != nullptr) {
        R5900Context ctx;
        SET_GPR_U32(&ctx, 4, kDestination);
        SET_GPR_U32(&ctx, 5, 1506u);
        SET_GPR_U32(&ctx, 6, 9u);
        SET_GPR_U32(&ctx, 31, 0x00a5a5a5u);
        start->function(rdram.data(), &ctx, nullptr);

        test.expect(getRegU32(&ctx, 2) == NativeVfs::kSectorBytes * 9u,
                    "0x216788 returns sectorCount * 0x800 bytes");
        test.expect(ctx.pc == 0x00a5a5a5u,
                    "0x216788 returns directly to the original guest caller");
        test.expect(rdram[kDestination] == 0x3cu &&
                        rdram[kDestination + NativeVfs::kSectorBytes * 9u - 1u] == 0x3cu,
                    "0x216788 completes the indexed sector read in guest RAM");

        bool managerUnchanged = true;
        for (std::size_t i = 0; i < managerBefore.size(); ++i) {
            if (rdram[kManagerBase + i] != managerBefore[i]) {
                managerUnchanged = false;
                break;
            }
        }
        test.expect(managerUnchanged,
                    "native game-sector start does not synthesize 989snd transport state");
    }

    if (wrapper != nullptr && wrapper->function != nullptr) {
        R5900Context ctx;
        SET_GPR_U32(&ctx, 4, kDestination); // retail a0 = destination
        SET_GPR_U32(&ctx, 5, 1506u);       // retail WAD0 source sector
        SET_GPR_U32(&ctx, 6, 9u);          // retail WAD0 sector count
        SET_GPR_U32(&ctx, 31, 0x00c0ffeeu);
        wrapper->function(rdram.data(), &ctx, nullptr);

        test.expect(getRegU32(&ctx, 2) == NativeVfs::kSectorBytes * 9u,
                    "0x216828 returns sectorCount * 0x800 bytes");
        test.expect(ctx.pc == 0x00c0ffeeu,
                    "0x216828 returns directly to the original guest caller");
        test.expect(rdram[kDestination] == 0x3cu &&
                        rdram[kDestination + NativeVfs::kSectorBytes * 9u - 1u] == 0x3cu,
                    "0x216828 copies the indexed sector to the requested guest destination");

        bool managerUnchanged = true;
        for (std::size_t i = 0; i < managerBefore.size(); ++i) {
            if (rdram[kManagerBase + i] != managerBefore[i]) {
                managerUnchanged = false;
                break;
            }
        }
        test.expect(managerUnchanged,
                    "native game-sector HLE does not synthesize 989snd transport state");

        std::fill(rdram.begin() + kDestination,
                  rdram.begin() + kDestination + NativeVfs::kSectorBytes,
                  0x7eu);
        R5900Context unresolved;
        SET_GPR_U32(&unresolved, 4, kDestination);
        SET_GPR_U32(&unresolved, 5, 75u); // unindexed gap
        SET_GPR_U32(&unresolved, 6, 1u);
        SET_GPR_U32(&unresolved, 31, 0x00bad00du);
        wrapper->function(rdram.data(), &unresolved, nullptr);

        test.expect(getRegU32(&unresolved, 2) == 0u &&
                        unresolved.pc == 0x00bad00du,
                    "unresolved native game-sector load returns the retail failure value");
        test.expect(rdram[kDestination] == 0x7eu &&
                        rdram[kDestination + NativeVfs::kSectorBytes - 1u] == 0x7eu,
                    "unresolved native game-sector load cannot fabricate or partially write data");
    }

    ratchet::game::unbindNativeGameServices();
    fs::remove_all(root, cleanupError);
    if (test.failures != 0) {
        std::cerr << test.failures << " native IO test(s) failed\n";
        return 1;
    }

    std::cout << "native IO tests passed\n";
    return 0;
}
