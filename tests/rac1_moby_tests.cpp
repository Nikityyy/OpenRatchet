#include "assets/rac1_moby.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <vector>

namespace {

void writeU16(std::vector<std::uint8_t>& bytes, std::size_t o, std::uint16_t v) {
    bytes[o + 0u] = static_cast<std::uint8_t>(v & 0xffu);
    bytes[o + 1u] = static_cast<std::uint8_t>((v >> 8u) & 0xffu);
}

void writeI16(std::vector<std::uint8_t>& bytes, std::size_t o, std::int16_t v) {
    writeU16(bytes, o, static_cast<std::uint16_t>(v));
}

void writeU32(std::vector<std::uint8_t>& bytes, std::size_t o, std::uint32_t v) {
    bytes[o + 0u] = static_cast<std::uint8_t>(v & 0xffu);
    bytes[o + 1u] = static_cast<std::uint8_t>((v >> 8u) & 0xffu);
    bytes[o + 2u] = static_cast<std::uint8_t>((v >> 16u) & 0xffu);
    bytes[o + 3u] = static_cast<std::uint8_t>((v >> 24u) & 0xffu);
}

void writeI32(std::vector<std::uint8_t>& bytes, std::size_t o, std::int32_t v) {
    writeU32(bytes, o, static_cast<std::uint32_t>(v));
}

void writeF32(std::vector<std::uint8_t>& bytes, std::size_t o, float v) {
    std::uint32_t bits = 0u;
    static_assert(sizeof(bits) == sizeof(v));
    std::memcpy(&bits, &v, sizeof(bits));
    writeU32(bytes, o, bits);
}

void writeVertex(std::vector<std::uint8_t>& core,
                 std::size_t o,
                 std::uint16_t cache,
                 std::int16_t x,
                 std::int16_t y,
                 std::int16_t z) {
    writeU16(core, o + 0x0u, cache);
    core[o + 0x8u] = 0u;
    core[o + 0x9u] = 0u;
    writeI16(core, o + 0xau, x);
    writeI16(core, o + 0xcu, y);
    writeI16(core, o + 0xeu, z);
}

std::vector<std::uint8_t> makeCore() {
    std::vector<std::uint8_t> core(0x600u, 0u);
    constexpr std::size_t cls = 0x100u;

    // R&C1 moby class header: two high-LOD packets, unit class scale. The
    // second packet intentionally contains no material upload so it must inherit
    // the TEX0 selected at the end of packet 0.
    writeI32(core, cls + 0x00u, 0x48);
    core[cls + 0x04u] = 2u;
    writeF32(core, cls + 0x24u, 1024.0f);

    // Packet header.
    constexpr std::size_t packetHeader = cls + 0x48u;
    writeI32(core, packetHeader + 0x00u, 0x200); // VIF list relative to class
    writeU16(core, packetHeader + 0x04u, 12u);   // 0xc0 bytes / 12 qwords
    writeU16(core, packetHeader + 0x06u, 1u);    // texture UNPACK is present
    writeI32(core, packetHeader + 0x08u, 0x100); // vertex table relative to class
    core[packetHeader + 0x0cu] = 9u;
    core[packetHeader + 0x0du] = 0u;
    core[packetHeader + 0x0eu] = 0u;
    core[packetHeader + 0x0fu] = 0u;

    // Packet 1 reuses the vertex table but has only UV/index VIF uploads. It
    // relies on the GS material state left by packet 0. Resetting currentMaterial
    // per packet incorrectly paints this triangle with global moby texture 0.
    constexpr std::size_t packetHeader1 = packetHeader + 0x10u;
    writeI32(core, packetHeader1 + 0x00u, 0x300);
    writeU16(core, packetHeader1 + 0x04u, 3u); // 0x30 bytes / 3 qwords
    writeU16(core, packetHeader1 + 0x06u, 0u); // no texture UNPACK
    writeI32(core, packetHeader1 + 0x08u, 0x100);
    core[packetHeader1 + 0x0cu] = 9u;
    core[packetHeader1 + 0x0du] = 0u;
    core[packetHeader1 + 0x0eu] = 0u;
    core[packetHeader1 + 0x0fu] = 0u;

    // R&C1 vertex-table header followed by seven serialized vertices. Seven is
    // intentional: retail R&C1 stores the final cache addresses in the tail of
    // the seventh-last 0x10-byte record.
    constexpr std::size_t vertexTable = cls + 0x100u;
    writeI32(core, vertexTable + 0x00u, 0); // matrix transfers
    writeI32(core, vertexTable + 0x04u, 0); // two-way blend
    writeI32(core, vertexTable + 0x08u, 0); // three-way blend
    writeI32(core, vertexTable + 0x0cu, 7); // main vertices
    writeI32(core, vertexTable + 0x10u, 0); // duplicate vertices
    writeI32(core, vertexTable + 0x14u, 0); // transfer vertices
    writeI32(core, vertexTable + 0x18u, 0x20);
    writeI32(core, vertexTable + 0x1cu, 0x90); // 0x20 + 7 * 0x10

    constexpr std::size_t vertices = vertexTable + 0x20u;
    writeVertex(core, vertices + 0x00u, 0u, 0, 0, 0);
    writeVertex(core, vertices + 0x10u, 1u, 1, 0, 0);
    writeVertex(core, vertices + 0x20u, 2u, 0, 1, 0);
    writeVertex(core, vertices + 0x30u, 3u, 0, 0, 1);
    writeVertex(core, vertices + 0x40u, 4u, 1, 1, 0);
    writeVertex(core, vertices + 0x50u, 5u, 1, 0, 1);
    writeVertex(core, vertices + 0x60u, 6u, 3, 4, 5);
    // The last record's bytes +4..+14 are the cache-address epilogue for the
    // first six vertices. x/y/z already provide addresses 3/4/5.
    writeU16(core, vertices + 0x64u, 0u);
    writeU16(core, vertices + 0x66u, 1u);
    writeU16(core, vertices + 0x68u, 2u);

    // VIF list: seven UVs, one indices block, and two 0x40-byte texture
    // primitives. The terminating hidden index lives at +0x1c in the first
    // primitive's GIF-AD padding, not at +0x0c of the second primitive.
    std::size_t o = cls + 0x200u;
    writeU32(core, o, (0x65u << 24u) | (7u << 16u)); // UNPACK V2_16 x7
    o += 4u;
    const std::int16_t uv[7][2] = {
        {0, 0}, {4096, 0}, {0, 4096}, {0, 0}, {0, 0}, {0, 0}, {0, 0},
    };
    for (const auto& st : uv) {
        writeI16(core, o + 0u, st[0]);
        writeI16(core, o + 2u, st[1]);
        o += 4u;
    }

    writeU32(core, o, (0x6eu << 24u) | (4u << 16u)); // UNPACK V4_8 x3
    o += 4u;
    core[o + 0u] = 0u;
    core[o + 1u] = 0u;
    core[o + 2u] = 1u; // first material reset resolves vertex 0
    core[o + 3u] = 0u;
    core[o + 4u] = 0u;
    core[o + 5u] = static_cast<std::uint8_t>(-126);
    core[o + 6u] = 3u; // first triangle, material 0
    core[o + 7u] = 0u;
    core[o + 8u] = static_cast<std::uint8_t>(-126);
    core[o + 9u] = 3u; // first material-1 triangle
    core[o + 10u] = 3u;
    core[o + 11u] = 3u;
    core[o + 12u] = 3u; // keep enough queued triangles for async-tail discard
    core[o + 13u] = 0u; // hidden zero terminates and discards the last 3 triangles
    core[o + 14u] = static_cast<std::uint8_t>(-127);
    core[o + 15u] = static_cast<std::uint8_t>(-127);
    o += 16u;

    writeU32(core, o, (0x6cu << 24u) | (8u << 16u)); // UNPACK V4_32 x8 = 0x80 bytes
    o += 4u;
    writeI32(core, o + 0x0cu, 1); // hidden index for the second material change
    writeI32(core, o + 0x1cu, 0); // hidden async-stream terminator
    writeI32(core, o + 0x20u, 0); // TEX0.low => class-local texture slot 0
    writeI32(core, o + 0x4cu, 1); // poison: old one-per-record secret-index read
    writeI32(core, o + 0x60u, 1); // TEX0.low => class-local texture slot 1
    o += 0x80u;

    // Pad the VIF list to its 0xc0-byte qword size with NOPs.
    writeU32(core, o + 0u, 0u);
    writeU32(core, o + 4u, 0u);
    o += 8u;

    if (o != cls + 0x2c0u) {
        std::cerr << "internal fixture VIF size mismatch\n";
    }

    // Packet 1: same seven UVs followed by a single triangle index stream. No
    // texture primitive is uploaded here; material 1/global texture 2 from the
    // previous packet must remain active.
    o = cls + 0x300u;
    writeU32(core, o, (0x65u << 24u) | (7u << 16u));
    o += 4u;
    for (const auto& st : uv) {
        writeI16(core, o + 0u, st[0]);
        writeI16(core, o + 2u, st[1]);
        o += 4u;
    }
    writeU32(core, o, (0x6eu << 24u) | (2u << 16u)); // 8-byte indices payload
    o += 4u;
    core[o + 0u] = 0u;
    core[o + 1u] = 0u;
    core[o + 2u] = 0u;
    core[o + 3u] = 0u;
    core[o + 4u] = static_cast<std::uint8_t>(-127);
    core[o + 5u] = static_cast<std::uint8_t>(-126);
    core[o + 6u] = 3u;
    core[o + 7u] = static_cast<std::uint8_t>(-127);
    o += 8u;
    writeU32(core, o, 0u); // pad qword with VIF NOP
    o += 4u;
    if (o != cls + 0x330u) {
        std::cerr << "internal fixture packet-1 VIF size mismatch\n";
    }
    return core;
}

std::vector<std::uint8_t> makeIndex() {
    std::vector<std::uint8_t> index(0x40u, 0xffu);
    writeU32(index, 0x00u, 0x100u);
    writeI32(index, 0x04u, 42);
    index[0x10u] = 3u;   // local texture 0 -> global moby texture 3
    index[0x11u] = 2u;   // local texture 1 -> global moby texture 2
    index[0x12u] = 0xffu;
    return index;
}

std::vector<std::uint8_t> makeGameplayForClasses(const std::vector<std::int32_t>& classes) {
    constexpr std::size_t block = 0x100u;
    constexpr std::size_t header = 0x10u;
    constexpr std::size_t instanceBytes = 0x78u;
    std::vector<std::uint8_t> gameplay(
        block + header + classes.size() * instanceBytes + 0x10u, 0u);
    writeI32(gameplay, 0x44u, static_cast<std::int32_t>(block));
    writeI32(gameplay, block, static_cast<std::int32_t>(classes.size()));
    for (std::size_t i = 0u; i < classes.size(); ++i) {
        const std::size_t instance = block + header + i * instanceBytes;
        writeI32(gameplay, instance + 0x18u, classes[i]);
        writeF32(gameplay, instance + 0x1cu, 1.0f);
        writeF32(gameplay, instance + 0x30u, 10.0f + static_cast<float>(i));
        writeF32(gameplay, instance + 0x34u, 20.0f);
        writeF32(gameplay, instance + 0x38u, 30.0f);
        writeF32(gameplay, instance + 0x3cu, 0.0f);
        writeF32(gameplay, instance + 0x40u, 0.0f);
        writeF32(gameplay, instance + 0x44u, 0.0f);
        // Deliberately non-neutral ambient light. This is a lighting input in the
        // original moby shader, not a direct texture tint; Phase 9 must not apply
        // it by itself while normals/directional lights are not yet evaluated.
        gameplay[instance + 0x64u] = 128u;
        gameplay[instance + 0x65u] = 0u;
        gameplay[instance + 0x66u] = 0u;
    }
    return gameplay;
}

std::vector<std::uint8_t> makeGameplay() {
    return makeGameplayForClasses({42});
}

bool near(float a, float b) {
    return std::fabs(a - b) < 0.0001f;
}

} // namespace

int main() {
    const auto result = ratchet::assets::decodeRac1MobyScene(
        makeCore(), makeIndex(), makeGameplay(), {0x00u, 1u}, 4u);
    if (!result.ok()) {
        std::cerr << "decode failed: "
                  << ratchet::assets::rac1MobyStatusName(result.status) << '\n';
        return 1;
    }

    const auto& mesh = result.mesh;
    if (mesh.classCount != 1u || mesh.renderableClassCount != 1u ||
        mesh.instanceCount != 1u || mesh.renderedInstanceCount != 1u ||
        mesh.intentionallyNonVisibleInstanceCount != 0u ||
        mesh.skippedInstanceCount != 0u || mesh.missingClassInstanceCount != 0u ||
        mesh.unaccountedInstanceCount != 0u || !mesh.skippedClasses.empty() ||
        mesh.sourceTriangleCount != 3u || mesh.specialMaterialTriangleCount != 0u ||
        mesh.triangleCount != 3u || mesh.batches.size() != 2u ||
        mesh.renderedInstances.size() != 1u) {
        std::cerr << "unexpected moby counts/material\n";
        return 1;
    }

    const ratchet::assets::Rac1MobyBatch* material3 = nullptr;
    const ratchet::assets::Rac1MobyBatch* material2 = nullptr;
    for (const auto& batch : mesh.batches) {
        if (batch.materialIndex == 3u) material3 = &batch;
        if (batch.materialIndex == 2u) material2 = &batch;
    }
    if (material3 == nullptr || material2 == nullptr ||
        material3->triangleVertices.size() != 3u || material2->triangleVertices.size() != 6u) {
        std::cerr << "missing expected moby material batches\n";
        return 1;
    }

    bool sawOrigin = false;
    bool sawX = false;
    bool sawY = false;
    for (const auto& v : material3->triangleVertices) {
        sawOrigin |= near(v.x, 10.0f) && near(v.y, 20.0f) && near(v.z, 30.0f) &&
                     near(v.u, 0.0f) && near(v.v, 0.0f);
        sawX |= near(v.x, 11.0f) && near(v.y, 20.0f) && near(v.z, 30.0f) &&
                near(v.u, 1.0f) && near(v.v, 0.0f);
        sawY |= near(v.x, 10.0f) && near(v.y, 21.0f) && near(v.z, 30.0f) &&
                near(v.u, 0.0f) && near(v.v, 1.0f);
        if (v.r != 255u || v.g != 255u || v.b != 255u || v.a != 255u) {
            std::cerr << "partial moby lighting leaked into bind-pose colour\n";
            return 1;
        }
    }
    if (!sawOrigin || !sawX || !sawY) {
        std::cerr << "instance/class transform or texcoords were not applied\n";
        return 1;
    }

    // Phase 10 Step 9: visible triangle vertices retain the exact explicit
    // skinning source that produced their position, including across packet
    // cache reuse. The fixture has seven explicit vertices in each of two
    // packets, so the flattened Step-8 source domain contains 14 entries.
    const auto& rendered = mesh.renderedInstances.front();
    if (rendered.instanceIndex != 0u || rendered.oClass != 42 ||
        !near(rendered.classVertexScale, 1.0f) || !near(rendered.scale, 1.0f) ||
        !near(rendered.position[0], 10.0f) || !near(rendered.position[1], 20.0f) ||
        !near(rendered.position[2], 30.0f) || rendered.skinVertexCount != 14u) {
        std::cerr << "rendered moby instance animation metadata mismatch\n";
        return 1;
    }
    for (const auto& batch : mesh.batches) {
        for (const auto& v : batch.triangleVertices) {
            if (v.oClass != 42 || v.instanceIndex != 0u || v.skinVertexIndex >= 14u) {
                std::cerr << "visible moby vertex lost skin-source identity\n";
                return 1;
            }
        }
    }
    const auto transformed = ratchet::assets::transformRac1MobySkinnedPositionToWorld(
        rendered, {1.0f, 2.0f, 3.0f});
    if (!near(transformed[0], 11.0f) || !near(transformed[1], 22.0f) ||
        !near(transformed[2], 33.0f)) {
        std::cerr << "skinned raw-to-world transform mismatch\n";
        return 1;
    }
    for (const auto& v : material2->triangleVertices) {
        if (v.r != 255u || v.g != 255u || v.b != 255u || v.a != 255u) {
            std::cerr << "second material received partial moby lighting\n";
            return 1;
        }
    }

    // An unreferenced class entry may be dormant/unsupported without breaking
    // the scene. Only gameplay-referenced moby classes are part of this decode.
    auto coreWithDormant = makeCore();
    auto indexWithDormant = makeIndex();
    indexWithDormant.resize(0x40u, 0xffu);
    writeU32(indexWithDormant, 0x20u, 0x5f0u); // deliberately invalid class blob
    writeI32(indexWithDormant, 0x24u, 99);
    const auto dormantResult = ratchet::assets::decodeRac1MobyScene(
        coreWithDormant, indexWithDormant, makeGameplay(), {0x00u, 2u}, 4u);
    if (!dormantResult.ok() || dormantResult.mesh.classCount != 2u ||
        dormantResult.mesh.renderableClassCount != 1u ||
        dormantResult.mesh.renderedInstanceCount != 1u) {
        std::cerr << "unreferenced moby class should not poison scene decode\n";
        return 1;
    }


    // Every non-rendered instance must now be structurally accounted for with
    // an exact oClass and reason. No missing class is allowed to disappear.
    auto accountingCore = makeCore();
    auto accountingIndex = makeIndex();
    accountingIndex.resize(0x80u, 0xffu);

    // oClass 100: no class blob in the level index.
    writeU32(accountingIndex, 0x20u, 0u);
    writeI32(accountingIndex, 0x24u, 100);

    // oClass 101: class blob exists but explicitly has no packet table / mesh.
    writeU32(accountingIndex, 0x40u, 0x500u);
    writeI32(accountingIndex, 0x44u, 101);
    writeI32(accountingCore, 0x500u + 0x00u, 0);
    accountingCore[0x500u + 0x04u] = 0u;
    accountingCore[0x500u + 0x05u] = 0u;
    writeF32(accountingCore, 0x500u + 0x24u, 1024.0f);

    // oClass 102: packet-table pointer exists, but both LOD packet counts are 0.
    writeU32(accountingIndex, 0x60u, 0x540u);
    writeI32(accountingIndex, 0x64u, 102);
    writeI32(accountingCore, 0x540u + 0x00u, 0x48);
    accountingCore[0x540u + 0x04u] = 0u;
    accountingCore[0x540u + 0x05u] = 0u;
    writeF32(accountingCore, 0x540u + 0x24u, 1024.0f);

    const auto accountingResult = ratchet::assets::decodeRac1MobyScene(
        accountingCore, accountingIndex,
        makeGameplayForClasses({42, 100, 101, 102}), {0x00u, 4u}, 4u);
    if (!accountingResult.ok()) {
        std::cerr << "accounted non-renderable classes failed: "
                  << ratchet::assets::rac1MobyStatusName(accountingResult.status) << '\n';
        return 1;
    }
    const auto& accounted = accountingResult.mesh;
    if (accounted.instanceCount != 4u || accounted.renderedInstanceCount != 1u ||
        accounted.intentionallyNonVisibleInstanceCount != 3u ||
        accounted.skippedInstanceCount != 3u || accounted.missingClassInstanceCount != 0u ||
        accounted.unaccountedInstanceCount != 0u || accounted.skippedClasses.size() != 3u ||
        accounted.renderedInstanceCount + accounted.intentionallyNonVisibleInstanceCount !=
            accounted.instanceCount) {
        std::cerr << "moby skip accounting invariant failed\n";
        return 1;
    }

    auto expectSkip = [&](std::int32_t oClass, ratchet::assets::Rac1MobySkipReason reason) {
        for (const auto& skipped : accounted.skippedClasses) {
            if (skipped.oClass == oClass && skipped.reason == reason &&
                skipped.instanceCount == 1u && skipped.sourceTriangleCount == 0u &&
                skipped.specialMaterialTriangleCount == 0u) {
                return true;
            }
        }
        return false;
    };
    if (!expectSkip(100, ratchet::assets::Rac1MobySkipReason::NoClassData) ||
        !expectSkip(101, ratchet::assets::Rac1MobySkipReason::NoPacketTable) ||
        !expectSkip(102, ratchet::assets::Rac1MobySkipReason::ZeroLodPacketCounts)) {
        std::cerr << "moby skip reason attribution failed\n";
        return 1;
    }


    // Negative TEX0 is an intentional special material in the retail renderer.
    // The reference fragment path discards it; it is not a missing/failed mesh.
    // If every surviving triangle in a class uses that material, the instance
    // must be fully accounted as intentionally invisible.
    auto specialOnlyCore = makeCore();
    writeI32(specialOnlyCore, 0x358u, -1); // packet 0 material 0 TEX0.low
    writeI32(specialOnlyCore, 0x398u, -1); // packet 0 material 1 TEX0.low
    const auto specialOnlyResult = ratchet::assets::decodeRac1MobyScene(
        specialOnlyCore, makeIndex(), makeGameplay(), {0x00u, 1u}, 4u);
    if (!specialOnlyResult.ok()) {
        std::cerr << "special-material-only class failed: "
                  << ratchet::assets::rac1MobyStatusName(specialOnlyResult.status) << '\n';
        return 1;
    }
    const auto& specialOnly = specialOnlyResult.mesh;
    if (specialOnly.instanceCount != 1u || specialOnly.renderedInstanceCount != 0u ||
        specialOnly.intentionallyNonVisibleInstanceCount != 1u ||
        specialOnly.skippedInstanceCount != 1u || specialOnly.skippedClasses.size() != 1u ||
        specialOnly.sourceTriangleCount != 3u ||
        specialOnly.specialMaterialTriangleCount != 3u || specialOnly.triangleCount != 0u ||
        specialOnly.missingClassInstanceCount != 0u ||
        specialOnly.unaccountedInstanceCount != 0u) {
        std::cerr << "special-material-only accounting failed\n";
        return 1;
    }
    const auto& specialSkip = specialOnly.skippedClasses.front();
    if (specialSkip.oClass != 42 ||
        specialSkip.reason != ratchet::assets::Rac1MobySkipReason::SpecialMaterialDiscard ||
        specialSkip.instanceCount != 1u || specialSkip.sourceTriangleCount != 3u ||
        specialSkip.specialMaterialTriangleCount != 3u) {
        std::cerr << "special-material skip reason attribution failed\n";
        return 1;
    }

    // A mixed class remains rendered, while its special-material triangle is
    // still included in the source-vs-visible triangle accounting.
    auto mixedMaterialCore = makeCore();
    writeI32(mixedMaterialCore, 0x358u, -1);
    const auto mixedMaterialResult = ratchet::assets::decodeRac1MobyScene(
        mixedMaterialCore, makeIndex(), makeGameplay(), {0x00u, 1u}, 4u);
    if (!mixedMaterialResult.ok() || mixedMaterialResult.mesh.renderedInstanceCount != 1u ||
        mixedMaterialResult.mesh.intentionallyNonVisibleInstanceCount != 0u ||
        mixedMaterialResult.mesh.sourceTriangleCount != 3u ||
        mixedMaterialResult.mesh.specialMaterialTriangleCount != 1u ||
        mixedMaterialResult.mesh.triangleCount != 2u) {
        std::cerr << "mixed special-material accounting failed\n";
        return 1;
    }

    // High-LOD packet metadata which produces no post-trim geometry remains a
    // hard failure. This keeps SpecialMaterialDiscard from becoming a catch-all.
    auto emptyGeometryCore = makeCore();
    emptyGeometryCore[0x104u] = 1u; // only packet 0
    writeI32(emptyGeometryCore, 0x344u, 0); // first hidden material/reset index => terminate
    const auto emptyGeometryResult = ratchet::assets::decodeRac1MobyScene(
        emptyGeometryCore, makeIndex(), makeGameplay(), {0x00u, 1u}, 4u);
    if (emptyGeometryResult.status !=
        ratchet::assets::Rac1MobyStatus::UnaccountedNonRenderableClass) {
        std::cerr << "empty high-LOD packet stream was silently accepted\n";
        return 1;
    }

    // A referenced oClass absent from the level class table is a hard failure.
    const auto missingClassResult = ratchet::assets::decodeRac1MobyScene(
        makeCore(), makeIndex(), makeGameplayForClasses({42, 777}), {0x00u, 1u}, 4u);
    if (missingClassResult.status != ratchet::assets::Rac1MobyStatus::MissingReferencedClass ||
        missingClassResult.mesh.missingClassInstanceCount != 1u) {
        std::cerr << "missing referenced moby class was silently accepted\n";
        return 1;
    }

    // A low-LOD-only class is potentially visible geometry, not a logic-only
    // object. Phase 9 does not decode that path yet, so it must fail loudly.
    auto lowOnlyCore = makeCore();
    auto lowOnlyIndex = makeIndex();
    lowOnlyIndex.resize(0x40u, 0xffu);
    writeU32(lowOnlyIndex, 0x20u, 0x580u);
    writeI32(lowOnlyIndex, 0x24u, 104);
    writeI32(lowOnlyCore, 0x580u + 0x00u, 0x48);
    lowOnlyCore[0x580u + 0x04u] = 0u;
    lowOnlyCore[0x580u + 0x05u] = 1u;
    writeF32(lowOnlyCore, 0x580u + 0x24u, 1024.0f);
    const auto lowOnlyResult = ratchet::assets::decodeRac1MobyScene(
        lowOnlyCore, lowOnlyIndex, makeGameplayForClasses({42, 104}), {0x00u, 2u}, 4u);
    if (lowOnlyResult.status != ratchet::assets::Rac1MobyStatus::UnsupportedLowLodOnlyClass) {
        std::cerr << "low-LOD-only moby class was silently skipped\n";
        return 1;
    }

    std::cout << "rac1_moby_tests: ok\n";
    return 0;
}
