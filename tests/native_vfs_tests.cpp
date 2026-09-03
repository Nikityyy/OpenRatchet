#include "platform/native_vfs.h"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

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

} // namespace

int main() {
    namespace fs = std::filesystem;
    using ratchet::platform::NativeAssetKind;
    using ratchet::platform::NativeVfs;

    TestState test;
    const fs::path root = fs::temp_directory_path() / "openratchet-native-vfs-tests";
    std::error_code cleanupError;
    fs::remove_all(root, cleanupError);
    fs::create_directories(root / "extracted");

    const fs::path toc = root / "toc.json";
    {
        std::ofstream output(toc);
        output << R"({
  "version": 1,
  "toc_size": 32,
  "wads": [
    { "num": 0, "start": 50, "length": 1 },
    { "num": 1, "start": 0, "length": 0 }
  ],
  "wads2": [
    { "num": 0, "start": 100, "length": 2 },
    { "num": 1, "start": 102, "length": 1 }
  ],
  "video": [], "vags": [], "vags2": []
})";
    }

    writeBytes(root / "extracted" / "wads" / "wad_0.wad",
               0x11u,
               NativeVfs::kSectorBytes);
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
    writeBytes(root / "extracted" / "wads2" / "wad2_1.wad",
               0x33u,
               NativeVfs::kSectorBytes);

    NativeVfs vfs;
    test.expect(vfs.initialize(root / "extracted", toc),
                "VFS initializes from an extracted tree and TOC");
    test.expect(vfs.ready(), "VFS reports ready after initialization");
    test.expect(vfs.summary().indexedAssets == 3u &&
                    vfs.summary().presentAssets == 3u &&
                    vfs.summary().missingAssets == 0u,
                "VFS reports indexed/present asset counts");

    const auto* boot = vfs.findAsset(NativeAssetKind::Wad2, 0u);
    test.expect(boot != nullptr && boot->startSector == 100u && boot->sectorCount == 2u,
                "WAD2/0 lookup preserves TOC sector metadata");
    test.expect(vfs.findAssetContainingSector(101u) == boot,
                "sector lookup resolves into the containing extracted asset");
    test.expect(vfs.findAssetContainingSector(99u) == nullptr,
                "unindexed disc gaps remain unresolved");

    std::array<std::uint8_t, NativeVfs::kSectorBytes> oneSector{};
    std::string source;
    test.expect(vfs.readSectors(101u,
                                1u,
                                oneSector.data(),
                                oneSector.size(),
                                &source),
                "partial read inside an indexed asset succeeds");
    test.expect(oneSector.front() == 0x22u && oneSector.back() == 0x22u &&
                    source == "wads2/0",
                "partial read returns bytes from the expected host asset");

    std::vector<std::uint8_t> crossing(NativeVfs::kSectorBytes * 2u, 0u);
    test.expect(vfs.readSectors(101u,
                                2u,
                                crossing.data(),
                                crossing.size(),
                                &source),
                "read spanning adjacent indexed assets succeeds");
    test.expect(crossing[0] == 0x22u &&
                    crossing[NativeVfs::kSectorBytes] == 0x33u &&
                    source == "wads2/0..wads2/1",
                "cross-asset read preserves physical disc order");

    std::array<std::uint8_t, NativeVfs::kSectorBytes> untouched{};
    untouched.fill(0xa5u);
    test.expect(!vfs.readSectors(75u,
                                 1u,
                                 untouched.data(),
                                 untouched.size()),
                "unindexed disc sector falls back instead of fabricating data");
    test.expect(untouched.front() == 0xa5u && untouched.back() == 0xa5u,
                "failed resolution does not partially modify destination memory");

    std::array<std::uint8_t, 16> prefix{};
    std::size_t bytesRead = 0u;
    test.expect(vfs.readAssetPrefix(NativeAssetKind::Wad2,
                                    0u,
                                    prefix.data(),
                                    prefix.size(),
                                    bytesRead),
                "resource-level prefix read succeeds through VFS");
    test.expect(bytesRead == prefix.size() && prefix.front() == 0x57u,
                "resource-level prefix read honors destination capacity");

    fs::remove_all(root, cleanupError);
    if (test.failures != 0) {
        std::cerr << test.failures << " native VFS test(s) failed\n";
        return 1;
    }

    std::cout << "native VFS tests passed\n";
    return 0;
}
