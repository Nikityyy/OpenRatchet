#include "assets/rac1_moby.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ratchet::assets {
namespace {

constexpr std::size_t kMobyClassEntryBytes = 0x20u;
constexpr std::size_t kMobyClassHeaderBytes = 0x48u;
constexpr std::size_t kMobyPacketHeaderBytes = 0x10u;
constexpr std::size_t kMobyVertexTableHeaderBytes = 0x20u; // R&C1
constexpr std::size_t kMobyVertexBytes = 0x10u;
constexpr std::size_t kMobyMatrixTransferBytes = 0x2u;
constexpr std::size_t kMobyTexturePrimitiveBytes = 0x40u;
constexpr std::size_t kGameplayHeaderBytes = 0x90u;
constexpr std::size_t kInstanceBlockHeaderBytes = 0x10u;
constexpr std::size_t kMobyInstanceBytes = 0x78u;
constexpr std::size_t kVertexCacheSize = 512u;
constexpr std::size_t kMaxCount = 1u << 20u;
constexpr float kVertexScale = 1.0f / 1024.0f;
constexpr float kTexcoordScale = 1.0f / 4096.0f;

std::uint16_t readU16(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return static_cast<std::uint16_t>(b[o]) |
           (static_cast<std::uint16_t>(b[o + 1u]) << 8u);
}
std::int16_t readI16(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return static_cast<std::int16_t>(readU16(b, o));
}
std::uint32_t readU32(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return static_cast<std::uint32_t>(b[o]) |
           (static_cast<std::uint32_t>(b[o + 1u]) << 8u) |
           (static_cast<std::uint32_t>(b[o + 2u]) << 16u) |
           (static_cast<std::uint32_t>(b[o + 3u]) << 24u);
}
std::int32_t readI32(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    return static_cast<std::int32_t>(readU32(b, o));
}
float readF32(std::span<const std::uint8_t> b, std::size_t o) noexcept {
    const std::uint32_t bits = readU32(b, o);
    float value = 0.0f;
    static_assert(sizeof(bits) == sizeof(value));
    std::memcpy(&value, &bits, sizeof(value));
    return value;
}
bool fits(std::size_t o, std::size_t n, std::size_t cap) noexcept {
    return o <= cap && n <= cap - o;
}
bool checkedAdd(std::size_t a, std::size_t b, std::size_t& out) noexcept {
    if (a > std::numeric_limits<std::size_t>::max() - b) return false;
    out = a + b;
    return true;
}
bool checkedMul(std::size_t a, std::size_t b, std::size_t& out) noexcept {
    if (b != 0u && a > std::numeric_limits<std::size_t>::max() / b) return false;
    out = a * b;
    return true;
}
std::size_t alignUp(std::size_t value, std::size_t alignment) noexcept {
    const std::size_t mask = alignment - 1u;
    return (value + mask) & ~mask;
}

struct ClassEntry {
    std::uint32_t offset = 0u;
    std::int32_t oClass = 0;
    std::vector<std::uint8_t> textures;
};

bool parseClassEntries(std::span<const std::uint8_t> index,
                       Rac1ArrayRange range,
                       std::unordered_map<std::int32_t, ClassEntry>& out) {
    if (range.count > kMaxCount) return false;
    std::size_t bytes = 0u;
    if (!checkedMul(range.count, kMobyClassEntryBytes, bytes) ||
        !fits(range.offset, bytes, index.size())) {
        return false;
    }

    out.reserve(range.count);
    for (std::uint32_t i = 0u; i < range.count; ++i) {
        const std::size_t o = static_cast<std::size_t>(range.offset) +
                              static_cast<std::size_t>(i) * kMobyClassEntryBytes;
        ClassEntry entry{};
        entry.offset = readU32(index, o + 0x0u);
        entry.oClass = readI32(index, o + 0x4u);
        for (std::size_t t = 0u; t < 16u; ++t) {
            const std::uint8_t texture = index[o + 0x10u + t];
            if (texture == 0xffu) break;
            entry.textures.push_back(texture);
        }
        out[entry.oClass] = std::move(entry);
    }
    return true;
}

struct Instance {
    std::int32_t oClass = 0;
    float scale = 1.0f;
    std::array<float, 3> position{};
    std::array<float, 3> rotation{};
    std::array<std::uint8_t, 4> color{255u, 255u, 255u, 255u};
};

bool parseInstances(std::span<const std::uint8_t> gameplay,
                    std::vector<Instance>& out) {
    if (gameplay.size() < kGameplayHeaderBytes) return false;
    const std::int32_t offsetSigned = readI32(gameplay, 0x44u);
    if (offsetSigned < 0) return false;
    const std::size_t offset = static_cast<std::size_t>(offsetSigned);
    if (offset == 0u) return true;
    if (!fits(offset, kInstanceBlockHeaderBytes, gameplay.size())) return false;

    const std::int32_t countSigned = readI32(gameplay, offset);
    if (countSigned < 0 || static_cast<std::size_t>(countSigned) > kMaxCount) return false;
    const std::size_t count = static_cast<std::size_t>(countSigned);
    std::size_t bytes = 0u;
    if (!checkedMul(count, kMobyInstanceBytes, bytes) ||
        !fits(offset + kInstanceBlockHeaderBytes, bytes, gameplay.size())) {
        return false;
    }

    out.reserve(count);
    for (std::size_t i = 0u; i < count; ++i) {
        const std::size_t o = offset + kInstanceBlockHeaderBytes + i * kMobyInstanceBytes;
        Instance instance{};
        instance.oClass = readI32(gameplay, o + 0x18u);
        instance.scale = readF32(gameplay, o + 0x1cu);
        for (std::size_t axis = 0u; axis < 3u; ++axis) {
            instance.position[axis] = readF32(gameplay, o + 0x30u + axis * 4u);
            instance.rotation[axis] = readF32(gameplay, o + 0x3cu + axis * 4u);
        }
        // R&C's moby shader effectively multiplies the 8-bit ambient colour by
        // two before the texture sample reaches the final framebuffer.
        for (std::size_t channel = 0u; channel < 3u; ++channel) {
            const unsigned expanded = static_cast<unsigned>(gameplay[o + 0x64u + channel]) * 2u;
            instance.color[channel] = static_cast<std::uint8_t>(std::min(255u, expanded));
        }
        instance.color[3] = 255u;
        if (!std::isfinite(instance.scale) ||
            !std::isfinite(instance.position[0]) || !std::isfinite(instance.position[1]) ||
            !std::isfinite(instance.position[2]) || !std::isfinite(instance.rotation[0]) ||
            !std::isfinite(instance.rotation[1]) || !std::isfinite(instance.rotation[2])) {
            return false;
        }
        out.push_back(instance);
    }
    return true;
}

struct VifUnpack {
    std::size_t payloadOffset = 0u;
    std::size_t payloadBytes = 0u;
    std::size_t logicalBytes = 0u;
};

bool oneWordVif(std::uint8_t cmd) noexcept {
    switch (cmd) {
    case 0x00u: // NOP
    case 0x01u: // STCYCL
    case 0x02u: // OFFSET
    case 0x03u: // BASE
    case 0x04u: // ITOP
    case 0x05u: // STMOD
    case 0x06u: // MSKPATH3
    case 0x07u: // MARK
    case 0x10u: // FLUSHE
    case 0x11u: // FLUSH
    case 0x13u: // FLUSHA
    case 0x14u: // MSCAL
    case 0x15u: // MSCALF
    case 0x17u: // MSCNT
        return true;
    default:
        return false;
    }
}

bool parseVifUnpacks(std::span<const std::uint8_t> buffer,
                     std::vector<VifUnpack>& unpacks) {
    if ((buffer.size() & 3u) != 0u) return false;
    std::uint16_t wl = 1u;
    std::uint16_t cl = 1u;
    std::size_t offset = 0u;

    while (offset < buffer.size()) {
        if (!fits(offset, 4u, buffer.size())) return false;
        const std::uint32_t word = readU32(buffer, offset);
        const std::uint16_t immediate = static_cast<std::uint16_t>(word & 0xffffu);
        std::uint16_t num = static_cast<std::uint16_t>((word >> 16u) & 0xffu);
        if (num == 0u) num = 256u;
        const std::uint8_t cmd = static_cast<std::uint8_t>((word >> 24u) & 0x7fu);

        std::size_t words = 0u;
        std::size_t logicalBytes = 0u;
        if (oneWordVif(cmd)) {
            words = 1u;
        } else if (cmd == 0x20u) { // STMASK
            words = 2u;
        } else if (cmd == 0x30u || cmd == 0x31u) { // STROW/STCOL
            words = 5u;
        } else if (cmd == 0x4au) { // MPG
            words = 1u + static_cast<std::size_t>(num) * 2u;
        } else if (cmd == 0x50u || cmd == 0x51u) { // DIRECT / DIRECTHL
            const std::size_t qwords = immediate == 0u ? 65536u : immediate;
            if (!checkedMul(qwords, 4u, words)) return false;
            words += 1u;
        } else if ((cmd & 0x60u) == 0x60u) { // UNPACK
            if (wl > cl) return false;
            const std::size_t componentCount = static_cast<std::size_t>(((cmd & 0x0cu) >> 2u) + 1u);
            const std::size_t componentBits = 32u >> (cmd & 0x03u);
            std::size_t totalBits = 0u;
            if (!checkedMul(componentCount, componentBits, totalBits) ||
                !checkedMul(totalBits, num, totalBits)) {
                return false;
            }
            logicalBytes = (totalBits + 7u) / 8u;
            const std::size_t payloadWords = (totalBits + 31u) / 32u;
            words = 1u + payloadWords;
        } else {
            return false;
        }

        std::size_t commandBytes = 0u;
        if (!checkedMul(words, 4u, commandBytes) || !fits(offset, commandBytes, buffer.size())) {
            return false;
        }
        if ((cmd & 0x60u) == 0x60u) {
            const std::size_t payloadBytes = commandBytes - 4u;
            if (logicalBytes > payloadBytes) return false;
            unpacks.push_back({offset + 4u, payloadBytes, logicalBytes});
        }
        if (cmd == 0x01u) {
            cl = static_cast<std::uint16_t>(immediate & 0xffu);
            wl = static_cast<std::uint16_t>((immediate >> 8u) & 0xffu);
            if (cl == 0u) cl = 256u;
            if (wl == 0u) wl = 256u;
        }
        offset += commandBytes;
    }
    return true;
}

struct RawVertex {
    std::uint16_t cacheAddress = 0u;
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
    std::int16_t s = 0;
    std::int16_t t = 0;
};

struct PacketData {
    std::vector<RawVertex> explicitVertices;
    std::vector<std::uint16_t> duplicateCacheAddresses;
    std::vector<std::array<std::int16_t, 2>> texcoords;
    std::vector<std::int8_t> indices;
    std::vector<std::int8_t> secretIndices;
    std::vector<std::int32_t> localMaterials;
};

bool parsePacket(std::span<const std::uint8_t> core,
                 std::size_t classBase,
                 std::size_t packetHeader,
                 PacketData& out,
                 Rac1MobyStatus& status) {
    const std::int32_t vifOffsetSigned = readI32(core, packetHeader + 0x0u);
    const std::uint16_t vifSizeQw = readU16(core, packetHeader + 0x4u);
    const std::uint16_t textureUnpackOffsetQw = readU16(core, packetHeader + 0x6u);
    const std::int32_t vertexOffsetSigned = readI32(core, packetHeader + 0x8u);
    const std::uint8_t vertexDataSizeQw = core[packetHeader + 0xcu];
    const std::uint8_t transferVertexCount = core[packetHeader + 0xfu];
    if (vifOffsetSigned < 0 || vertexOffsetSigned < 0 || vifSizeQw == 0u) {
        status = Rac1MobyStatus::InvalidPacket;
        return false;
    }

    std::size_t vifBase = 0u;
    std::size_t vifBytes = 0u;
    if (!checkedAdd(classBase, static_cast<std::size_t>(vifOffsetSigned), vifBase) ||
        !checkedMul(vifSizeQw, 0x10u, vifBytes) ||
        !fits(vifBase, vifBytes, core.size())) {
        status = Rac1MobyStatus::InvalidPacket;
        return false;
    }
    const std::span<const std::uint8_t> vif = core.subspan(vifBase, vifBytes);
    std::vector<VifUnpack> unpacks;
    if (!parseVifUnpacks(vif, unpacks) || unpacks.size() < 2u || unpacks.size() > 3u ||
        ((textureUnpackOffsetQw != 0u) != (unpacks.size() == 3u))) {
        status = Rac1MobyStatus::InvalidVifLayout;
        return false;
    }

    const auto& uvUnpack = unpacks[0];
    if ((uvUnpack.logicalBytes % 4u) != 0u) {
        status = Rac1MobyStatus::InvalidVifLayout;
        return false;
    }
    for (std::size_t o = 0u; o < uvUnpack.logicalBytes; o += 4u) {
        out.texcoords.push_back({readI16(vif, uvUnpack.payloadOffset + o + 0u),
                                 readI16(vif, uvUnpack.payloadOffset + o + 2u)});
    }

    const auto& indexUnpack = unpacks[1];
    if (indexUnpack.logicalBytes < 4u) {
        status = Rac1MobyStatus::InvalidVifLayout;
        return false;
    }
    out.secretIndices.push_back(static_cast<std::int8_t>(vif[indexUnpack.payloadOffset + 0x2u]));
    for (std::size_t o = 4u; o < indexUnpack.logicalBytes; ++o) {
        out.indices.push_back(static_cast<std::int8_t>(vif[indexUnpack.payloadOffset + o]));
    }

    if (unpacks.size() == 3u) {
        const auto& textureUnpack = unpacks[2];
        if ((textureUnpack.logicalBytes % kMobyTexturePrimitiveBytes) != 0u) {
            status = Rac1MobyStatus::InvalidVifLayout;
            return false;
        }
        const std::size_t textureCount = textureUnpack.logicalBytes / kMobyTexturePrimitiveBytes;
        for (std::size_t i = 0u; i < textureCount; ++i) {
            // The hidden strip indices are packed into consecutive 0x10-byte
            // GIF-AD padding slots, not one-at-+0xc of each 0x40-byte material
            // record. This odd layout is how the retail R&C1 packet reader
            // reconstructs material-change indices.
            const std::size_t secretOffset = textureUnpack.payloadOffset + i * 0x10u + 0x0cu;
            if (secretOffset >= textureUnpack.payloadOffset + textureUnpack.logicalBytes) {
                status = Rac1MobyStatus::InvalidVifLayout;
                return false;
            }
            out.secretIndices.push_back(static_cast<std::int8_t>(vif[secretOffset]));

            const std::size_t materialOffset =
                textureUnpack.payloadOffset + i * kMobyTexturePrimitiveBytes + 0x20u;
            // TEX0.low is the class-local texture slot; -1 denotes a special
            // material that the reference renderer discards.
            out.localMaterials.push_back(readI32(vif, materialOffset));
        }
    }

    std::size_t vertexBase = 0u;
    if (!checkedAdd(classBase, static_cast<std::size_t>(vertexOffsetSigned), vertexBase) ||
        !fits(vertexBase, kMobyVertexTableHeaderBytes, core.size())) {
        status = Rac1MobyStatus::InvalidVertexTable;
        return false;
    }

    const std::array<std::int32_t, 8> header = {
        readI32(core, vertexBase + 0x00u), // matrix transfers
        readI32(core, vertexBase + 0x04u), // two-way blend verts
        readI32(core, vertexBase + 0x08u), // three-way blend verts
        readI32(core, vertexBase + 0x0cu), // main verts
        readI32(core, vertexBase + 0x10u), // duplicate verts
        readI32(core, vertexBase + 0x14u), // transfer verts
        readI32(core, vertexBase + 0x18u), // vertex table offset
        readI32(core, vertexBase + 0x1cu), // unknownE / serialized end
    };
    for (std::int32_t value : header) {
        if (value < 0 || static_cast<std::size_t>(value) > kMaxCount * kMobyVertexBytes) {
            status = Rac1MobyStatus::InvalidVertexTable;
            return false;
        }
    }
    const std::size_t matrixTransferCount = static_cast<std::size_t>(header[0]);
    const std::size_t vertexCount = static_cast<std::size_t>(header[1]) +
                                    static_cast<std::size_t>(header[2]) +
                                    static_cast<std::size_t>(header[3]);
    const std::size_t duplicateCount = static_cast<std::size_t>(header[4]);
    const std::size_t headerTransferCount = static_cast<std::size_t>(header[5]);
    const std::size_t vertexTableOffset = static_cast<std::size_t>(header[6]);
    const std::size_t unknownE = static_cast<std::size_t>(header[7]);
    if (vertexCount > kMaxCount || duplicateCount > kMaxCount ||
        headerTransferCount != transferVertexCount ||
        vertexTableOffset / 0x10u > vertexDataSizeQw ||
        unknownE < vertexTableOffset) {
        status = Rac1MobyStatus::InvalidVertexTable;
        return false;
    }

    std::size_t verticesOffset = 0u;
    std::size_t verticesBytes = 0u;
    if (!checkedAdd(vertexBase, vertexTableOffset, verticesOffset) ||
        !checkedMul(vertexCount, kMobyVertexBytes, verticesBytes) ||
        !fits(verticesOffset, verticesBytes, core.size())) {
        status = Rac1MobyStatus::InvalidVertexTable;
        return false;
    }
    out.explicitVertices.reserve(vertexCount);
    for (std::size_t i = 0u; i < vertexCount; ++i) {
        const std::size_t o = verticesOffset + i * kMobyVertexBytes;
        out.explicitVertices.push_back({
            static_cast<std::uint16_t>(readU16(core, o + 0x0u) & 0x01ffu),
            readI16(core, o + 0xau),
            readI16(core, o + 0xcu),
            readI16(core, o + 0xeu),
            0,
            0,
        });
    }

    std::size_t arrayOffset = vertexBase + kMobyVertexTableHeaderBytes;
    std::size_t matrixTransferBytes = 0u;
    if (!checkedMul(matrixTransferCount, kMobyMatrixTransferBytes, matrixTransferBytes) ||
        !fits(arrayOffset, matrixTransferBytes, core.size())) {
        status = Rac1MobyStatus::InvalidVertexTable;
        return false;
    }
    arrayOffset += matrixTransferBytes;
    arrayOffset = alignUp(arrayOffset, 8u);
    std::size_t duplicateBytes = 0u;
    if (!checkedMul(duplicateCount, 2u, duplicateBytes) ||
        !fits(arrayOffset, duplicateBytes, core.size())) {
        status = Rac1MobyStatus::InvalidVertexTable;
        return false;
    }
    out.duplicateCacheAddresses.reserve(duplicateCount);
    for (std::size_t i = 0u; i < duplicateCount; ++i) {
        out.duplicateCacheAddresses.push_back(
            static_cast<std::uint16_t>(readU16(core, arrayOffset + i * 2u) >> 7u));
    }

    // Reconstruct the unusual cache-address epilogue exactly like the R&C1
    // asset reader: addresses for most vertices are shifted seven entries,
    // then the final seven slots are patched from padding after the vertex
    // table. The positions themselves remain in the ordinary 0x10-byte rows.
    for (std::size_t i = 7u; i < out.explicitVertices.size(); ++i) {
        out.explicitVertices[i - 7u].cacheAddress = out.explicitVertices[i].cacheAddress;
    }

    const std::size_t serializedBlocks = (unknownE - vertexTableOffset) / 0x10u;
    if (serializedBlocks < vertexCount) {
        status = Rac1MobyStatus::InvalidVertexTable;
        return false;
    }
    const std::size_t epilogueCount = serializedBlocks - vertexCount;
    if (epilogueCount >= 7u) {
        status = Rac1MobyStatus::InvalidVertexTable;
        return false;
    }

    std::size_t epiloguePtr = verticesOffset + verticesBytes;
    const std::size_t firstEpilogue = vertexCount < 7u ? 7u - vertexCount : 0u;
    std::size_t skipBytes = 0u;
    if (!checkedMul(firstEpilogue, kMobyVertexBytes, skipBytes) ||
        !fits(epiloguePtr, skipBytes, core.size())) {
        status = Rac1MobyStatus::InvalidVertexTable;
        return false;
    }
    epiloguePtr += skipBytes;
    for (std::size_t i = firstEpilogue; i < epilogueCount; ++i) {
        if (!fits(epiloguePtr, 2u, core.size())) {
            status = Rac1MobyStatus::InvalidVertexTable;
            return false;
        }
        const std::ptrdiff_t dest = static_cast<std::ptrdiff_t>(vertexCount + i) - 7;
        if (dest >= 0 && static_cast<std::size_t>(dest) < out.explicitVertices.size()) {
            out.explicitVertices[static_cast<std::size_t>(dest)].cacheAddress =
                static_cast<std::uint16_t>(readU16(core, epiloguePtr) & 0x01ffu);
        }
        epiloguePtr += kMobyVertexBytes;
    }

    if (epiloguePtr < kMobyVertexBytes - 4u ||
        !fits(epiloguePtr - kMobyVertexBytes + 4u, 12u, core.size())) {
        status = Rac1MobyStatus::InvalidVertexTable;
        return false;
    }
    const std::size_t trailing = epiloguePtr - kMobyVertexBytes + 4u;
    const std::size_t trailingStart =
        (vertexCount + epilogueCount < 7u) ? 7u - vertexCount - epilogueCount : 0u;
    for (std::size_t i = trailingStart; i < 6u; ++i) {
        const std::ptrdiff_t dest = static_cast<std::ptrdiff_t>(vertexCount + epilogueCount + i) - 7;
        if (dest >= 0 && static_cast<std::size_t>(dest) < out.explicitVertices.size()) {
            out.explicitVertices[static_cast<std::size_t>(dest)].cacheAddress =
                static_cast<std::uint16_t>(readU16(core, trailing + i * 2u) & 0x01ffu);
        }
    }

    if (out.texcoords.size() < out.explicitVertices.size() + out.duplicateCacheAddresses.size()) {
        status = Rac1MobyStatus::InvalidPacket;
        return false;
    }
    return true;
}

struct LocalVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float u = 0.0f;
    float v = 0.0f;
};
struct LocalOutputVertex : LocalVertex {
    std::int32_t material = -1;
};
struct LocalBatch {
    std::uint32_t material = 0u;
    std::vector<LocalVertex> triangleVertices;
};
struct DecodedClass {
    bool renderable = false;
    bool intentionallyNonVisible = false;
    Rac1MobySkipReason skipReason = Rac1MobySkipReason::NoClassData;
    std::vector<LocalBatch> batches;
    // Post-async-trim triangles represented by the packet stream. Some can
    // intentionally carry the retail negative/special material, which is
    // discarded in the fragment shader rather than sampled as a texture.
    std::size_t sourceTriangleCount = 0u;
    std::size_t specialMaterialTriangleCount = 0u;
    std::size_t triangleCount = 0u;
};

bool decodeClass(std::span<const std::uint8_t> core,
                 const ClassEntry& entry,
                 std::uint32_t textureCount,
                 DecodedClass& decoded,
                 Rac1MobyStatus& status) {
    if (entry.offset == 0u) {
        decoded.renderable = false;
        decoded.intentionallyNonVisible = true;
        decoded.skipReason = Rac1MobySkipReason::NoClassData;
        return true;
    }
    const std::size_t base = static_cast<std::size_t>(entry.offset);
    if (!fits(base, kMobyClassHeaderBytes, core.size())) {
        status = Rac1MobyStatus::InvalidClass;
        return false;
    }

    const std::int32_t packetTableSigned = readI32(core, base + 0x0u);
    const std::uint8_t highLodCount = core[base + 0x4u];
    const std::uint8_t lowLodCount = core[base + 0x5u];
    const float classScale = readF32(core, base + 0x24u) * kVertexScale;

    // packetTableOffset == 0 is the explicit no-mesh representation used by
    // the R&C class format (noclip likewise leaves MobyClass.mesh null). If
    // packet counts contradict that pointer, do not silently bless the class.
    if (packetTableSigned == 0) {
        if (highLodCount != 0u || lowLodCount != 0u) {
            status = Rac1MobyStatus::InvalidClass;
            return false;
        }
        decoded.renderable = false;
        decoded.intentionallyNonVisible = true;
        decoded.skipReason = Rac1MobySkipReason::NoPacketTable;
        return true;
    }

    // A low-LOD-only mesh is not equivalent to a logic-only moby. Phase 9 only
    // decodes the high-LOD path, so such a referenced class must fail loudly
    // rather than disappearing from the native scene.
    if (highLodCount == 0u && lowLodCount != 0u) {
        status = Rac1MobyStatus::UnsupportedLowLodOnlyClass;
        return false;
    }
    if (highLodCount == 0u) {
        decoded.renderable = false;
        decoded.intentionallyNonVisible = true;
        decoded.skipReason = Rac1MobySkipReason::ZeroLodPacketCounts;
        return true;
    }
    if (packetTableSigned < 0 || !std::isfinite(classScale) || classScale == 0.0f) {
        status = Rac1MobyStatus::InvalidClass;
        return false;
    }

    const std::size_t packetTable = static_cast<std::size_t>(packetTableSigned);
    std::size_t packetHeadersBytes = 0u;
    std::size_t packetTableBase = 0u;
    if (!checkedAdd(base, packetTable, packetTableBase) ||
        !checkedMul(highLodCount, kMobyPacketHeaderBytes, packetHeadersBytes) ||
        !fits(packetTableBase, packetHeadersBytes, core.size())) {
        status = Rac1MobyStatus::InvalidClass;
        return false;
    }

    std::array<bool, kVertexCacheSize> cacheValid{};
    std::array<LocalVertex, kVertexCacheSize> vertexCache{};
    std::vector<LocalOutputVertex> outputVertices;

    // GS material state survives packet boundaries. A moby packet is allowed to
    // contain geometry without uploading another TEX0; in that case the retail
    // renderer keeps using the material selected by the previous packet. Resetting
    // this to texture 0 for every packet makes otherwise-correct models borrow the
    // first global moby texture (very visibly Ratchet fur on crates in level 0).
    std::int32_t currentMaterial = textureCount > 0u ? 0 : -1;

    for (std::size_t packetIndex = 0u; packetIndex < highLodCount; ++packetIndex) {
        PacketData packet{};
        if (!parsePacket(core,
                         base,
                         packetTableBase + packetIndex * kMobyPacketHeaderBytes,
                         packet,
                         status)) {
            return false;
        }

        std::vector<LocalVertex> realPacket;
        realPacket.reserve(packet.explicitVertices.size() + packet.duplicateCacheAddresses.size());
        std::size_t texcoordIndex = 0u;
        for (const RawVertex& source : packet.explicitVertices) {
            if (source.cacheAddress >= kVertexCacheSize || texcoordIndex >= packet.texcoords.size()) {
                status = Rac1MobyStatus::InvalidVertexCache;
                return false;
            }
            const auto st = packet.texcoords[texcoordIndex++];
            LocalVertex vertex{
                source.x * classScale,
                source.y * classScale,
                source.z * classScale,
                st[0] * kTexcoordScale,
                st[1] * kTexcoordScale,
            };
            realPacket.push_back(vertex);
            vertexCache[source.cacheAddress] = vertex;
            cacheValid[source.cacheAddress] = true;
        }
        for (std::uint16_t cacheAddress : packet.duplicateCacheAddresses) {
            if (cacheAddress >= kVertexCacheSize || !cacheValid[cacheAddress] ||
                texcoordIndex >= packet.texcoords.size()) {
                status = Rac1MobyStatus::InvalidVertexCache;
                return false;
            }
            LocalVertex vertex = vertexCache[cacheAddress];
            const auto st = packet.texcoords[texcoordIndex++];
            vertex.u = st[0] * kTexcoordScale;
            vertex.v = st[1] * kTexcoordScale;
            realPacket.push_back(vertex);
        }

        std::array<const LocalVertex*, 3> tri{nullptr, nullptr, nullptr};
        std::size_t materialChangeIndex = 0u;
        for (std::int8_t encodedIndex : packet.indices) {
            int index = static_cast<int>(encodedIndex);
            if (index == 0) {
                if (materialChangeIndex >= packet.secretIndices.size()) {
                    status = Rac1MobyStatus::InvalidIndexStream;
                    return false;
                }
                const int secretIndex = static_cast<int>(packet.secretIndices[materialChangeIndex]);
                if (secretIndex == 0) {
                    // The game has three asynchronous transformed vertices in
                    // flight. On early termination it discards those three
                    // triangles (nine output vertices).
                    const std::size_t discard = std::min<std::size_t>(9u, outputVertices.size());
                    outputVertices.resize(outputVertices.size() - discard);
                    break;
                }
                index = secretIndex - 0x80;
                if (materialChangeIndex >= packet.localMaterials.size()) {
                    status = Rac1MobyStatus::InvalidMaterial;
                    return false;
                }
                const std::int32_t localMaterial = packet.localMaterials[materialChangeIndex];
                if (localMaterial < 0) {
                    currentMaterial = -1; // special material, reference path discards it
                } else if (static_cast<std::size_t>(localMaterial) >= entry.textures.size()) {
                    status = Rac1MobyStatus::InvalidMaterial;
                    return false;
                } else {
                    const std::uint32_t global = entry.textures[static_cast<std::size_t>(localMaterial)];
                    if (global >= textureCount) {
                        status = Rac1MobyStatus::InvalidMaterial;
                        return false;
                    }
                    currentMaterial = static_cast<std::int32_t>(global);
                }
                ++materialChangeIndex;
            }

            const int realIndex = (index > 0 ? index : index + 0x80) - 1;
            if (realIndex < 0 || static_cast<std::size_t>(realIndex) >= realPacket.size()) {
                status = Rac1MobyStatus::InvalidIndexStream;
                return false;
            }
            tri[0] = tri[1];
            tri[1] = tri[2];
            tri[2] = &realPacket[static_cast<std::size_t>(realIndex)];
            if (index > 0) {
                if (tri[0] == nullptr || tri[1] == nullptr || tri[2] == nullptr) {
                    status = Rac1MobyStatus::InvalidIndexStream;
                    return false;
                }
                for (const LocalVertex* vertex : tri) {
                    LocalOutputVertex output{};
                    static_cast<LocalVertex&>(output) = *vertex;
                    output.material = currentMaterial;
                    outputVertices.push_back(output);
                }
            }
        }
    }

    if ((outputVertices.size() % 3u) != 0u) {
        status = Rac1MobyStatus::InvalidIndexStream;
        return false;
    }

    decoded.sourceTriangleCount = outputVertices.size() / 3u;
    std::unordered_map<std::uint32_t, std::size_t> batchByMaterial;
    for (std::size_t i = 0u; i < outputVertices.size(); i += 3u) {
        const std::int32_t material = outputVertices[i].material;
        if (outputVertices[i + 1u].material != material ||
            outputVertices[i + 2u].material != material) {
            status = Rac1MobyStatus::InvalidMaterial;
            return false;
        }

        if (material < 0) {
            // This is not missing geometry. The retail/native reference path
            // preserves these vertices with textureIndex=-1 and its fragment
            // shader explicitly discards them. They are used by water, triggers
            // and a few other special moby surfaces. Account for them exactly
            // instead of pretending the class failed to render.
            ++decoded.specialMaterialTriangleCount;
            continue;
        }

        const std::uint32_t globalMaterial = static_cast<std::uint32_t>(material);
        auto [it, inserted] = batchByMaterial.emplace(globalMaterial, decoded.batches.size());
        if (inserted) decoded.batches.push_back({globalMaterial, {}});
        auto& batch = decoded.batches[it->second].triangleVertices;
        for (std::size_t j = 0u; j < 3u; ++j) {
            const LocalOutputVertex& vertex = outputVertices[i + j];
            batch.push_back({vertex.x, vertex.y, vertex.z, vertex.u, vertex.v});
        }
        ++decoded.triangleCount;
    }

    if (decoded.triangleCount + decoded.specialMaterialTriangleCount !=
        decoded.sourceTriangleCount) {
        status = Rac1MobyStatus::UnaccountedNonRenderableClass;
        return false;
    }

    decoded.renderable = decoded.triangleCount > 0u;
    if (decoded.renderable) return true;

    if (decoded.sourceTriangleCount > 0u &&
        decoded.specialMaterialTriangleCount == decoded.sourceTriangleCount) {
        // A real mesh can be intentionally 100% invisible: every surviving
        // triangle uses the negative/special material and is discarded by the
        // retail fragment path. This is an accounted render semantic, not a
        // decoder failure.
        decoded.intentionallyNonVisible = true;
        decoded.skipReason = Rac1MobySkipReason::SpecialMaterialDiscard;
        return true;
    }

    // A class that advertises high-LOD packets but produces neither visible nor
    // explicitly shader-discarded geometry is still unexplained and must fail.
    status = Rac1MobyStatus::UnaccountedNonRenderableClass;
    return false;
}

struct Quaternion {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    float w = 1.0f;
};

Quaternion quaternionFromEulerZyx(float x, float y, float z) noexcept {
    // gl-matrix quat.fromEuler's default order is intrinsic ZYX. The source
    // gameplay angles are already radians; the reference renderer converts
    // them to degrees only because quat.fromEuler's API accepts degrees.
    const float sx = std::sin(x * 0.5f), cx = std::cos(x * 0.5f);
    const float sy = std::sin(y * 0.5f), cy = std::cos(y * 0.5f);
    const float sz = std::sin(z * 0.5f), cz = std::cos(z * 0.5f);
    return {
        sx * cy * cz - cx * sy * sz,
        cx * sy * cz + sx * cy * sz,
        cx * cy * sz - sx * sy * cz,
        cx * cy * cz + sx * sy * sz,
    };
}

std::array<float, 3> rotate(const Quaternion& q, float x, float y, float z) noexcept {
    // Fast quaternion-vector rotation: v' = v + w*(2 q×v) + q×(2 q×v).
    const float tx = 2.0f * (q.y * z - q.z * y);
    const float ty = 2.0f * (q.z * x - q.x * z);
    const float tz = 2.0f * (q.x * y - q.y * x);
    return {
        x + q.w * tx + (q.y * tz - q.z * ty),
        y + q.w * ty + (q.z * tx - q.x * tz),
        z + q.w * tz + (q.x * ty - q.y * tx),
    };
}

std::size_t batchFor(Rac1MobySceneMesh& mesh, std::uint32_t material) {
    for (std::size_t i = 0u; i < mesh.batches.size(); ++i) {
        if (mesh.batches[i].materialIndex == material) return i;
    }
    mesh.batches.push_back({material, {}});
    return mesh.batches.size() - 1u;
}

void appendInstance(Rac1MobySceneMesh& mesh,
                    const DecodedClass& decoded,
                    const Instance& instance) {
    const Quaternion q = quaternionFromEulerZyx(
        instance.rotation[0], instance.rotation[1], instance.rotation[2]);
    for (const LocalBatch& sourceBatch : decoded.batches) {
        auto& destination = mesh.batches[batchFor(mesh, sourceBatch.material)].triangleVertices;
        destination.reserve(destination.size() + sourceBatch.triangleVertices.size());
        for (const LocalVertex& source : sourceBatch.triangleVertices) {
            const float sx = source.x * instance.scale;
            const float sy = source.y * instance.scale;
            const float sz = source.z * instance.scale;
            const auto rotated = rotate(q, sx, sy, sz);
            // The gameplay colour is an ambient-light input, not a standalone
            // texture tint. The retail moby shader combines it with the packed
            // vertex normal and the instance's directional-light indices before
            // modulating the texture. Phase 9 does not carry that lighting data
            // through the host mesh yet, so applying ambient alone produces a
            // severe false colour cast (retail level 0 appears almost solid red).
            // Render the bind-pose model neutrally until native moby lighting is
            // implemented rather than displaying a knowingly incorrect partial
            // lighting result.
            destination.push_back({
                rotated[0] + instance.position[0],
                rotated[1] + instance.position[1],
                rotated[2] + instance.position[2],
                source.u,
                source.v,
                255u,
                255u,
                255u,
                255u,
            });
        }
    }
    mesh.triangleCount += decoded.triangleCount;
}

Rac1MobyResult fail(Rac1MobyStatus status, Rac1MobySceneMesh mesh = {}) {
    return {status, std::move(mesh)};
}

} // namespace

const char* rac1MobyStatusName(Rac1MobyStatus status) noexcept {
    switch (status) {
    case Rac1MobyStatus::Ok: return "ok";
    case Rac1MobyStatus::InvalidIndexTable: return "invalid-index-table";
    case Rac1MobyStatus::InvalidGameplayHeader: return "invalid-gameplay-header";
    case Rac1MobyStatus::InvalidInstanceBlock: return "invalid-instance-block";
    case Rac1MobyStatus::InvalidClass: return "invalid-class";
    case Rac1MobyStatus::InvalidPacket: return "invalid-packet";
    case Rac1MobyStatus::InvalidVifLayout: return "invalid-vif-layout";
    case Rac1MobyStatus::InvalidVertexTable: return "invalid-vertex-table";
    case Rac1MobyStatus::InvalidVertexCache: return "invalid-vertex-cache";
    case Rac1MobyStatus::InvalidIndexStream: return "invalid-index-stream";
    case Rac1MobyStatus::InvalidMaterial: return "invalid-material";
    case Rac1MobyStatus::MissingReferencedClass: return "missing-referenced-class";
    case Rac1MobyStatus::UnsupportedLowLodOnlyClass: return "unsupported-low-lod-only-class";
    case Rac1MobyStatus::UnaccountedNonRenderableClass: return "unaccounted-non-renderable-class";
    case Rac1MobyStatus::UnaccountedInstance: return "unaccounted-instance";
    case Rac1MobyStatus::EmptyScene: return "empty-scene";
    }
    return "unknown";
}

const char* rac1MobySkipReasonName(Rac1MobySkipReason reason) noexcept {
    switch (reason) {
    case Rac1MobySkipReason::NoClassData: return "no-class-data";
    case Rac1MobySkipReason::NoPacketTable: return "no-packet-table";
    case Rac1MobySkipReason::ZeroLodPacketCounts: return "zero-lod-packet-counts";
    case Rac1MobySkipReason::SpecialMaterialDiscard: return "special-material-discard";
    }
    return "unknown";
}

Rac1MobyResult decodeRac1MobyScene(
    std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,
    std::span<const std::uint8_t> gameplay,
    Rac1ArrayRange mobyClasses,
    std::uint32_t mobyTextureCount) {
    Rac1MobySceneMesh mesh{};
    mesh.classCount = mobyClasses.count;

    std::unordered_map<std::int32_t, ClassEntry> classEntries;
    if (!parseClassEntries(coreIndex, mobyClasses, classEntries)) {
        return fail(Rac1MobyStatus::InvalidIndexTable);
    }
    if (gameplay.size() < kGameplayHeaderBytes) {
        return fail(Rac1MobyStatus::InvalidGameplayHeader);
    }

    std::vector<Instance> instances;
    if (!parseInstances(gameplay, instances)) {
        return fail(Rac1MobyStatus::InvalidInstanceBlock);
    }
    mesh.instanceCount = instances.size();

    // Decode classes lazily when the gameplay instance list actually references
    // them. Retail level indexes can contain classes that are not instantiated
    // in the current level state; those blobs are irrelevant to this scene and
    // must not make otherwise-valid gameplay fail just because a dormant class
    // uses a packet variant we have not needed yet.
    std::unordered_map<std::int32_t, DecodedClass> decodedClasses;
    decodedClasses.reserve(std::min(classEntries.size(), instances.size()));
    Rac1MobyStatus detailedStatus = Rac1MobyStatus::Ok;

    for (const Instance& instance : instances) {
        auto decodedIt = decodedClasses.find(instance.oClass);
        if (decodedIt == decodedClasses.end()) {
            const auto classIt = classEntries.find(instance.oClass);
            if (classIt == classEntries.end()) {
                ++mesh.missingClassInstanceCount;
                return fail(Rac1MobyStatus::MissingReferencedClass, std::move(mesh));
            }

            DecodedClass decoded{};
            if (!decodeClass(core, classIt->second, mobyTextureCount, decoded, detailedStatus)) {
                return fail(detailedStatus, std::move(mesh));
            }
            if (decoded.renderable) ++mesh.renderableClassCount;
            decodedIt = decodedClasses.emplace(instance.oClass, std::move(decoded)).first;
        }

        if (!decodedIt->second.renderable) {
            if (!decodedIt->second.intentionallyNonVisible) {
                ++mesh.unaccountedInstanceCount;
                return fail(Rac1MobyStatus::UnaccountedInstance, std::move(mesh));
            }
            ++mesh.intentionallyNonVisibleInstanceCount;
            ++mesh.skippedInstanceCount;
            mesh.sourceTriangleCount += decodedIt->second.sourceTriangleCount;
            mesh.specialMaterialTriangleCount += decodedIt->second.specialMaterialTriangleCount;
            auto skipIt = std::find_if(
                mesh.skippedClasses.begin(), mesh.skippedClasses.end(),
                [&](const Rac1MobySkippedClass& skipped) {
                    return skipped.oClass == instance.oClass &&
                           skipped.reason == decodedIt->second.skipReason;
                });
            if (skipIt == mesh.skippedClasses.end()) {
                mesh.skippedClasses.push_back({
                    instance.oClass,
                    decodedIt->second.skipReason,
                    1u,
                    decodedIt->second.sourceTriangleCount,
                    decodedIt->second.specialMaterialTriangleCount,
                });
            } else {
                ++skipIt->instanceCount;
                skipIt->sourceTriangleCount += decodedIt->second.sourceTriangleCount;
                skipIt->specialMaterialTriangleCount +=
                    decodedIt->second.specialMaterialTriangleCount;
            }
            continue;
        }
        appendInstance(mesh, decodedIt->second, instance);
        mesh.sourceTriangleCount += decodedIt->second.sourceTriangleCount;
        mesh.specialMaterialTriangleCount += decodedIt->second.specialMaterialTriangleCount;
        ++mesh.renderedInstanceCount;
    }

    const std::size_t accounted =
        mesh.renderedInstanceCount + mesh.intentionallyNonVisibleInstanceCount;
    const bool trianglesAccounted =
        mesh.triangleCount + mesh.specialMaterialTriangleCount == mesh.sourceTriangleCount;
    if (accounted != mesh.instanceCount ||
        mesh.skippedInstanceCount != mesh.intentionallyNonVisibleInstanceCount ||
        !trianglesAccounted || mesh.missingClassInstanceCount != 0u ||
        mesh.unaccountedInstanceCount != 0u) {
        mesh.unaccountedInstanceCount =
            mesh.instanceCount > accounted ? mesh.instanceCount - accounted : 0u;
        return fail(Rac1MobyStatus::UnaccountedInstance, std::move(mesh));
    }
    if (mesh.instanceCount > 0u && accounted == 0u) {
        return fail(Rac1MobyStatus::EmptyScene, std::move(mesh));
    }
    return {Rac1MobyStatus::Ok, std::move(mesh)};
}

} // namespace ratchet::assets
