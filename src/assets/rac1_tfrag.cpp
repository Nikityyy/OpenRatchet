#include "assets/rac1_tfrag.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <unordered_map>
#include <utility>
#include <vector>

namespace ratchet::assets {
namespace {

constexpr std::size_t kTfragBlockHeaderBytes = 0x10u;
constexpr std::size_t kTfragHeaderBytes = 0x40u;
constexpr std::size_t kTfragVuHeaderBytes = 0x28u;
constexpr std::size_t kTfragTexturePrimitiveBytes = 0x50u;
constexpr std::size_t kTfragVertexInfoBytes = 0x8u;
constexpr std::size_t kTfragStripBytes = 0x4u;
constexpr std::size_t kMaxTfragCount = 1u << 20u;

constexpr std::uint8_t kVifNop = 0x00u;
constexpr std::uint8_t kVifStcycl = 0x01u;
constexpr std::uint8_t kVifStmask = 0x20u;
constexpr std::uint8_t kVifStrow = 0x30u;
constexpr std::uint8_t kVifStcol = 0x31u;
constexpr std::uint8_t kVifMpg = 0x4au;
constexpr std::uint8_t kVifDirect = 0x50u;
constexpr std::uint8_t kVifDirectHl = 0x51u;
constexpr std::uint8_t kVifUnpackMask = 0x60u;

constexpr std::uint8_t kV4_8 = 0x0eu;
constexpr std::uint8_t kV4_16 = 0x0du;
constexpr std::uint8_t kV3_16 = 0x09u;

std::uint16_t readU16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
}

std::int16_t readI16(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::int16_t>(readU16(bytes, offset));
}

std::uint32_t readU32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

std::int32_t readI32(std::span<const std::uint8_t> bytes, std::size_t offset) noexcept {
    return static_cast<std::int32_t>(readU32(bytes, offset));
}

bool fits(std::size_t offset, std::size_t size, std::size_t capacity) noexcept {
    return offset <= capacity && size <= capacity - offset;
}

bool checkedAdd(std::size_t a, std::size_t b, std::size_t& out) noexcept {
    if (a > std::numeric_limits<std::size_t>::max() - b) {
        return false;
    }
    out = a + b;
    return true;
}

bool checkedMul(std::size_t a, std::size_t b, std::size_t& out) noexcept {
    if (b != 0u && a > std::numeric_limits<std::size_t>::max() / b) {
        return false;
    }
    out = a * b;
    return true;
}

struct VifCommand {
    std::uint8_t cmd = 0u;
    std::uint8_t vnvl = 0u;
    std::uint16_t immediate = 0u;
    std::uint16_t addr = 0u;
    std::uint16_t num = 0u;
    std::size_t offset = 0u;
    std::size_t size = 0u;
    std::size_t logicalPayloadBytes = 0u;

    [[nodiscard]] bool isUnpack() const noexcept {
        return (cmd & kVifUnpackMask) == kVifUnpackMask;
    }
};

struct VifParseResult {
    Rac1TfragStatus status = Rac1TfragStatus::InvalidVifLayout;
    std::vector<VifCommand> commands;

    [[nodiscard]] bool ok() const noexcept {
        return status == Rac1TfragStatus::Ok;
    }
};

bool isOneWordVifCommand(std::uint8_t cmd) noexcept {
    switch (cmd) {
    case kVifNop:
    case kVifStcycl:
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

VifParseResult parseVif(std::span<const std::uint8_t> buffer) {
    if ((buffer.size() & 3u) != 0u) {
        return {Rac1TfragStatus::InvalidVifLayout, {}};
    }

    std::uint16_t wl = 1u;
    std::uint16_t cl = 1u;
    std::size_t offset = 0u;
    std::vector<VifCommand> out;

    while (offset < buffer.size()) {
        if (!fits(offset, 4u, buffer.size())) {
            return {Rac1TfragStatus::InvalidVifLayout, {}};
        }
        const std::uint32_t word = readU32(buffer, offset);
        VifCommand command{};
        command.offset = offset;
        command.immediate = static_cast<std::uint16_t>(word & 0xffffu);
        command.num = static_cast<std::uint16_t>((word >> 16u) & 0xffu);
        if (command.num == 0u) {
            command.num = 256u;
        }
        command.cmd = static_cast<std::uint8_t>((word >> 24u) & 0x7fu);
        command.vnvl = static_cast<std::uint8_t>(command.cmd & 0x0fu);
        command.addr = static_cast<std::uint16_t>(command.immediate & 0x03ffu);

        std::size_t words = 0u;
        if (isOneWordVifCommand(command.cmd)) {
            words = 1u;
        } else if (command.cmd == kVifStmask) {
            words = 2u;
        } else if (command.cmd == kVifStrow || command.cmd == kVifStcol) {
            words = 5u;
            command.logicalPayloadBytes = 16u;
        } else if (command.cmd == kVifMpg) {
            words = 1u + static_cast<std::size_t>(command.num) * 2u;
            command.logicalPayloadBytes = static_cast<std::size_t>(command.num) * 8u;
        } else if (command.cmd == kVifDirect || command.cmd == kVifDirectHl) {
            const std::size_t qwords = command.immediate == 0u ? 65536u : command.immediate;
            if (!checkedMul(qwords, 4u, words)) {
                return {Rac1TfragStatus::InvalidVifLayout, {}};
            }
            words += 1u;
            command.logicalPayloadBytes = qwords * 16u;
        } else if (command.isUnpack()) {
            // VIF UNPACK command encoding: VN selects 1..4 components and VL
            // selects 32/16/8/4 bits per component. R&C1 tfrags use ordinary
            // writes (WL <= CL), matching the original asset readers.
            if (wl > cl) {
                return {Rac1TfragStatus::UnsupportedVifCycle, {}};
            }
            const std::size_t vn = static_cast<std::size_t>((command.cmd & 0x0cu) >> 2u);
            const std::size_t vl = static_cast<std::size_t>(command.cmd & 0x03u);
            const std::size_t componentCount = vn + 1u;
            const std::size_t componentBits = 32u >> vl;
            std::size_t totalBits = 0u;
            if (!checkedMul(componentBits, componentCount, totalBits) ||
                !checkedMul(totalBits, command.num, totalBits)) {
                return {Rac1TfragStatus::InvalidVifLayout, {}};
            }
            command.logicalPayloadBytes = (totalBits + 7u) / 8u;
            const std::size_t payloadWords = (totalBits + 31u) / 32u;
            words = 1u + payloadWords;
        } else {
            return {Rac1TfragStatus::InvalidVifLayout, {}};
        }

        std::size_t commandBytes = 0u;
        if (!checkedMul(words, 4u, commandBytes) ||
            !fits(offset, commandBytes, buffer.size())) {
            return {Rac1TfragStatus::InvalidVifLayout, {}};
        }
        command.size = commandBytes;
        out.push_back(command);

        if (command.cmd == kVifStcycl) {
            cl = static_cast<std::uint16_t>(command.immediate & 0xffu);
            wl = static_cast<std::uint16_t>((command.immediate >> 8u) & 0xffu);
            if (cl == 0u) cl = 256u;
            if (wl == 0u) wl = 256u;
        }

        offset += commandBytes;
    }

    return {Rac1TfragStatus::Ok, std::move(out)};
}

std::span<const std::uint8_t> commandPayload(std::span<const std::uint8_t> buffer,
                                             const VifCommand& command) noexcept {
    if (command.size < 4u || !fits(command.offset + 4u, command.size - 4u, buffer.size())) {
        return {};
    }
    return buffer.subspan(command.offset + 4u, command.size - 4u);
}

std::vector<const VifCommand*> unpackCommands(const std::vector<VifCommand>& commands) {
    std::vector<const VifCommand*> out;
    for (const auto& command : commands) {
        if (command.isUnpack()) {
            out.push_back(&command);
        }
    }
    return out;
}

struct VertexInfo {
    std::int16_t s = 0;
    std::int16_t t = 0;
    std::int16_t parent = 0;
    std::int16_t vertex = 0;
};

struct PositionI16 {
    std::int16_t x = 0;
    std::int16_t y = 0;
    std::int16_t z = 0;
};

struct Strip {
    std::uint8_t vertexCount = 0u;
    bool hasMaterial = false;
    std::uint8_t endFlag = 0u;
    std::int8_t materialOffset = -1;
};

struct TexturePrimitive {
    std::int32_t materialIndex = -1;
};

struct VuHeader {
    std::uint16_t positionsCommonCount = 0u;
    std::uint16_t positionsLod01Count = 0u;
    std::uint16_t positionsLod0Count = 0u;
};

bool parseVertexInfos(std::span<const std::uint8_t> payload,
                      std::size_t logicalBytes,
                      std::vector<VertexInfo>& out) {
    if (logicalBytes > payload.size() || (logicalBytes % kTfragVertexInfoBytes) != 0u) {
        return false;
    }
    for (std::size_t offset = 0u; offset < logicalBytes; offset += kTfragVertexInfoBytes) {
        out.push_back({readI16(payload, offset + 0u),
                       readI16(payload, offset + 2u),
                       readI16(payload, offset + 4u),
                       readI16(payload, offset + 6u)});
    }
    return true;
}

bool parsePositions(std::span<const std::uint8_t> payload,
                    std::size_t count,
                    std::vector<PositionI16>& out) {
    std::size_t bytes = 0u;
    if (!checkedMul(count, 6u, bytes) || bytes > payload.size()) {
        return false;
    }
    for (std::size_t offset = 0u; offset < bytes; offset += 6u) {
        out.push_back({readI16(payload, offset + 0u),
                       readI16(payload, offset + 2u),
                       readI16(payload, offset + 4u)});
    }
    return true;
}

bool parseStrips(std::span<const std::uint8_t> payload,
                 std::size_t logicalBytes,
                 std::vector<Strip>& out) {
    if (logicalBytes > payload.size() || (logicalBytes % kTfragStripBytes) != 0u) {
        return false;
    }
    for (std::size_t offset = 0u; offset < logicalBytes; offset += kTfragStripBytes) {
        const std::uint8_t first = payload[offset + 0u];
        out.push_back({static_cast<std::uint8_t>(first & 0x7fu),
                       (first & 0x80u) != 0u,
                       payload[offset + 1u],
                       static_cast<std::int8_t>(payload[offset + 2u])});
    }
    return true;
}

bool parseIndices(std::span<const std::uint8_t> payload,
                  std::size_t logicalBytes,
                  std::vector<std::uint8_t>& out) {
    if (logicalBytes > payload.size()) {
        return false;
    }
    out.insert(out.end(), payload.begin(), payload.begin() + static_cast<std::ptrdiff_t>(logicalBytes));
    return true;
}

bool updateBounds(Rac1TfragBounds& bounds, bool& hasBounds, const Rac1TfragVertex& v) noexcept {
    if (!std::isfinite(v.x) || !std::isfinite(v.y) || !std::isfinite(v.z)) {
        return false;
    }
    if (!hasBounds) {
        bounds.minX = bounds.maxX = v.x;
        bounds.minY = bounds.maxY = v.y;
        bounds.minZ = bounds.maxZ = v.z;
        hasBounds = true;
        return true;
    }
    bounds.minX = std::min(bounds.minX, v.x);
    bounds.minY = std::min(bounds.minY, v.y);
    bounds.minZ = std::min(bounds.minZ, v.z);
    bounds.maxX = std::max(bounds.maxX, v.x);
    bounds.maxY = std::max(bounds.maxY, v.y);
    bounds.maxZ = std::max(bounds.maxZ, v.z);
    return true;
}

std::uint8_t expandVertexColor(std::uint8_t value) noexcept {
    return static_cast<std::uint8_t>(std::min<unsigned>(255u, static_cast<unsigned>(value) * 2u));
}

Rac1TfragResult fail(Rac1TfragStatus status, Rac1TfragMesh mesh = {}) {
    return {status, std::move(mesh)};
}

} // namespace

const char* rac1TfragStatusName(Rac1TfragStatus status) noexcept {
    switch (status) {
    case Rac1TfragStatus::Ok: return "ok";
    case Rac1TfragStatus::OffsetOutOfRange: return "offset-out-of-range";
    case Rac1TfragStatus::InvalidBlockHeader: return "invalid-block-header";
    case Rac1TfragStatus::InvalidTfragHeader: return "invalid-tfrag-header";
    case Rac1TfragStatus::InvalidVifLayout: return "invalid-vif-layout";
    case Rac1TfragStatus::UnsupportedVifCycle: return "unsupported-vif-cycle";
    case Rac1TfragStatus::MissingVifData: return "missing-vif-data";
    case Rac1TfragStatus::InvalidVuHeader: return "invalid-vu-header";
    case Rac1TfragStatus::InvalidStrip: return "invalid-strip";
    case Rac1TfragStatus::InvalidVertexIndex: return "invalid-vertex-index";
    case Rac1TfragStatus::InvalidMaterialIndex: return "invalid-material-index";
    case Rac1TfragStatus::EmptyMesh: return "empty-mesh";
    }
    return "unknown";
}

Rac1TfragResult decodeRac1TfragTerrain(std::span<const std::uint8_t> core,
                                       std::uint32_t tfragsOffset,
                                       std::uint32_t textureCount) {
    const std::size_t blockBase = static_cast<std::size_t>(tfragsOffset);
    if (!fits(blockBase, kTfragBlockHeaderBytes, core.size())) {
        return fail(Rac1TfragStatus::OffsetOutOfRange);
    }

    const std::span<const std::uint8_t> block = core.subspan(blockBase);
    const std::int32_t tableOffsetSigned = readI32(block, 0x0u);
    const std::int32_t tfragCountSigned = readI32(block, 0x4u);
    if (tableOffsetSigned < 0 || tfragCountSigned < 0 ||
        static_cast<std::size_t>(tfragCountSigned) > kMaxTfragCount) {
        return fail(Rac1TfragStatus::InvalidBlockHeader);
    }

    const std::size_t tableOffset = static_cast<std::size_t>(tableOffsetSigned);
    const std::size_t tfragCount = static_cast<std::size_t>(tfragCountSigned);
    std::size_t headersBytes = 0u;
    if (!checkedMul(tfragCount, kTfragHeaderBytes, headersBytes) ||
        !fits(tableOffset, headersBytes, block.size())) {
        return fail(Rac1TfragStatus::InvalidBlockHeader);
    }

    Rac1TfragMesh mesh{};
    mesh.tfragCount = tfragCount;
    bool hasBounds = false;
    std::unordered_map<std::uint32_t, std::size_t> batchByMaterial;

    for (std::size_t tfragIndex = 0u; tfragIndex < tfragCount; ++tfragIndex) {
        const std::size_t headerOffset = tableOffset + tfragIndex * kTfragHeaderBytes;
        const std::span<const std::uint8_t> header = block.subspan(headerOffset, kTfragHeaderBytes);
        const std::int32_t dataOffsetSigned = readI32(header, 0x10u);
        if (dataOffsetSigned < 0) {
            return fail(Rac1TfragStatus::InvalidTfragHeader, std::move(mesh));
        }
        std::size_t dataBase = 0u;
        if (!checkedAdd(tableOffset, static_cast<std::size_t>(dataOffsetSigned), dataBase) ||
            dataBase >= block.size()) {
            return fail(Rac1TfragStatus::InvalidTfragHeader, std::move(mesh));
        }
        const std::span<const std::uint8_t> data = block.subspan(dataBase);

        const std::uint16_t lod2Offset = readU16(header, 0x14u);
        const std::uint16_t sharedOffset = readU16(header, 0x16u);
        const std::uint16_t lod0Offset = readU16(header, 0x1au);
        const std::uint16_t rgbaOffset = readU16(header, 0x1eu);
        const std::uint8_t commonSize = header[0x20u];
        const std::uint8_t lod1Size = header[0x22u];
        const std::uint8_t lod0Size = header[0x23u];
        const std::uint8_t texturePrimitiveCount = header[0x28u];
        const std::uint8_t rgbaSize = header[0x29u];

        std::array<std::size_t, 6> pointers{};
        pointers[0] = lod2Offset;
        pointers[1] = sharedOffset;
        pointers[2] = static_cast<std::size_t>(sharedOffset) + static_cast<std::size_t>(commonSize) * 0x10u;
        pointers[3] = lod0Offset;
        pointers[4] = static_cast<std::size_t>(sharedOffset) + static_cast<std::size_t>(lod1Size) * 0x10u;
        pointers[5] = static_cast<std::size_t>(lod0Offset) + static_cast<std::size_t>(lod0Size) * 0x10u;
        for (std::size_t i = 0u; i + 1u < pointers.size(); ++i) {
            if (pointers[i + 1u] <= pointers[i] || pointers[i + 1u] > data.size()) {
                return fail(Rac1TfragStatus::InvalidTfragHeader, std::move(mesh));
            }
        }

        std::array<std::span<const std::uint8_t>, 5> buffers{};
        for (std::size_t i = 0u; i < buffers.size(); ++i) {
            buffers[i] = data.subspan(pointers[i], pointers[i + 1u] - pointers[i]);
        }

        std::array<VifParseResult, 5> parsed{};
        for (std::size_t i = 0u; i < parsed.size(); ++i) {
            parsed[i] = parseVif(buffers[i]);
            if (!parsed[i].ok()) {
                return fail(parsed[i].status, std::move(mesh));
            }
        }

        // Group 2 provides the base position, VU layout, material primitives,
        // common vertex infos and common positions. There are multiple STROW
        // commands in this packet. The first one is a VU vertex-info helper
        // row whose first two words are 0x45000000; it is *not* a world-space
        // origin. The retail/Wrench command layout stores the tfrag base
        // position specifically in command slot 5. Selecting the first STROW
        // collapses every tfrag around ~1,130,496 world units.
        if (parsed[1].commands.size() <= 5u || parsed[1].commands[5u].cmd != kVifStrow) {
            return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
        }
        std::array<std::int32_t, 4> basePosition{};
        const auto basePayload = commandPayload(buffers[1], parsed[1].commands[5u]);
        if (basePayload.size() < 16u) {
            return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
        }
        for (std::size_t j = 0u; j < 4u; ++j) {
            basePosition[j] = readI32(basePayload, j * 4u);
        }

        const auto group2Unpacks = unpackCommands(parsed[1].commands);
        if (group2Unpacks.size() < 4u) {
            return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
        }
        const auto vuPayload = commandPayload(buffers[1], *group2Unpacks[0]);
        if (group2Unpacks[0]->logicalPayloadBytes < kTfragVuHeaderBytes ||
            vuPayload.size() < kTfragVuHeaderBytes) {
            return fail(Rac1TfragStatus::InvalidVuHeader, std::move(mesh));
        }
        VuHeader vu{};
        vu.positionsCommonCount = readU16(vuPayload, 0x0u);
        vu.positionsLod01Count = readU16(vuPayload, 0x4u);
        vu.positionsLod0Count = readU16(vuPayload, 0x8u);

        const auto texturePayload = commandPayload(buffers[1], *group2Unpacks[1]);
        std::size_t texturePrimitiveBytes = 0u;
        if (!checkedMul(texturePrimitiveCount, kTfragTexturePrimitiveBytes, texturePrimitiveBytes) ||
            texturePrimitiveBytes > group2Unpacks[1]->logicalPayloadBytes ||
            texturePrimitiveBytes > texturePayload.size()) {
            return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
        }
        std::vector<TexturePrimitive> texturePrimitives;
        texturePrimitives.reserve(texturePrimitiveCount);
        for (std::size_t i = 0u; i < texturePrimitiveCount; ++i) {
            texturePrimitives.push_back({readI32(texturePayload, i * kTfragTexturePrimitiveBytes)});
        }

        std::vector<VertexInfo> infos;
        const auto info1Payload = commandPayload(buffers[1], *group2Unpacks[2]);
        if (!parseVertexInfos(info1Payload, group2Unpacks[2]->logicalPayloadBytes, infos)) {
            return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
        }
        std::vector<PositionI16> positions;
        const auto pos1Payload = commandPayload(buffers[1], *group2Unpacks[3]);
        if (!parsePositions(pos1Payload, vu.positionsCommonCount, positions)) {
            return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
        }

        // Group 4 appends LOD0/1 vertex metadata and positions after two
        // optional V4_8 bookkeeping unpacks.
        const auto group4Unpacks = unpackCommands(parsed[3].commands);
        std::size_t u4 = 0u;
        if (vu.positionsLod01Count > 0u) {
            if (u4 >= group4Unpacks.size() || group4Unpacks[u4]->vnvl != kV4_8) {
                return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
            }
            ++u4;
        }
        if (u4 < group4Unpacks.size() && group4Unpacks[u4]->vnvl == kV4_8 &&
            group4Unpacks[u4]->addr != 0u) {
            ++u4;
        }
        if (u4 < group4Unpacks.size() && group4Unpacks[u4]->vnvl == kV4_16) {
            const auto payload = commandPayload(buffers[3], *group4Unpacks[u4]);
            if (!parseVertexInfos(payload, group4Unpacks[u4]->logicalPayloadBytes, infos)) {
                return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
            }
            ++u4;
        }
        if (u4 < group4Unpacks.size() && group4Unpacks[u4]->vnvl == kV3_16) {
            const auto payload = commandPayload(buffers[3], *group4Unpacks[u4]);
            if (!parsePositions(payload, vu.positionsLod01Count, positions)) {
                return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
            }
            ++u4;
        } else if (vu.positionsLod01Count > 0u) {
            return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
        }

        // Group 5 is the LOD0 packet: optional extra positions, strips,
        // indices, optional bookkeeping, then optional extra vertex infos.
        const auto group5Unpacks = unpackCommands(parsed[4].commands);
        std::size_t u5 = 0u;
        if (vu.positionsLod0Count > 0u) {
            if (u5 >= group5Unpacks.size() || group5Unpacks[u5]->vnvl != kV3_16) {
                return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
            }
            const auto payload = commandPayload(buffers[4], *group5Unpacks[u5]);
            if (!parsePositions(payload, vu.positionsLod0Count, positions)) {
                return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
            }
            ++u5;
        }
        if (u5 + 1u >= group5Unpacks.size()) {
            return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
        }
        std::vector<Strip> strips;
        const auto stripsPayload = commandPayload(buffers[4], *group5Unpacks[u5]);
        if (!parseStrips(stripsPayload, group5Unpacks[u5]->logicalPayloadBytes, strips)) {
            return fail(Rac1TfragStatus::InvalidStrip, std::move(mesh));
        }
        ++u5;
        std::vector<std::uint8_t> indices;
        const auto indicesPayload = commandPayload(buffers[4], *group5Unpacks[u5]);
        if (!parseIndices(indicesPayload, group5Unpacks[u5]->logicalPayloadBytes, indices)) {
            return fail(Rac1TfragStatus::InvalidStrip, std::move(mesh));
        }
        ++u5;
        if (vu.positionsLod0Count > 0u) {
            if (u5 >= group5Unpacks.size() || group5Unpacks[u5]->vnvl != kV4_8) {
                return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
            }
            ++u5;
        }
        if (u5 < group5Unpacks.size() && group5Unpacks[u5]->vnvl == kV4_8) {
            ++u5;
        }
        if (vu.positionsLod0Count > 0u) {
            if (u5 >= group5Unpacks.size() || group5Unpacks[u5]->vnvl != kV4_16) {
                return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
            }
            const auto payload = commandPayload(buffers[4], *group5Unpacks[u5]);
            if (!parseVertexInfos(payload, group5Unpacks[u5]->logicalPayloadBytes, infos)) {
                return fail(Rac1TfragStatus::MissingVifData, std::move(mesh));
            }
            ++u5;
        }

        // Vertex colors are ordinary 4-byte RGBA entries. rgbaSize is in
        // groups of four entries.
        std::size_t rgbaCount = static_cast<std::size_t>(rgbaSize) * 4u;
        std::size_t rgbaBytes = 0u;
        if (!checkedMul(rgbaCount, 4u, rgbaBytes) || !fits(rgbaOffset, rgbaBytes, data.size())) {
            return fail(Rac1TfragStatus::InvalidTfragHeader, std::move(mesh));
        }
        const auto rgbas = data.subspan(rgbaOffset, rgbaBytes);

        std::size_t indexCursor = 0u;
        std::uint32_t activeMaterial = 0u;
        bool foundSentinel = false;
        for (const Strip& strip : strips) {
            if (strip.endFlag == 0xffu) {
                foundSentinel = true;
                break;
            }
            if (strip.endFlag != 0u && strip.endFlag != 0x80u) {
                return fail(Rac1TfragStatus::InvalidStrip, std::move(mesh));
            }
            if (strip.hasMaterial) {
                if (strip.materialOffset == -1) {
                    // Deliberately keep the previous material.
                } else if (strip.materialOffset < 0 ||
                           (static_cast<unsigned>(strip.materialOffset) % 5u) != 0u) {
                    return fail(Rac1TfragStatus::InvalidMaterialIndex, std::move(mesh));
                } else {
                    const std::size_t local = static_cast<unsigned>(strip.materialOffset) / 5u;
                    if (local >= texturePrimitives.size() || texturePrimitives[local].materialIndex < 0) {
                        return fail(Rac1TfragStatus::InvalidMaterialIndex, std::move(mesh));
                    }
                    activeMaterial = static_cast<std::uint32_t>(texturePrimitives[local].materialIndex);
                }
            }
            if (textureCount == 0u || activeMaterial >= textureCount) {
                return fail(Rac1TfragStatus::InvalidMaterialIndex, std::move(mesh));
            }
            if (indexCursor > indices.size() || strip.vertexCount > indices.size() - indexCursor) {
                return fail(Rac1TfragStatus::InvalidStrip, std::move(mesh));
            }

            ++mesh.stripCount;
            mesh.sourceVertexReferences += strip.vertexCount;
            if (strip.vertexCount >= 3u) {
                std::size_t batchIndex = 0u;
                const auto existing = batchByMaterial.find(activeMaterial);
                if (existing == batchByMaterial.end()) {
                    batchIndex = mesh.batches.size();
                    batchByMaterial.emplace(activeMaterial, batchIndex);
                    mesh.batches.push_back({activeMaterial, {}});
                } else {
                    batchIndex = existing->second;
                }
                auto& batch = mesh.batches[batchIndex];

                for (std::size_t tri = 0u; tri + 2u < strip.vertexCount; ++tri) {
                    const std::array<std::uint8_t, 3> triangle = {
                        indices[indexCursor + tri + 0u],
                        indices[indexCursor + tri + 1u],
                        indices[indexCursor + tri + 2u],
                    };
                    std::array<Rac1TfragVertex, 3> vertices{};
                    for (std::size_t corner = 0u; corner < 3u; ++corner) {
                        const std::size_t infoIndex = triangle[corner];
                        if (infoIndex >= infos.size()) {
                            return fail(Rac1TfragStatus::InvalidVertexIndex, std::move(mesh));
                        }
                        const VertexInfo& info = infos[infoIndex];
                        if (info.vertex < 0 || (info.vertex & 1) != 0) {
                            return fail(Rac1TfragStatus::InvalidVertexIndex, std::move(mesh));
                        }
                        const std::size_t positionIndex = static_cast<std::uint16_t>(info.vertex) / 2u;
                        if (positionIndex >= positions.size() || positionIndex >= rgbaCount) {
                            return fail(Rac1TfragStatus::InvalidVertexIndex, std::move(mesh));
                        }
                        const PositionI16& p = positions[positionIndex];
                        const std::size_t color = positionIndex * 4u;
                        const float fixedS = info.s < 0 ? static_cast<float>(info.s) * 0.5f
                                                       : static_cast<float>(info.s);
                        const float fixedT = info.t < 0 ? static_cast<float>(info.t) * 0.5f
                                                       : static_cast<float>(info.t);
                        Rac1TfragVertex v{};
                        v.x = static_cast<float>(static_cast<std::int64_t>(basePosition[0]) + p.x) / 1024.0f;
                        v.y = static_cast<float>(static_cast<std::int64_t>(basePosition[1]) + p.y) / 1024.0f;
                        v.z = static_cast<float>(static_cast<std::int64_t>(basePosition[2]) + p.z) / 1024.0f;
                        v.u = fixedS / 4096.0f;
                        v.v = fixedT / 4096.0f;
                        v.r = expandVertexColor(rgbas[color + 0u]);
                        v.g = expandVertexColor(rgbas[color + 1u]);
                        v.b = expandVertexColor(rgbas[color + 2u]);
                        v.a = expandVertexColor(rgbas[color + 3u]);
                        if (!updateBounds(mesh.bounds, hasBounds, v)) {
                            return fail(Rac1TfragStatus::InvalidVertexIndex, std::move(mesh));
                        }
                        vertices[corner] = v;
                    }
                    batch.triangleVertices.insert(batch.triangleVertices.end(),
                                                  vertices.begin(), vertices.end());
                    ++mesh.triangleCount;
                }
            }
            indexCursor += strip.vertexCount;
        }
        if (!foundSentinel) {
            return fail(Rac1TfragStatus::InvalidStrip, std::move(mesh));
        }
    }

    if (!hasBounds || mesh.triangleCount == 0u) {
        return fail(Rac1TfragStatus::EmptyMesh, std::move(mesh));
    }
    return {Rac1TfragStatus::Ok, std::move(mesh)};
}

} // namespace ratchet::assets
