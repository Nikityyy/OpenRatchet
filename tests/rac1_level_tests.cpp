#include "assets/rac1_level.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <vector>

namespace {

void writeLe32(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint32_t value) {
    bytes.at(offset + 0u) = static_cast<std::uint8_t>(value & 0xffu);
    bytes.at(offset + 1u) = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes.at(offset + 2u) = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes.at(offset + 3u) = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

void writeRange(std::vector<std::uint8_t>& bytes,
                std::size_t offset,
                std::uint32_t start,
                std::uint32_t length) {
    writeLe32(bytes, offset, start);
    writeLe32(bytes, offset + 4u, length);
}

std::vector<std::uint8_t> makeFixture() {
    constexpr std::uint32_t kLevelStart = 100u;
    constexpr std::uint32_t kHeaderSector = 102u;
    constexpr std::uint32_t kLevelSectors = 20u;
    constexpr std::uint32_t kDataStart = 110u;
    constexpr std::size_t kSectorBytes = 0x800u;
    constexpr std::size_t kHeaderOffset = (kHeaderSector - kLevelStart) * kSectorBytes;
    constexpr std::size_t kDataOffset = (kDataStart - kLevelStart) * kSectorBytes;

    std::vector<std::uint8_t> bytes(kLevelSectors * kSectorBytes, 0u);
    writeLe32(bytes, kHeaderOffset + 0x000u, 7u);
    writeLe32(bytes, kHeaderOffset + 0x004u, 0x2434u);
    writeRange(bytes, kHeaderOffset + 0x008u, kDataStart, 5u);
    writeRange(bytes, kHeaderOffset + 0x010u, 105u, 1u);
    writeRange(bytes, kHeaderOffset + 0x018u, 0u, 0u);
    writeRange(bytes, kHeaderOffset + 0x020u, 0u, 0u);

    // A tiny valid gameplay WAD in its own sector range.
    const std::size_t gameplayWad = (105u - kLevelStart) * kSectorBytes;
    bytes[gameplayWad + 0u] = 'W';
    bytes[gameplayWad + 1u] = 'A';
    bytes[gameplayWad + 2u] = 'D';
    writeLe32(bytes, gameplayWad + 3u, 0x31u);
    bytes[gameplayWad + 0x10u] = 49u;
    for (std::size_t i = 0u; i < 32u; ++i) {
        bytes[gameplayWad + 0x11u + i] = static_cast<std::uint8_t>(0x80u + i);
    }

    // Level-data header: exact overlay records, core index, raw GS RAM, then core.
    writeRange(bytes, kDataOffset + 0x00u, 0x80u, 0x2cu);
    const std::size_t overlay = kDataOffset + 0x80u;
    writeLe32(bytes, overlay + 0x00u, 0x001000u);
    writeLe32(bytes, overlay + 0x04u, 4u);
    writeLe32(bytes, overlay + 0x08u, 1u);
    writeLe32(bytes, overlay + 0x0cu, 0x11111111u);
    bytes[overlay + 0x10u] = 0x10u;
    bytes[overlay + 0x11u] = 0x20u;
    bytes[overlay + 0x12u] = 0x30u;
    bytes[overlay + 0x13u] = 0x40u;
    writeLe32(bytes, overlay + 0x14u, 0x002000u);
    writeLe32(bytes, overlay + 0x18u, 8u);
    writeLe32(bytes, overlay + 0x1cu, 8u);
    writeLe32(bytes, overlay + 0x20u, 0x22222222u);
    for (std::size_t i = 0u; i < 8u; ++i) {
        bytes[overlay + 0x24u + i] = static_cast<std::uint8_t>(0x50u + i);
    }

    writeRange(bytes, kDataOffset + 0x10u, 0x100u, 0x1000u);
    writeRange(bytes, kDataOffset + 0x18u, 0x1100u, 0x200u);
    writeRange(bytes, kDataOffset + 0x50u, 0x1400u, 0x100u);
    bytes[kDataOffset + 0x1100u] = 0xa5u;

    const std::size_t core = kDataOffset + 0x100u;
    writeLe32(bytes, core + 0x08u, 4u);   // tfrags
    writeLe32(bytes, core + 0x0cu, 0u);   // optional occlusion
    writeLe32(bytes, core + 0x10u, 8u);   // sky
    writeLe32(bytes, core + 0x14u, 12u);  // collision
    // LevelCoreHeader array pairs are {count, offset}.
    writeRange(bytes, core + 0x00u, 2u, 0x0c0u);
    writeRange(bytes, core + 0x18u, 3u, 0x100u);
    writeRange(bytes, core + 0x20u, 4u, 0x200u);
    writeRange(bytes, core + 0x28u, 5u, 0x300u);
    writeRange(bytes, core + 0x30u, 6u, 0x400u);
    writeRange(bytes, core + 0x38u, 7u, 0x500u);
    writeRange(bytes, core + 0x40u, 8u, 0x600u);
    writeRange(bytes, core + 0x48u, 9u, 0x700u);
    writeRange(bytes, core + 0x50u, 10u, 0x800u);
    writeRange(bytes, core + 0x58u, 11u, 0x900u);
    writeLe32(bytes, core + 0x60u, 16u);
    writeLe32(bytes, core + 0x74u, 0x345678u);
    writeLe32(bytes, core + 0x78u, 0x9a0u);
    writeLe32(bytes, core + 0x7cu, 0xabcdefu);
    writeLe32(bytes, core + 0x88u, 0x31u);
    writeLe32(bytes, core + 0x8cu, 32u);

    // A tiny valid WAD whose initial-literal packet expands to 32 bytes.
    const std::size_t wad = kDataOffset + 0x1400u;
    bytes[wad + 0u] = 'W';
    bytes[wad + 1u] = 'A';
    bytes[wad + 2u] = 'D';
    writeLe32(bytes, wad + 3u, 0x31u);
    bytes[wad + 0x10u] = 49u; // 49 - 17 = 32 literal bytes.
    for (std::size_t i = 0u; i < 32u; ++i) {
        bytes[wad + 0x11u + i] = static_cast<std::uint8_t>(i);
    }
    return bytes;
}

} // namespace

int main() {
    namespace fs = std::filesystem;
    const fs::path root = fs::temp_directory_path() / "openratchet-rac1-level-tests";
    const fs::path level = root / "level_00.wad";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root);

    const std::vector<std::uint8_t> fixture = makeFixture();
    {
        std::ofstream output(level, std::ios::binary);
        output.write(reinterpret_cast<const char*>(fixture.data()),
                     static_cast<std::streamsize>(fixture.size()));
    }

    const auto loaded = ratchet::assets::loadRac1LevelCore(level, 0u, 100u, 20u, 102u);
    if (!loaded.ok()) {
        std::cerr << "rac1_level_tests: load failed status="
                  << ratchet::assets::rac1LevelInspectStatusName(loaded.status) << '\n';
        return 1;
    }
    if (loaded.overlay.size() != 0x2cu || loaded.overlaySegments.size() != 2u ||
        loaded.overlaySegments[0].destination != 0x001000u ||
        loaded.overlaySegments[0].payloadOffset != 0x10u ||
        loaded.overlaySegments[0].payloadSize != 4u ||
        loaded.overlaySegments[1].destination != 0x002000u ||
        loaded.overlaySegments[1].payloadOffset != 0x24u ||
        loaded.overlaySegments[1].payloadSize != 8u ||
        loaded.coreIndex.size() != 0x1000u || loaded.gsRam.size() != 0x200u ||
        loaded.gsRam.front() != 0xa5u || loaded.core.size() != 32u ||
        loaded.gameplay.size() != 32u || loaded.gameplay.front() != 0x80u) {
        std::cerr << "rac1_level_tests: renderer-owned core/index/GS blobs mismatch\n";
        return 1;
    }

    const auto result = ratchet::assets::inspectRac1Level(level, 0u, 100u, 20u, 102u);
    if (!result.ok()) {
        std::cerr << "rac1_level_tests: inspect failed status="
                  << ratchet::assets::rac1LevelInspectStatusName(result.status) << '\n';
        return 1;
    }

    const auto& s = result.summary;
    if (s.levelId != 7u || s.headerSize != 0x2434u ||
        s.discHeaderSector != 102u ||
        s.data.startSector != 110u || s.data.sectorCount != 5u ||
        s.overlay.offset != 0x80u || s.overlay.size != 0x2cu ||
        s.overlaySegmentCount != 2u || s.overlayPayloadBytes != 12u ||
        s.coreIndex.offset != 0x100u || s.coreIndex.size != 0x1000u ||
        s.coreData.offset != 0x1400u || s.coreEncodedSize != 0x31u ||
        s.coreDecompressedBytes != 32u || s.gameplayEncodedSize != 0x31u ||
        s.gameplayDecompressedBytes != 32u ||
        s.tfragsOffset != 4u || s.skyOffset != 8u || s.collisionOffset != 12u ||
        s.gsRamTable.offset != 0x0c0u || s.gsRamTable.count != 2u ||
        s.mobyClasses.offset != 0x100u || s.mobyClasses.count != 3u ||
        s.tieClasses.offset != 0x200u || s.tieClasses.count != 4u ||
        s.shrubClasses.offset != 0x300u || s.shrubClasses.count != 5u ||
        s.tfragTextures.offset != 0x400u || s.tfragTextures.count != 6u ||
        s.coreHeader74 != 0x345678u ||
        s.ratchetSequenceTableOffset != 0x9a0u ||
        s.coreHeader7c != 0xabcdefu) {
        std::cerr << "rac1_level_tests: parsed metadata mismatch\n";
        return 1;
    }

    const auto emptyOverlay = ratchet::assets::parseRac1LevelOverlay({});
    if (!emptyOverlay.ok() || !emptyOverlay.segments.empty() || emptyOverlay.payloadBytes != 0u) {
        std::cerr << "rac1_level_tests: empty overlay must be valid and empty\n";
        return 1;
    }

    std::vector<std::uint8_t> truncated(15u, 0u);
    const auto truncatedOverlay = ratchet::assets::parseRac1LevelOverlay(truncated);
    if (truncatedOverlay.status != ratchet::assets::Rac1LevelOverlayStatus::TruncatedHeader) {
        std::cerr << "rac1_level_tests: truncated overlay accepted\n";
        return 1;
    }

    std::vector<std::uint8_t> outOfRangePayload(16u, 0u);
    writeLe32(outOfRangePayload, 0u, 0x001000u);
    writeLe32(outOfRangePayload, 4u, 8u);
    const auto outOfRangeOverlay = ratchet::assets::parseRac1LevelOverlay(outOfRangePayload);
    if (outOfRangeOverlay.status != ratchet::assets::Rac1LevelOverlayStatus::PayloadOutOfRange) {
        std::cerr << "rac1_level_tests: out-of-range overlay payload accepted\n";
        return 1;
    }

    std::vector<std::uint8_t> overflowingDestination(24u, 0u);
    writeLe32(overflowingDestination, 0u, 0xfffffffcu);
    writeLe32(overflowingDestination, 4u, 8u);
    const auto overflowingOverlay =
        ratchet::assets::parseRac1LevelOverlay(overflowingDestination);
    if (overflowingOverlay.status != ratchet::assets::Rac1LevelOverlayStatus::DestinationOverflow) {
        std::cerr << "rac1_level_tests: overflowing overlay destination accepted\n";
        return 1;
    }

    fs::remove_all(root, ec);
    std::cout << "R&C1 native level parser tests passed\n";
    return 0;
}
