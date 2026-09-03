#include "assets/rac1_static_scene.h"

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

constexpr std::size_t kTieClassEntryBytes = 0x20u;
constexpr std::size_t kShrubClassEntryBytes = 0x30u;
constexpr std::size_t kTieInstanceBytes = 0xe0u;
constexpr std::size_t kShrubInstanceBytes = 0x70u;
constexpr std::size_t kInstanceBlockHeaderBytes = 0x10u;
constexpr std::size_t kTiePacketHeaderBytes = 0x10u;
constexpr std::size_t kTieRegularVertexBytes = 0x10u;
constexpr std::size_t kTieMorphVertexBytes = 0x18u;
constexpr std::size_t kShrubClassHeaderBytes = 0x40u;
constexpr std::size_t kMaxCount = 1u << 20u;
constexpr float kFixed12 = 1.0f / 4096.0f;

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
    const std::uint32_t u = readU32(b, o);
    float f = 0.0f;
    static_assert(sizeof(f) == sizeof(u));
    std::memcpy(&f, &u, sizeof(f));
    return f;
}
bool fits(std::size_t o, std::size_t n, std::size_t cap) noexcept {
    return o <= cap && n <= cap - o;
}
bool mul(std::size_t a, std::size_t b, std::size_t& out) noexcept {
    if (b != 0u && a > std::numeric_limits<std::size_t>::max() / b) return false;
    out = a * b;
    return true;
}
struct ClassEntry {
    std::uint32_t offset = 0u;
    std::int32_t oClass = 0;
    std::vector<std::uint8_t> textures;
};

bool parseClassEntries(std::span<const std::uint8_t> index,
                       Rac1ArrayRange range,
                       std::size_t stride,
                       std::unordered_map<std::int32_t, ClassEntry>& out) {
    std::size_t bytes = 0u;
    if (range.count > kMaxCount || !mul(range.count, stride, bytes) ||
        !fits(range.offset, bytes, index.size())) return false;
    for (std::uint32_t i = 0u; i < range.count; ++i) {
        const std::size_t o = static_cast<std::size_t>(range.offset) + i * stride;
        ClassEntry e{};
        e.offset = readU32(index, o + 0x0u);
        e.oClass = readI32(index, o + 0x4u);
        for (std::size_t t = 0u; t < 16u; ++t) {
            const std::uint8_t v = index[o + 0x10u + t];
            if (v == 0xffu) break;
            e.textures.push_back(v);
        }
        out[e.oClass] = std::move(e);
    }
    return true;
}

struct MatrixInstance {
    std::int32_t oClass = 0;
    std::array<float, 16> matrix{};
    std::array<std::uint8_t, 4> color{255u,255u,255u,255u};
};

bool parseInstanceBlock(std::span<const std::uint8_t> gameplay,
                        std::uint32_t offset,
                        std::size_t stride,
                        bool shrub,
                        std::vector<MatrixInstance>& out) {
    if (offset == 0u) return true;
    if (!fits(offset, kInstanceBlockHeaderBytes, gameplay.size())) return false;
    const std::int32_t countSigned = readI32(gameplay, offset);
    if (countSigned < 0 || static_cast<std::size_t>(countSigned) > kMaxCount) return false;
    const std::size_t count = static_cast<std::size_t>(countSigned);
    std::size_t bytes = 0u;
    if (!mul(count, stride, bytes) || !fits(offset + kInstanceBlockHeaderBytes, bytes, gameplay.size())) return false;
    out.reserve(count);
    for (std::size_t i = 0u; i < count; ++i) {
        const std::size_t o = static_cast<std::size_t>(offset) + kInstanceBlockHeaderBytes + i * stride;
        MatrixInstance inst{};
        inst.oClass = readI32(gameplay, o);
        for (std::size_t m = 0u; m < 16u; ++m) inst.matrix[m] = readF32(gameplay, o + 0x10u + m * 4u);
        if (shrub) {
            inst.color = {gameplay[o + 0x50u], gameplay[o + 0x51u], gameplay[o + 0x52u], 255u};
        }
        out.push_back(inst);
    }
    return true;
}

Rac1StaticVertex transform(float x, float y, float z, float u, float v,
                           const MatrixInstance& inst, float classScale) {
    x *= classScale; y *= classScale; z *= classScale;
    Rac1StaticVertex out{};
    out.x = inst.matrix[0]*x + inst.matrix[4]*y + inst.matrix[8]*z + inst.matrix[12];
    out.y = inst.matrix[1]*x + inst.matrix[5]*y + inst.matrix[9]*z + inst.matrix[13];
    out.z = inst.matrix[2]*x + inst.matrix[6]*y + inst.matrix[10]*z + inst.matrix[14];
    out.u = u; out.v = v;
    out.r = inst.color[0]; out.g = inst.color[1]; out.b = inst.color[2]; out.a = inst.color[3];
    return out;
}

std::size_t batchFor(Rac1StaticSceneMesh& mesh, Rac1StaticMaterialKind kind, std::uint32_t material) {
    for (std::size_t i = 0u; i < mesh.batches.size(); ++i)
        if (mesh.batches[i].kind == kind && mesh.batches[i].materialIndex == material) return i;
    mesh.batches.push_back({kind, material, {}});
    return mesh.batches.size() - 1u;
}

struct TieVertexRaw {
    std::int16_t x=0,y=0,z=0;
    std::uint16_t s=0,t=0;
    std::uint16_t write=0, write2=0;
};
struct TiePrimitive { std::int32_t material=-1; std::vector<TieVertexRaw> vertices; };

bool decodeTieClass(std::span<const std::uint8_t> core, const ClassEntry& ce,
                    const MatrixInstance& inst, std::uint32_t textureCount,
                    Rac1StaticSceneMesh& mesh) {
    const std::size_t base = ce.offset;
    if (!fits(base, 0x70u, core.size())) return false;

    // Retail R&C1 tie positions are int16 values multiplied by class.scale/1024.
    const float classScale = readF32(core, base + 0x40u) / 1024.0f;
    if (!std::isfinite(classScale) || classScale == 0.0f) return false;

    const std::uint32_t packetOffset = readU32(core, base + 0x00u); // LOD0 packet table
    const std::uint8_t packetCount = core[base + 0x20u];
    std::size_t headersBytes = 0u;
    if (!mul(packetCount, kTiePacketHeaderBytes, headersBytes) ||
        !fits(base + packetOffset, headersBytes, core.size())) {
        return false;
    }

    for (std::size_t pi = 0u; pi < packetCount; ++pi) {
        const std::size_t ph = base + packetOffset + pi * kTiePacketHeaderBytes;
        const std::int32_t dataRel = readI32(core, ph + 0x0u);
        const std::uint8_t vertOffsetQw = core[ph + 0x8u];
        const std::uint8_t vertSizeQw = core[ph + 0x9u];
        if (dataRel < 0 || vertSizeQw == 0u) return false;

        const std::size_t body = base + packetOffset + static_cast<std::size_t>(dataRel);
        if (!fits(body, 0x2cu, core.size())) return false;

        std::array<std::int32_t, 4> dest{};
        std::array<std::int32_t, 4> src{};
        for (std::size_t i = 0u; i < 4u; ++i) {
            dest[i] = readI32(core, body + i * 4u);
            src[i] = readI32(core, body + 0x10u + i * 4u);
        }

        const std::uint8_t stripCount = core[body + 0x23u];
        if (!fits(body + 0x2cu, static_cast<std::size_t>(stripCount) * 4u, core.size())) {
            return false;
        }
        struct Strip {
            std::uint8_t vertexCount = 0u;
            std::uint8_t gifOffset = 0u;
        };
        std::vector<Strip> strips;
        strips.reserve(stripCount);
        for (std::size_t i = 0u; i < stripCount; ++i) {
            const std::size_t o = body + 0x2cu + i * 4u;
            strips.push_back({core[o], core[o + 2u]});
        }

        // TiePacketHeader::vertOffset/vertSize describe the actual serialized
        // vertex buffer. This matters on retail classes where the source
        // buffer is not simply "the bytes immediately after the strip table".
        const std::size_t vertexRel = static_cast<std::size_t>(vertOffsetQw) * 0x10u;
        const std::size_t vertexBytes = static_cast<std::size_t>(vertSizeQw) * 0x10u;
        if (!fits(body, vertexRel, core.size()) || !fits(body + vertexRel, vertexBytes, core.size())) {
            return false;
        }
        const std::size_t vbase = body + vertexRel;

        // The retail R&C1 VU header contains explicit regular/morphing
        // vertex counts at +0x2a/+0x2b.  vert_size is the serialized buffer
        // allocation and may include tail padding; deriving the morph count
        // from every remaining byte incorrectly turns that padding into fake
        // 0x18-byte vertices on authentic classes.
        const std::size_t regularCount = core[body + 0x2au];
        const std::size_t morphCount = core[body + 0x2bu];
        std::size_t regularBytes = 0u;
        std::size_t morphBytes = 0u;
        if (!mul(regularCount, kTieRegularVertexBytes, regularBytes) ||
            !mul(morphCount, kTieMorphVertexBytes, morphBytes) ||
            regularBytes > vertexBytes || morphBytes > vertexBytes - regularBytes) {
            return false;
        }

        std::vector<TieVertexRaw> raw;
        raw.reserve((regularCount + morphCount) * 2u);
        auto addRaw = [&](TieVertexRaw vertex) {
            raw.push_back(vertex);
            if (vertex.write2 != 0u && vertex.write2 != vertex.write) {
                vertex.write = vertex.write2;
                raw.push_back(vertex);
            }
        };
        for (std::size_t i = 0u; i < regularCount; ++i) {
            const std::size_t o = vbase + i * kTieRegularVertexBytes;
            addRaw({readI16(core, o + 0x0u),
                    readI16(core, o + 0x2u),
                    readI16(core, o + 0x4u),
                    readU16(core, o + 0x8u),
                    readU16(core, o + 0xau),
                    readU16(core, o + 0x6u),
                    readU16(core, o + 0xeu)});
        }
        const std::size_t morphBase = vbase + regularBytes;
        for (std::size_t i = 0u; i < morphCount; ++i) {
            const std::size_t o = morphBase + i * kTieMorphVertexBytes;
            addRaw({readI16(core, o + 0x8u),
                    readI16(core, o + 0xau),
                    readI16(core, o + 0xcu),
                    readU16(core, o + 0x10u),
                    readU16(core, o + 0x12u),
                    readU16(core, o + 0x6u),
                    readU16(core, o + 0x16u)});
        }

        // Retail packets intentionally contain duplicate padding vertices
        // (minimum four regular vertices). The PS2 writes them to the same GS
        // packet address, so they overwrite rather than creating extra
        // commands. Sort by destination and collapse duplicates before replay.
        std::sort(raw.begin(), raw.end(), [](const TieVertexRaw& a, const TieVertexRaw& b) {
            return a.write < b.write;
        });
        raw.erase(std::unique(raw.begin(), raw.end(), [](const TieVertexRaw& a, const TieVertexRaw& b) {
            return a.write == b.write;
        }), raw.end());

        if (src[0] < 0 || src[0] % 0x50 != 0) return false;
        std::int32_t material = src[0] / 0x50;
        std::size_t nextStrip = 0u;
        std::size_t nextVertex = 0u;
        std::size_t nextAdGif = 1u;
        int nextOffset = 6; // first AD-GIF is implicitly written at GS slot 0

        struct Primitive {
            std::int32_t material = -1;
            std::uint8_t expectedVertices = 0u;
            std::vector<TieVertexRaw> vertices;
        };
        std::vector<Primitive> primitives;
        Primitive* active = nullptr;

        std::size_t guard = 0u;
        while ((nextStrip < strips.size() || nextVertex < raw.size()) && guard++ < 0x1000u) {
            if (nextStrip < strips.size() && strips[nextStrip].gifOffset == nextOffset) {
                active = &primitives.emplace_back();
                active->material = material;
                active->expectedVertices = strips[nextStrip].vertexCount;
                active->vertices.reserve(active->expectedVertices);
                ++nextStrip;
                nextOffset += 1;
                continue;
            }

            if (nextVertex < raw.size() && raw[nextVertex].write == nextOffset) {
                if (active == nullptr) return false;
                active->vertices.push_back(raw[nextVertex]);
                ++nextVertex;
                nextOffset += 3;
                continue;
            }

            if (nextAdGif < src.size() && dest[nextAdGif - 1u] == nextOffset) {
                if (src[nextAdGif] < 0 || src[nextAdGif] % 0x50 != 0) return false;
                material = src[nextAdGif] / 0x50;
                ++nextAdGif;
                nextOffset += 6;
                continue;
            }

            return false;
        }
        if (guard >= 0x1000u || nextStrip != strips.size() || nextVertex != raw.size()) return false;

        for (const Primitive& primitive : primitives) {
            // The GS-address replay is authoritative. The strip's stored count
            // is useful as a sanity check, but padded/overwritten source verts
            // must not be counted twice.
            if (primitive.vertices.size() < 3u ||
                (primitive.expectedVertices != 0u && primitive.vertices.size() != primitive.expectedVertices)) {
                return false;
            }
            if (primitive.material < 0 ||
                static_cast<std::size_t>(primitive.material) >= ce.textures.size()) {
                return false;
            }
            const std::uint32_t global = ce.textures[static_cast<std::size_t>(primitive.material)];
            if (global >= textureCount) return false;
            auto& out = mesh.batches[batchFor(mesh, Rac1StaticMaterialKind::Tie, global)]
                            .triangleVertices;
            for (std::size_t i = 2u; i < primitive.vertices.size(); ++i) {
                const TieVertexRaw* a = &primitive.vertices[i - 2u];
                const TieVertexRaw* b = &primitive.vertices[i - 1u];
                const TieVertexRaw* c = &primitive.vertices[i];
                if ((i & 1u) == 0u) std::swap(a, c);
                for (const TieVertexRaw* vertex : {a, b, c}) {
                    out.push_back(transform(vertex->x,
                                            vertex->y,
                                            vertex->z,
                                            vertex->s * kFixed12,
                                            vertex->t * kFixed12,
                                            inst,
                                            classScale));
                }
                ++mesh.tieTriangleCount;
            }
        }
    }
    return true;
}

struct Unpack { std::size_t payload=0, bytes=0; std::uint8_t vnvl=0; std::uint16_t num=0; };
bool parseShrubVif(std::span<const std::uint8_t> b, std::vector<Unpack>& out){
    std::size_t o=0; while(o<b.size()){
        if(!fits(o,4,b.size())) return false;
        const auto w=readU32(b,o); std::uint8_t cmd=(w>>24u)&0x7fu; std::uint16_t num=(w>>16u)&0xffu; if(num==0)num=256;
        std::size_t words=1;
        if((cmd&0x60u)==0x60u){ std::size_t vn=((cmd&0x0cu)>>2u)+1u, vl=cmd&3u, bits=(32u>>vl)*vn*static_cast<std::size_t>(num); std::size_t pb=(bits+7u)/8u; words=1u+(bits+31u)/32u; out.push_back({o+4u,pb,static_cast<std::uint8_t>(cmd&0xfu),num}); }
        else if(cmd==0x20u) words=2u; else if(cmd==0x30u||cmd==0x31u) words=5u;
        else if(cmd==0x4au) words=1u+static_cast<std::size_t>(num)*2u;
        else if(cmd==0x50u||cmd==0x51u){ std::size_t q=w&0xffffu; if(q==0)q=65536u; words=1u+q*4u; }
        else { switch(cmd){case 0x00:case 0x01:case 0x02:case 0x03:case 0x04:case 0x05:case 0x06:case 0x07:case 0x10:case 0x11:case 0x13:case 0x14:case 0x15:case 0x17: break; default:return false;} }
        const std::size_t cb=words*4u; if(!fits(o,cb,b.size()))return false; o+=cb;
    } return true;
}

bool decodeShrubClass(std::span<const std::uint8_t> core, const ClassEntry& ce,
                      const MatrixInstance& inst, std::uint32_t textureCount,
                      Rac1StaticSceneMesh& mesh){
    const std::size_t base=ce.offset; if(!fits(base,kShrubClassHeaderBytes,core.size()))return false;
    const float scale=readF32(core,base+0x20u)/1024.0f; const std::int16_t pc=readI16(core,base+0x28u);
    if(!std::isfinite(scale)||scale==0.0f||pc<0||pc>4096)return false;
    if(!fits(base+kShrubClassHeaderBytes,static_cast<std::size_t>(pc)*8u,core.size()))return false;
    for(int pi=0;pi<pc;pi++){
        const std::size_t pe=base+kShrubClassHeaderBytes+static_cast<std::size_t>(pi)*8u;
        const std::int32_t rel=readI32(core,pe), sz=readI32(core,pe+4u); if(rel<0||sz<=0||!fits(base+static_cast<std::size_t>(rel),static_cast<std::size_t>(sz),core.size()))return false;
        auto packet=core.subspan(base+static_cast<std::size_t>(rel),static_cast<std::size_t>(sz)); std::vector<Unpack> u; if(!parseShrubVif(packet,u)||u.size()!=3u)return false;
        for(auto& x:u) if(!fits(x.payload,x.bytes,packet.size()))return false;
        auto h=packet.subspan(u[0].payload,u[0].bytes), p1=packet.subspan(u[1].payload,u[1].bytes), p2=packet.subspan(u[2].payload,u[2].bytes);
        if (h.size() < 0x10u) return false;
        const std::int32_t tc = readI32(h, 0u);
        const std::int32_t gc = readI32(h, 4u);
        const std::int32_t vc = readI32(h, 8u);
        if (tc < 0 || gc < 0 || vc < 0 || vc > 65536) return false;
        const std::size_t tagsEnd=0x10u+static_cast<std::size_t>(gc)*0x10u, matsEnd=tagsEnd+static_cast<std::size_t>(tc)*0x40u;
        if(matsEnd>h.size()||static_cast<std::size_t>(vc)*8u>p1.size()||static_cast<std::size_t>(vc)*8u>p2.size())return false;
        struct G{int off;int type;}; struct M{int off;int mat;}; struct V{int off;std::int16_t x,y,z,s,t;};
        std::vector<G> gs; std::vector<M> ms; std::vector<V> vs;
        for(int i=0;i<gc;i++){std::size_t o=0x10u+static_cast<std::size_t>(i)*0x10u; const std::uint32_t high=readU32(h,o+4u); int prim=(high>>15u)&7u; if(prim!=3&&prim!=4)return false; gs.push_back({readI32(h,o+0xc),prim});}
        for(int i=0;i<tc;i++){std::size_t o=tagsEnd+static_cast<std::size_t>(i)*0x40u; /* Retail shrub packets repurpose TEX0.low as the class-local texture slot; ClassEntry::textures maps it to the global shrub texture table. */ ms.push_back({readI32(h,o+0x0cu),readI32(h,o+0x30u)});}
        for(int i=0;i<vc;i++){std::size_t o=static_cast<std::size_t>(i)*8u; vs.push_back({readI16(p1,o+6u),readI16(p1,o),readI16(p1,o+2),readI16(p1,o+4),readI16(p2,o),readI16(p2,o+2)});}
        std::size_t ig=0,im=0,iv=0; int next=0, primType=4, material=-1; std::vector<V> pv; auto flush=[&](){ if(pv.size()<3||material<0){pv.clear();return true;} if(static_cast<std::size_t>(material)>=ce.textures.size())return false; std::uint32_t global=ce.textures[material]; if(global>=textureCount)return false; auto& out=mesh.batches[batchFor(mesh,Rac1StaticMaterialKind::Shrub,global)].triangleVertices; if(primType==3){for(std::size_t i=0;i+2<pv.size();i+=3){for(int j=0;j<3;j++){auto&v=pv[i+j];out.push_back(transform(v.x,v.y,v.z,v.s*kFixed12,v.t*kFixed12,inst,scale));}++mesh.shrubTriangleCount;}} else {for(std::size_t i=2;i<pv.size();i++){const V *a=&pv[i-2],*b=&pv[i-1],*c=&pv[i];if((i&1u)==0u)std::swap(a,c);for(auto*q:{a,b,c})out.push_back(transform(q->x,q->y,q->z,q->s*kFixed12,q->t*kFixed12,inst,scale));++mesh.shrubTriangleCount;}} pv.clear();return true;};
        std::size_t guard=0; while((ig<gs.size()||im<ms.size()||iv<vs.size())&&guard++<100000u){
            if(ig<gs.size()&&gs[ig].off==next){if(!flush())return false;primType=gs[ig++].type;next+=1;continue;}
            if(im<ms.size()&&ms[im].off==next){if(!flush())return false;material=ms[im++].mat;next+=5;continue;}
            if(iv<vs.size()&&vs[iv].off==next){pv.push_back(vs[iv++]);next+=3;continue;}
            if (iv < vs.size() && vs[iv].off == next - 3) break;
            return false;
        } if(!flush())return false;
    } return true;
}

Rac1StaticSceneResult fail(Rac1StaticSceneStatus s, Rac1StaticSceneMesh m={}) { return {s,std::move(m)}; }
} // namespace

const char* rac1StaticSceneStatusName(Rac1StaticSceneStatus s) noexcept {
    switch(s){case Rac1StaticSceneStatus::Ok:return "ok";case Rac1StaticSceneStatus::InvalidIndexTable:return "invalid-index-table";case Rac1StaticSceneStatus::InvalidGameplayHeader:return "invalid-gameplay-header";case Rac1StaticSceneStatus::InvalidInstanceBlock:return "invalid-instance-block";case Rac1StaticSceneStatus::MissingClass:return "missing-class";case Rac1StaticSceneStatus::InvalidClass:return "invalid-class";case Rac1StaticSceneStatus::InvalidPacket:return "invalid-packet";case Rac1StaticSceneStatus::InvalidMaterial:return "invalid-material";case Rac1StaticSceneStatus::EmptyScene:return "empty-scene";} return "unknown";
}

Rac1StaticSceneResult decodeRac1StaticScene(std::span<const std::uint8_t> core,
    std::span<const std::uint8_t> coreIndex,std::span<const std::uint8_t> gameplay,
    Rac1ArrayRange tieClasses,Rac1ArrayRange shrubClasses,std::uint32_t tieTextureCount,std::uint32_t shrubTextureCount){
    Rac1StaticSceneMesh mesh{}; mesh.tieClassCount=tieClasses.count; mesh.shrubClassCount=shrubClasses.count;
    std::unordered_map<std::int32_t,ClassEntry> ties,shrubs;
    if(!parseClassEntries(coreIndex,tieClasses,kTieClassEntryBytes,ties)||!parseClassEntries(coreIndex,shrubClasses,kShrubClassEntryBytes,shrubs))return fail(Rac1StaticSceneStatus::InvalidIndexTable);
    if(gameplay.size()<0x90u)return fail(Rac1StaticSceneStatus::InvalidGameplayHeader);
    const std::int32_t tieOfs=readI32(gameplay,0x34u), shrubOfs=readI32(gameplay,0x3cu); if(tieOfs<0||shrubOfs<0)return fail(Rac1StaticSceneStatus::InvalidGameplayHeader);
    std::vector<MatrixInstance> ti,si; if(!parseInstanceBlock(gameplay,tieOfs,kTieInstanceBytes,false,ti)||!parseInstanceBlock(gameplay,shrubOfs,kShrubInstanceBytes,true,si))return fail(Rac1StaticSceneStatus::InvalidInstanceBlock);
    mesh.tieInstanceCount=ti.size(); mesh.shrubInstanceCount=si.size();
    for(const auto& inst:ti){auto it=ties.find(inst.oClass);if(it==ties.end())return fail(Rac1StaticSceneStatus::MissingClass,std::move(mesh));if(!decodeTieClass(core,it->second,inst,tieTextureCount,mesh))return fail(Rac1StaticSceneStatus::InvalidClass,std::move(mesh));}
    for(const auto& inst:si){auto it=shrubs.find(inst.oClass);if(it==shrubs.end())return fail(Rac1StaticSceneStatus::MissingClass,std::move(mesh));if(!decodeShrubClass(core,it->second,inst,shrubTextureCount,mesh))return fail(Rac1StaticSceneStatus::InvalidClass,std::move(mesh));}
    if(mesh.tieInstanceCount+mesh.shrubInstanceCount>0u && mesh.tieTriangleCount+mesh.shrubTriangleCount==0u)return fail(Rac1StaticSceneStatus::EmptyScene,std::move(mesh));
    return {Rac1StaticSceneStatus::Ok,std::move(mesh)};
}

} // namespace ratchet::assets
