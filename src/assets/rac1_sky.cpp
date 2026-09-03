#include "assets/rac1_sky.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <utility>
#include <vector>

namespace ratchet::assets {
namespace {

constexpr std::size_t kSkyHeaderBytes = 0x40u;
constexpr std::size_t kSkyTextureEntryBytes = 0x10u;
constexpr std::size_t kSkyShellHeaderBytes = 0x10u;
constexpr std::size_t kSkyClusterHeaderBytes = 0x20u;
constexpr std::size_t kSkyVertexBytes = 0x8u;
constexpr std::size_t kSkyTexcoordBytes = 0x4u;
constexpr std::size_t kSkyRgbaBytes = 0x4u;
constexpr std::size_t kSkyFaceBytes = 0x4u;
constexpr std::size_t kPaletteBytes = 256u * 4u;
constexpr float kPositionScale = 1.0f / 1024.0f;
constexpr float kFixed12 = 1.0f / 4096.0f;
constexpr std::size_t kMaxCount = 1u << 20u;
constexpr std::uint32_t kMaxTextureSide = 4096u;

std::uint16_t readU16(std::span<const std::uint8_t> bytes,
                      std::size_t offset) noexcept {
    return static_cast<std::uint16_t>(bytes[offset]) |
           (static_cast<std::uint16_t>(bytes[offset + 1u]) << 8u);
}

std::int16_t readI16(std::span<const std::uint8_t> bytes,
                     std::size_t offset) noexcept {
    return static_cast<std::int16_t>(readU16(bytes, offset));
}

std::uint32_t readU32(std::span<const std::uint8_t> bytes,
                      std::size_t offset) noexcept {
    return static_cast<std::uint32_t>(bytes[offset]) |
           (static_cast<std::uint32_t>(bytes[offset + 1u]) << 8u) |
           (static_cast<std::uint32_t>(bytes[offset + 2u]) << 16u) |
           (static_cast<std::uint32_t>(bytes[offset + 3u]) << 24u);
}

std::int32_t readI32(std::span<const std::uint8_t> bytes,
                     std::size_t offset) noexcept {
    return static_cast<std::int32_t>(readU32(bytes, offset));
}

bool fits(std::size_t offset, std::size_t size, std::size_t capacity) noexcept {
    return offset <= capacity && size <= capacity - offset;
}

bool mul(std::size_t a, std::size_t b, std::size_t& result) noexcept {
    if (b != 0u && a > std::numeric_limits<std::size_t>::max() / b) return false;
    result = a * b;
    return true;
}

std::uint8_t mapPaletteIndex(std::uint8_t index) noexcept {
    const bool bit4Shifted = ((index & 0x10u) >> 1u) != 0u;
    const bool bit3 = (index & 0x08u) != 0u;
    return bit4Shifted != bit3 ? static_cast<std::uint8_t>(index ^ 0x18u) : index;
}

std::uint8_t doubledAlpha(std::uint8_t alpha) noexcept {
    return static_cast<std::uint8_t>(
        std::min<unsigned>(255u, static_cast<unsigned>(alpha) * 2u));
}

std::uint8_t skyVertexAlpha(std::int16_t alpha) noexcept {
    if (alpha == 0x80) return 255u;
    if (alpha <= 0) return 0u;
    return static_cast<std::uint8_t>(
        std::min<int>(255, static_cast<int>(alpha) * 2));
}

std::size_t batchFor(Rac1SkyMesh& mesh, std::uint32_t materialIndex) {
    for (std::size_t i = 0u; i < mesh.batches.size(); ++i) {
        if (mesh.batches[i].materialIndex == materialIndex) return i;
    }
    mesh.batches.push_back({materialIndex, {}});
    return mesh.batches.size() - 1u;
}

Rac1SkyResult fail(Rac1SkyStatus status, Rac1SkyMesh mesh = {}) {
    return {status, std::move(mesh)};
}

} // namespace

const char* rac1SkyStatusName(Rac1SkyStatus status) noexcept {
    switch (status) {
    case Rac1SkyStatus::Ok:
        return "ok";
    case Rac1SkyStatus::OffsetOutOfRange:
        return "offset-out-of-range";
    case Rac1SkyStatus::InvalidHeader:
        return "invalid-header";
    case Rac1SkyStatus::InvalidTexture:
        return "invalid-texture";
    case Rac1SkyStatus::InvalidShell:
        return "invalid-shell";
    case Rac1SkyStatus::InvalidCluster:
        return "invalid-cluster";
    case Rac1SkyStatus::InvalidFace:
        return "invalid-face";
    case Rac1SkyStatus::EmptySky:
        return "empty-sky";
    }
    return "unknown";
}

Rac1SkyResult decodeRac1Sky(std::span<const std::uint8_t> core,
                            std::uint32_t skyOffset) {
    if (!fits(skyOffset, kSkyHeaderBytes, core.size())) {
        return fail(Rac1SkyStatus::OffsetOutOfRange);
    }
    const auto sky = core.subspan(skyOffset);

    Rac1SkyMesh mesh{};
    mesh.clearColor = {sky[0u], sky[1u], sky[2u], sky[3u]};

    const std::int16_t shellCountSigned = readI16(sky, 0x06u);
    const std::int16_t textureCountSigned = readI16(sky, 0x0cu);
    const std::int32_t textureDefsSigned = readI32(sky, 0x10u);
    const std::int32_t textureDataSigned = readI32(sky, 0x14u);
    if (shellCountSigned < 0 || shellCountSigned > 8 ||
        textureCountSigned < 0 ||
        static_cast<std::size_t>(textureCountSigned) > kMaxCount ||
        textureDefsSigned < 0 || textureDataSigned < 0) {
        return fail(Rac1SkyStatus::InvalidHeader, std::move(mesh));
    }
    mesh.shellCount = static_cast<std::size_t>(shellCountSigned);
    const std::size_t textureCount = static_cast<std::size_t>(textureCountSigned);
    const std::size_t textureDefs = static_cast<std::size_t>(textureDefsSigned);
    const std::size_t textureData = static_cast<std::size_t>(textureDataSigned);

    std::size_t textureDefsBytes = 0u;
    if (!mul(textureCount, kSkyTextureEntryBytes, textureDefsBytes) ||
        !fits(textureDefs, textureDefsBytes, sky.size()) ||
        textureData > sky.size()) {
        return fail(Rac1SkyStatus::InvalidHeader, std::move(mesh));
    }

    mesh.textures.reserve(textureCount);
    for (std::size_t i = 0u; i < textureCount; ++i) {
        const std::size_t entry = textureDefs + i * kSkyTextureEntryBytes;
        const std::int32_t paletteSigned = readI32(sky, entry + 0x0u);
        const std::int32_t pixelsSigned = readI32(sky, entry + 0x4u);
        const std::int32_t widthSigned = readI32(sky, entry + 0x8u);
        const std::int32_t heightSigned = readI32(sky, entry + 0xcu);
        if (paletteSigned < 0 || pixelsSigned < 0 || widthSigned <= 0 ||
            heightSigned <= 0 ||
            static_cast<std::uint32_t>(widthSigned) > kMaxTextureSide ||
            static_cast<std::uint32_t>(heightSigned) > kMaxTextureSide) {
            return fail(Rac1SkyStatus::InvalidTexture, std::move(mesh));
        }

        const std::size_t width = static_cast<std::size_t>(widthSigned);
        const std::size_t height = static_cast<std::size_t>(heightSigned);
        std::size_t pixelCount = 0u;
        if (!mul(width, height, pixelCount)) {
            return fail(Rac1SkyStatus::InvalidTexture, std::move(mesh));
        }
        const std::size_t paletteOffset = textureData +
                                          static_cast<std::size_t>(paletteSigned);
        const std::size_t pixelOffset = textureData +
                                        static_cast<std::size_t>(pixelsSigned);
        if (paletteOffset < textureData || pixelOffset < textureData ||
            !fits(paletteOffset, kPaletteBytes, sky.size()) ||
            !fits(pixelOffset, pixelCount, sky.size())) {
            return fail(Rac1SkyStatus::InvalidTexture, std::move(mesh));
        }

        Rac1Texture texture{};
        texture.width = static_cast<std::uint16_t>(width);
        texture.height = static_cast<std::uint16_t>(height);
        texture.rgba.resize(pixelCount * 4u);
        bool hasAlpha = false;
        for (std::size_t p = 0u; p < pixelCount; ++p) {
            const std::uint8_t index = mapPaletteIndex(sky[pixelOffset + p]);
            const std::size_t color = paletteOffset + static_cast<std::size_t>(index) * 4u;
            const std::uint8_t alpha = doubledAlpha(sky[color + 3u]);
            texture.rgba[p * 4u + 0u] = sky[color + 0u];
            texture.rgba[p * 4u + 1u] = sky[color + 1u];
            texture.rgba[p * 4u + 2u] = sky[color + 2u];
            texture.rgba[p * 4u + 3u] = alpha;
            hasAlpha |= alpha < 255u;
        }
        texture.hasAlpha = hasAlpha;
        mesh.textures.push_back(std::move(texture));
    }

    for (std::size_t shellIndex = 0u; shellIndex < mesh.shellCount; ++shellIndex) {
        const std::int32_t shellOffsetSigned = readI32(sky, 0x20u + shellIndex * 4u);
        if (shellOffsetSigned < 0) {
            return fail(Rac1SkyStatus::InvalidShell, std::move(mesh));
        }
        const std::size_t shellOffset = static_cast<std::size_t>(shellOffsetSigned);
        if (!fits(shellOffset, kSkyShellHeaderBytes, sky.size())) {
            return fail(Rac1SkyStatus::InvalidShell, std::move(mesh));
        }
        const std::int32_t clusterCountSigned = readI32(sky, shellOffset + 0x0u);
        const std::int32_t flags = readI32(sky, shellOffset + 0x4u);
        if (clusterCountSigned < 0 ||
            static_cast<std::size_t>(clusterCountSigned) > kMaxCount) {
            return fail(Rac1SkyStatus::InvalidShell, std::move(mesh));
        }
        const std::size_t clusterCount = static_cast<std::size_t>(clusterCountSigned);
        const bool textured = (flags & 1) == 0;
        std::size_t clusterHeaderBytes = 0u;
        if (!mul(clusterCount, kSkyClusterHeaderBytes, clusterHeaderBytes) ||
            !fits(shellOffset + kSkyShellHeaderBytes,
                  clusterHeaderBytes,
                  sky.size())) {
            return fail(Rac1SkyStatus::InvalidShell, std::move(mesh));
        }
        mesh.clusterCount += clusterCount;

        for (std::size_t ci = 0u; ci < clusterCount; ++ci) {
            const std::size_t ch = shellOffset + kSkyShellHeaderBytes +
                                   ci * kSkyClusterHeaderBytes;
            const std::int32_t dataSigned = readI32(sky, ch + 0x10u);
            const std::int16_t vertexCountSigned = readI16(sky, ch + 0x14u);
            const std::int16_t triangleCountSigned = readI16(sky, ch + 0x16u);
            const std::int16_t vertexOffsetSigned = readI16(sky, ch + 0x18u);
            const std::int16_t attrOffsetSigned = readI16(sky, ch + 0x1au);
            const std::int16_t triangleOffsetSigned = readI16(sky, ch + 0x1cu);
            const std::int16_t dataSizeSigned = readI16(sky, ch + 0x1eu);
            if (dataSigned < 0 || vertexCountSigned < 0 || triangleCountSigned < 0 ||
                vertexOffsetSigned < 0 || attrOffsetSigned < 0 ||
                triangleOffsetSigned < 0 || dataSizeSigned < 0) {
                return fail(Rac1SkyStatus::InvalidCluster, std::move(mesh));
            }

            const std::size_t data = static_cast<std::size_t>(dataSigned);
            const std::size_t vertexCount = static_cast<std::size_t>(vertexCountSigned);
            const std::size_t triangleCount = static_cast<std::size_t>(triangleCountSigned);
            const std::size_t vertexOffset = static_cast<std::size_t>(vertexOffsetSigned);
            const std::size_t attrOffset = static_cast<std::size_t>(attrOffsetSigned);
            const std::size_t triangleOffset = static_cast<std::size_t>(triangleOffsetSigned);
            const std::size_t dataSize = static_cast<std::size_t>(dataSizeSigned);
            if (vertexCount > 255u || vertexCount > kMaxCount ||
                triangleCount > kMaxCount || !fits(data, dataSize, sky.size())) {
                return fail(Rac1SkyStatus::InvalidCluster, std::move(mesh));
            }

            std::size_t vertexBytes = 0u;
            std::size_t attrBytes = 0u;
            std::size_t faceBytes = 0u;
            if (!mul(vertexCount, kSkyVertexBytes, vertexBytes) ||
                !mul(vertexCount,
                     textured ? kSkyTexcoordBytes : kSkyRgbaBytes,
                     attrBytes) ||
                !mul(triangleCount, kSkyFaceBytes, faceBytes) ||
                !fits(data + vertexOffset, vertexBytes, sky.size()) ||
                !fits(data + attrOffset, attrBytes, sky.size()) ||
                !fits(data + triangleOffset, faceBytes, sky.size())) {
                return fail(Rac1SkyStatus::InvalidCluster, std::move(mesh));
            }

            struct SourceVertex {
                Rac1SkyVertex vertex{};
            };
            std::vector<SourceVertex> vertices(vertexCount);
            for (std::size_t vi = 0u; vi < vertexCount; ++vi) {
                const std::size_t vo = data + vertexOffset + vi * kSkyVertexBytes;
                auto& vertex = vertices[vi].vertex;
                vertex.x = static_cast<float>(readI16(sky, vo + 0x0u)) * kPositionScale;
                vertex.y = static_cast<float>(readI16(sky, vo + 0x2u)) * kPositionScale;
                vertex.z = static_cast<float>(readI16(sky, vo + 0x4u)) * kPositionScale;
                vertex.a = skyVertexAlpha(readI16(sky, vo + 0x6u));

                const std::size_t ao = data + attrOffset +
                                       vi * (textured ? kSkyTexcoordBytes : kSkyRgbaBytes);
                if (textured) {
                    vertex.u = static_cast<float>(readU16(sky, ao + 0x0u)) * kFixed12;
                    vertex.v = static_cast<float>(readU16(sky, ao + 0x2u)) * kFixed12;
                } else {
                    vertex.r = sky[ao + 0u];
                    vertex.g = sky[ao + 1u];
                    vertex.b = sky[ao + 2u];
                    const std::uint8_t rgbaAlpha = doubledAlpha(sky[ao + 3u]);
                    vertex.a = static_cast<std::uint8_t>(
                        (static_cast<unsigned>(vertex.a) * rgbaAlpha + 127u) / 255u);
                }
            }

            for (std::size_t ti = 0u; ti < triangleCount; ++ti) {
                const std::size_t face = data + triangleOffset + ti * kSkyFaceBytes;
                const std::uint8_t i0 = sky[face + 0u];
                const std::uint8_t i1 = sky[face + 1u];
                const std::uint8_t i2 = sky[face + 2u];
                const std::uint8_t texture = sky[face + 3u];
                if (i0 >= vertexCount || i1 >= vertexCount || i2 >= vertexCount) {
                    return fail(Rac1SkyStatus::InvalidFace, std::move(mesh));
                }
                std::uint32_t material = UINT32_MAX;
                if (textured) {
                    if (texture >= mesh.textures.size()) {
                        return fail(Rac1SkyStatus::InvalidFace, std::move(mesh));
                    }
                    material = texture;
                    ++mesh.texturedTriangleCount;
                } else {
                    if (texture != 0xffu) {
                        return fail(Rac1SkyStatus::InvalidFace, std::move(mesh));
                    }
                    ++mesh.colorTriangleCount;
                }
                auto& out = mesh.batches[batchFor(mesh, material)].triangleVertices;
                // Retail sky indices use the opposite winding from our native
                // host convention, matching Wrench/noclip's import path.
                out.push_back(vertices[i2].vertex);
                out.push_back(vertices[i1].vertex);
                out.push_back(vertices[i0].vertex);
                ++mesh.triangleCount;
            }
        }
    }

    if (mesh.shellCount > 0u && mesh.triangleCount == 0u) {
        return fail(Rac1SkyStatus::EmptySky, std::move(mesh));
    }
    return {Rac1SkyStatus::Ok, std::move(mesh)};
}

} // namespace ratchet::assets
