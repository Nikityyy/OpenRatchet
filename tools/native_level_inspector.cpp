#include "assets/rac1_level.h"
#include "platform/native_vfs.h"

#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <string>

namespace {

void printRange(const char* name, const ratchet::assets::Rac1SectorRange& range) {
    std::cout << "  " << name << " sector=0x" << std::hex << range.startSector
              << " sectors=0x" << range.sectorCount << std::dec << '\n';
}

} // namespace

int main(int argc, char** argv) {
    if (argc != 3 && argc != 4) {
        std::cerr << "usage: native_level_inspector <toc.json> <extracted-root> [level-index]\n";
        return 2;
    }

    const std::filesystem::path toc = argv[1];
    const std::filesystem::path extracted = argv[2];
    int requestedIndex = -1;
    if (argc == 4) {
        try {
            requestedIndex = std::stoi(argv[3]);
        } catch (...) {
            std::cerr << "invalid level index: " << argv[3] << '\n';
            return 2;
        }
    }

    ratchet::platform::NativeVfs vfs;
    if (!vfs.initialize(extracted, toc)) {
        std::cerr << "native_level_inspector: VFS initialization failed\n";
        return 1;
    }
    if (vfs.levels().empty()) {
        std::cerr << "native_level_inspector: toc.json has no semantic level table; "
                     "run tools/extract-native-levels.ps1 first\n";
        return 1;
    }

    const ratchet::platform::NativeAssetLocation* selected = nullptr;
    if (requestedIndex >= 0) {
        selected = vfs.findLevel(static_cast<std::uint32_t>(requestedIndex));
        if (selected == nullptr || !std::filesystem::is_regular_file(selected->path)) {
            std::cerr << "native_level_inspector: requested level is not extracted: "
                      << requestedIndex << '\n';
            return 1;
        }
    } else {
        for (const auto& level : vfs.levels()) {
            if (std::filesystem::is_regular_file(level.path)) {
                selected = &level;
                break;
            }
        }
    }

    if (selected == nullptr) {
        std::cerr << "native_level_inspector: no extracted level found; "
                     "run tools/extract-native-levels.ps1 first\n";
        return 1;
    }

    const auto result = ratchet::assets::inspectRac1Level(
        selected->path,
        selected->index,
        selected->startSector,
        selected->sectorCount,
        selected->headerSector);
    const auto& s = result.summary;

    std::cout << "[OpenRatchet:level] index=" << s.tocIndex
              << " id=" << s.levelId
              << " span=0x" << std::hex << s.discStartSector
              << "+0x" << s.discSectorCount
              << " headerSector=0x" << s.discHeaderSector
              << " header=0x" << s.headerSize << std::dec
              << " status=" << ratchet::assets::rac1LevelInspectStatusName(result.status)
              << '\n';
    printRange("data", s.data);
    printRange("gameplay-ntsc", s.gameplayNtsc);
    printRange("gameplay-pal", s.gameplayPal);
    printRange("occlusion", s.occlusion);
    std::cout << "[OpenRatchet:level] core index=0x" << std::hex << s.coreIndex.offset
              << "+0x" << s.coreIndex.size
              << " data=0x" << s.coreData.offset
              << "+0x" << s.coreData.size
              << " encoded=0x" << s.coreEncodedSize
              << " decompressed=0x" << s.coreDecompressedBytes << std::dec << '\n';
    std::cout << "[OpenRatchet:level] render tfrags=0x" << std::hex << s.tfragsOffset
              << " sky=0x" << s.skyOffset
              << " collision=0x" << s.collisionOffset
              << " occlusion=0x" << s.occlusionOffset << std::dec
              << " classes(moby/tie/shrub)=" << s.mobyClasses.count << '/'
              << s.tieClasses.count << '/' << s.shrubClasses.count
              << " textures(tfrag/moby/tie/shrub)=" << s.tfragTextures.count << '/'
              << s.mobyTextures.count << '/' << s.tieTextures.count << '/'
              << s.shrubTextures.count << '\n';

    return result.ok() ? 0 : 1;
}
