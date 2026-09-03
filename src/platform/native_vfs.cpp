#include "platform/native_vfs.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <string_view>

namespace ratchet::platform {
namespace {

struct TocSectlen {
    std::uint32_t num = 0u;
    std::uint32_t start = 0u;
    std::uint32_t length = 0u;
};

struct TocNativeLevel {
    std::uint32_t num = 0u;
    std::uint32_t header = 0u;
    std::uint32_t start = 0u;
    std::uint32_t length = 0u;
};

struct TocLocation {
    std::uint32_t num = 0u;
    std::uint32_t start = 0u;
};

std::optional<std::uint32_t> parseUnsignedField(std::string_view object,
                                                std::string_view field) {
    const std::string key = "\"" + std::string(field) + "\"";
    const std::size_t keyPosition = object.find(key);
    if (keyPosition == std::string_view::npos) {
        return std::nullopt;
    }

    const std::size_t colon = object.find(':', keyPosition + key.size());
    if (colon == std::string_view::npos) {
        return std::nullopt;
    }

    std::size_t begin = colon + 1u;
    while (begin < object.size() &&
           (object[begin] == ' ' || object[begin] == '\t' ||
            object[begin] == '\r' || object[begin] == '\n')) {
        ++begin;
    }

    std::size_t end = begin;
    while (end < object.size() && object[end] >= '0' && object[end] <= '9') {
        ++end;
    }
    if (end == begin) {
        return std::nullopt;
    }

    std::uint32_t value = 0u;
    const char* first = object.data() + begin;
    const char* last = object.data() + end;
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        return std::nullopt;
    }
    return value;
}

bool parseSectlenArray(const std::string& json,
                       std::string_view arrayName,
                       std::vector<TocSectlen>& output,
                       std::string& error,
                       bool required = true) {
    const std::string key = "\"" + std::string(arrayName) + "\"";
    const std::size_t keyPosition = json.find(key);
    if (keyPosition == std::string::npos) {
        if (!required) {
            output.clear();
            return true;
        }
        error = "missing array '" + std::string(arrayName) + "'";
        return false;
    }

    const std::size_t arrayBegin = json.find('[', keyPosition + key.size());
    if (arrayBegin == std::string::npos) {
        error = "missing '[' for array '" + std::string(arrayName) + "'";
        return false;
    }

    std::size_t cursor = arrayBegin + 1u;
    while (cursor < json.size()) {
        while (cursor < json.size() &&
               (json[cursor] == ' ' || json[cursor] == '\t' ||
                json[cursor] == '\r' || json[cursor] == '\n' ||
                json[cursor] == ',')) {
            ++cursor;
        }

        if (cursor >= json.size()) {
            break;
        }
        if (json[cursor] == ']') {
            return true;
        }
        if (json[cursor] != '{') {
            error = "unexpected token while parsing array '" +
                    std::string(arrayName) + "'";
            return false;
        }

        const std::size_t objectEnd = json.find('}', cursor + 1u);
        if (objectEnd == std::string::npos) {
            error = "unterminated object in array '" + std::string(arrayName) + "'";
            return false;
        }

        const std::string_view object(json.data() + cursor, objectEnd - cursor + 1u);
        const auto num = parseUnsignedField(object, "num");
        const auto start = parseUnsignedField(object, "start");
        const auto length = parseUnsignedField(object, "length");
        if (!num || !start || !length) {
            error = "invalid sectlen object in array '" + std::string(arrayName) + "'";
            return false;
        }

        output.push_back({*num, *start, *length});
        cursor = objectEnd + 1u;
    }

    error = "unterminated array '" + std::string(arrayName) + "'";
    return false;
}

bool parseNativeLevelArray(const std::string& json,
                           std::string_view arrayName,
                           std::vector<TocNativeLevel>& output,
                           std::string& error,
                           bool required = true) {
    const std::string key = "\"" + std::string(arrayName) + "\"";
    const std::size_t keyPosition = json.find(key);
    if (keyPosition == std::string::npos) {
        if (!required) {
            output.clear();
            return true;
        }
        error = "missing array '" + std::string(arrayName) + "'";
        return false;
    }

    const std::size_t arrayBegin = json.find('[', keyPosition + key.size());
    if (arrayBegin == std::string::npos) {
        error = "missing '[' for array '" + std::string(arrayName) + "'";
        return false;
    }

    std::size_t cursor = arrayBegin + 1u;
    while (cursor < json.size()) {
        while (cursor < json.size() &&
               (json[cursor] == ' ' || json[cursor] == '\t' ||
                json[cursor] == '\r' || json[cursor] == '\n' ||
                json[cursor] == ',')) {
            ++cursor;
        }

        if (cursor >= json.size()) {
            break;
        }
        if (json[cursor] == ']') {
            return true;
        }
        if (json[cursor] != '{') {
            error = "unexpected token while parsing array '" +
                    std::string(arrayName) + "'";
            return false;
        }

        const std::size_t objectEnd = json.find('}', cursor + 1u);
        if (objectEnd == std::string::npos) {
            error = "unterminated object in array '" + std::string(arrayName) + "'";
            return false;
        }

        const std::string_view object(json.data() + cursor, objectEnd - cursor + 1u);
        const auto num = parseUnsignedField(object, "num");
        const auto header = parseUnsignedField(object, "header");
        const auto start = parseUnsignedField(object, "start");
        const auto length = parseUnsignedField(object, "length");
        if (!num || !header || !start || !length) {
            error = "invalid native level object in array '" +
                    std::string(arrayName) + "'";
            return false;
        }

        output.push_back({*num, *header, *start, *length});
        cursor = objectEnd + 1u;
    }

    error = "unterminated array '" + std::string(arrayName) + "'";
    return false;
}

bool parseLocationArray(const std::string& json,
                        std::string_view arrayName,
                        std::vector<TocLocation>& output,
                        std::string& error,
                        bool required = true) {
    const std::string key = "\"" + std::string(arrayName) + "\"";
    const std::size_t keyPosition = json.find(key);
    if (keyPosition == std::string::npos) {
        if (!required) {
            output.clear();
            return true;
        }
        error = "missing array '" + std::string(arrayName) + "'";
        return false;
    }

    const std::size_t arrayBegin = json.find('[', keyPosition + key.size());
    if (arrayBegin == std::string::npos) {
        error = "missing '[' for array '" + std::string(arrayName) + "'";
        return false;
    }

    std::size_t cursor = arrayBegin + 1u;
    while (cursor < json.size()) {
        while (cursor < json.size() &&
               (json[cursor] == ' ' || json[cursor] == '\t' ||
                json[cursor] == '\r' || json[cursor] == '\n' ||
                json[cursor] == ',')) {
            ++cursor;
        }

        if (cursor >= json.size()) {
            break;
        }
        if (json[cursor] == ']') {
            return true;
        }
        if (json[cursor] != '{') {
            error = "unexpected token while parsing array '" +
                    std::string(arrayName) + "'";
            return false;
        }

        const std::size_t objectEnd = json.find('}', cursor + 1u);
        if (objectEnd == std::string::npos) {
            error = "unterminated object in array '" + std::string(arrayName) + "'";
            return false;
        }

        const std::string_view object(json.data() + cursor, objectEnd - cursor + 1u);
        const auto num = parseUnsignedField(object, "num");
        const auto start = parseUnsignedField(object, "start");
        if (!num || !start) {
            error = "invalid location object in array '" + std::string(arrayName) + "'";
            return false;
        }

        output.push_back({*num, *start});
        cursor = objectEnd + 1u;
    }

    error = "unterminated array '" + std::string(arrayName) + "'";
    return false;
}

void appendU32(std::vector<std::uint8_t>& bytes, std::uint32_t value) {
    bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
    bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}

std::filesystem::path assetPath(const std::filesystem::path& root,
                                NativeAssetKind kind,
                                std::uint32_t index) {
    switch (kind) {
    case NativeAssetKind::Wad:
        return root / "wads" / ("wad_" + std::to_string(index) + ".wad");
    case NativeAssetKind::Wad2:
        return root / "wads2" / ("wad2_" + std::to_string(index) + ".wad");
    case NativeAssetKind::Level:
        return root / "levels" / (std::string("level_") +
               (index < 10u ? "0" : "") + std::to_string(index) + ".wad");
    }
    return {};
}

bool readFileRange(const NativeAssetLocation& asset,
                   std::uint64_t byteOffset,
                   std::uint8_t* destination,
                   std::size_t byteCount) {
    if (destination == nullptr || byteCount == 0u) {
        return false;
    }

    std::error_code sizeError;
    const std::uint64_t fileSize = std::filesystem::file_size(asset.path, sizeError);
    if (sizeError || byteOffset > fileSize || byteCount > fileSize - byteOffset) {
        return false;
    }

    std::ifstream input(asset.path, std::ios::binary);
    if (!input) {
        return false;
    }

    input.seekg(static_cast<std::streamoff>(byteOffset), std::ios::beg);
    if (!input) {
        return false;
    }

    input.read(reinterpret_cast<char*>(destination),
               static_cast<std::streamsize>(byteCount));
    return input.good() || input.gcount() == static_cast<std::streamsize>(byteCount);
}

} // namespace

const char* nativeAssetKindName(NativeAssetKind kind) noexcept {
    switch (kind) {
    case NativeAssetKind::Wad:
        return "wads";
    case NativeAssetKind::Wad2:
        return "wads2";
    case NativeAssetKind::Level:
        return "levels";
    }
    return "unknown";
}

std::string nativeAssetName(const NativeAssetLocation& asset) {
    return std::string(nativeAssetKindName(asset.kind)) + "/" +
           std::to_string(asset.index);
}

bool NativeVfs::initialize(const std::filesystem::path& extractedRoot,
                           const std::filesystem::path& tocPath) {
    ready_ = false;
    extractedRoot_ = extractedRoot;
    tocPath_ = tocPath;
    assets_.clear();
    levelAssets_.clear();
    tocImage_.clear();
    tocKnownBytes_ = 0u;
    tocComplete_ = false;
    summary_ = {};

    std::ifstream input(tocPath_, std::ios::binary);
    if (!input) {
        std::cerr << "[OpenRatchet:VFS] missing TOC: " << tocPath_.string() << '\n';
        return false;
    }

    std::ostringstream buffer;
    buffer << input.rdbuf();
    const std::string json = buffer.str();
    if (json.empty()) {
        std::cerr << "[OpenRatchet:VFS] empty TOC: " << tocPath_.string() << '\n';
        return false;
    }

    const auto version = parseUnsignedField(json, "version");
    const auto tocSize = parseUnsignedField(json, "toc_size");
    std::vector<TocSectlen> wads;
    std::vector<TocLocation> vags;
    std::vector<TocSectlen> wads2;
    std::vector<TocSectlen> video;
    std::vector<TocLocation> vags2;
    std::vector<TocSectlen> levels;
    std::vector<TocNativeLevel> nativeLevels;
    std::vector<TocLocation> leveldirs;
    std::string parseError;
    if (!version || !tocSize || *tocSize < 8u ||
        !parseSectlenArray(json, "wads", wads, parseError) ||
        !parseLocationArray(json, "vags", vags, parseError) ||
        !parseSectlenArray(json, "wads2", wads2, parseError) ||
        !parseSectlenArray(json, "video", video, parseError) ||
        !parseLocationArray(json, "vags2", vags2, parseError) ||
        !parseSectlenArray(json, "levels", levels, parseError, false) ||
        !parseNativeLevelArray(json, "native_levels", nativeLevels, parseError, false) ||
        !parseLocationArray(json, "leveldirs", leveldirs, parseError, false)) {
        if (parseError.empty()) {
            parseError = "missing/invalid TOC header";
        }
        std::cerr << "[OpenRatchet:VFS] TOC parse failed: " << parseError
                  << " path=" << tocPath_.string() << '\n';
        return false;
    }

    // Reconstruct the exact in-memory table consumed by the game. The retail
    // tail is 19 SectorRange entries (start,length), not 38 independent level
    // directory locations. Phase-3-era JSON that exposed those raw 38 words as
    // `leveldirs` remains accepted so existing extractions are not invalidated.
    appendU32(tocImage_, *version);
    appendU32(tocImage_, *tocSize);
    for (const TocSectlen& entry : wads) {
        appendU32(tocImage_, entry.start);
        appendU32(tocImage_, entry.length);
    }
    for (const TocLocation& entry : vags) {
        appendU32(tocImage_, entry.start);
    }
    for (const TocSectlen& entry : wads2) {
        appendU32(tocImage_, entry.start);
        appendU32(tocImage_, entry.length);
    }
    for (const TocSectlen& entry : video) {
        appendU32(tocImage_, entry.start);
        appendU32(tocImage_, entry.length);
    }
    for (const TocLocation& entry : vags2) {
        appendU32(tocImage_, entry.start);
    }
    if (!levels.empty()) {
        for (const TocSectlen& entry : levels) {
            appendU32(tocImage_, entry.start);
            appendU32(tocImage_, entry.length);
        }
    } else {
        for (const TocLocation& entry : leveldirs) {
            appendU32(tocImage_, entry.start);
        }
        if (!leveldirs.empty() && leveldirs.size() % 2u == 0u) {
            levels.reserve(leveldirs.size() / 2u);
            for (std::size_t i = 0u; i < leveldirs.size(); i += 2u) {
                levels.push_back({static_cast<std::uint32_t>(i / 2u),
                                  leveldirs[i].start,
                                  leveldirs[i + 1u].start});
            }
        }
    }

    tocKnownBytes_ = tocImage_.size();
    if (tocKnownBytes_ > *tocSize) {
        std::cerr << "[OpenRatchet:VFS] serialized TOC exceeds declared size known=0x"
                  << std::hex << tocKnownBytes_ << " declared=0x" << *tocSize
                  << std::dec << " path=" << tocPath_.string() << '\n';
        tocImage_.clear();
        tocKnownBytes_ = 0u;
        return false;
    }
    tocComplete_ = tocKnownBytes_ == *tocSize;
    tocImage_.resize(*tocSize, 0u);

    const auto append = [this](NativeAssetKind kind,
                               const std::vector<TocSectlen>& entries) {
        for (const TocSectlen& entry : entries) {
            if (entry.start == 0u || entry.length == 0u) {
                continue;
            }
            assets_.push_back({kind,
                               entry.num,
                               entry.start,
                               entry.length,
                               0u,
                               assetPath(extractedRoot_, kind, entry.num)});
        }
    };
    append(NativeAssetKind::Wad, wads);
    append(NativeAssetKind::Wad2, wads2);

    // Raw `levels` entries above are serialized back into the guest-visible TOC.
    // Do not treat those bytes as host-file extents. Native extraction spans are
    // discovered independently from validated 0x2434 amalgamated headers.
    for (const TocNativeLevel& entry : nativeLevels) {
        if (entry.header == 0u || entry.start == 0u || entry.length == 0u) {
            continue;
        }
        const std::uint64_t end =
            static_cast<std::uint64_t>(entry.start) + entry.length;
        if (entry.header < entry.start || entry.header >= end) {
            std::cerr << "[OpenRatchet:VFS] invalid native level span index="
                      << entry.num << " header=0x" << std::hex << entry.header
                      << " span=0x" << entry.start << "+0x" << entry.length
                      << std::dec << '\n';
            continue;
        }
        levelAssets_.push_back({NativeAssetKind::Level,
                                entry.num,
                                entry.start,
                                entry.length,
                                entry.header,
                                assetPath(extractedRoot_,
                                          NativeAssetKind::Level,
                                          entry.num)});
    }

    std::sort(assets_.begin(), assets_.end(),
              [](const NativeAssetLocation& lhs, const NativeAssetLocation& rhs) {
                  if (lhs.startSector != rhs.startSector) {
                      return lhs.startSector < rhs.startSector;
                  }
                  if (lhs.kind != rhs.kind) {
                      return static_cast<unsigned>(lhs.kind) < static_cast<unsigned>(rhs.kind);
                  }
                  return lhs.index < rhs.index;
              });

    for (std::size_t i = 1u; i < assets_.size(); ++i) {
        const NativeAssetLocation& previous = assets_[i - 1u];
        const NativeAssetLocation& current = assets_[i];
        const std::uint64_t previousEnd =
            static_cast<std::uint64_t>(previous.startSector) + previous.sectorCount;
        if (current.startSector < previousEnd) {
            std::cerr << "[OpenRatchet:VFS] overlapping TOC assets: "
                      << nativeAssetName(previous) << " and " << nativeAssetName(current)
                      << '\n';
            assets_.clear();
            return false;
        }
    }

    summary_.indexedAssets = assets_.size();
    for (const NativeAssetLocation& asset : assets_) {
        std::error_code sizeError;
        const std::uint64_t fileSize = std::filesystem::file_size(asset.path, sizeError);
        const std::uint64_t expectedSize =
            static_cast<std::uint64_t>(asset.sectorCount) * kSectorBytes;
        if (!sizeError && fileSize == expectedSize) {
            ++summary_.presentAssets;
        } else {
            ++summary_.missingAssets;
        }
    }

    summary_.indexedLevels = levelAssets_.size();
    for (const NativeAssetLocation& level : levelAssets_) {
        std::error_code sizeError;
        const std::uint64_t fileSize = std::filesystem::file_size(level.path, sizeError);
        const std::uint64_t expectedSize =
            static_cast<std::uint64_t>(level.sectorCount) * kSectorBytes;
        if (!sizeError && fileSize == expectedSize) {
            ++summary_.presentLevels;
        } else {
            ++summary_.missingLevels;
        }
    }

    const NativeAssetLocation* boot = findAsset(NativeAssetKind::Wad2, 0u);
    if (boot == nullptr || !std::filesystem::is_regular_file(boot->path)) {
        std::cerr << "[OpenRatchet:VFS] missing required boot asset wads2/0 under "
                  << extractedRoot_.string() << '\n';
        assets_.clear();
        summary_ = {};
        return false;
    }

    std::error_code bootSizeError;
    const std::uint64_t bootSize = std::filesystem::file_size(boot->path, bootSizeError);
    const std::uint64_t expectedBootSize =
        static_cast<std::uint64_t>(boot->sectorCount) * kSectorBytes;
    std::array<std::uint8_t, 4> bootMagic{};
    std::ifstream bootInput(boot->path, std::ios::binary);
    const bool bootHeaderRead =
        bootInput && static_cast<bool>(bootInput.read(
                         reinterpret_cast<char*>(bootMagic.data()),
                         static_cast<std::streamsize>(bootMagic.size())));
    if (bootSizeError || bootSize != expectedBootSize || !bootHeaderRead ||
        bootMagic != std::array<std::uint8_t, 4>{0x57u, 0x41u, 0x44u, 0x0fu}) {
        std::cerr << "[OpenRatchet:VFS] invalid required boot asset wads2/0 path="
                  << boot->path.string() << " expectedBytes=0x" << std::hex
                  << expectedBootSize << " actualBytes=0x" << bootSize << std::dec << '\n';
        assets_.clear();
        summary_ = {};
        return false;
    }

    ready_ = true;
    std::cerr << "[OpenRatchet:VFS] indexed=" << summary_.indexedAssets
              << " present=" << summary_.presentAssets
              << " missing=" << summary_.missingAssets
              << " toc=" << tocPath_.string() << '\n';
    std::cerr << "[OpenRatchet:VFS] boot asset=" << nativeAssetName(*boot)
              << " sector=0x" << std::hex << boot->startSector
              << " sectors=0x" << boot->sectorCount << std::dec
              << " path=" << boot->path.string() << '\n';
    std::cerr << "[OpenRatchet:VFS] disc TOC bytes=0x" << std::hex
              << tocImage_.size() << " known=0x" << tocKnownBytes_
              << " complete=" << (tocComplete_ ? 1 : 0) << std::dec << '\n';
    if (summary_.indexedLevels != 0u) {
        std::cerr << "[OpenRatchet:VFS] levels indexed=" << summary_.indexedLevels
                  << " present=" << summary_.presentLevels
                  << " missing=" << summary_.missingLevels << '\n';
    }
    return true;
}

const NativeAssetLocation* NativeVfs::findAsset(NativeAssetKind kind,
                                                 std::uint32_t index) const noexcept {
    if (kind == NativeAssetKind::Level) {
        return findLevel(index);
    }
    const auto it = std::find_if(
        assets_.begin(), assets_.end(),
        [kind, index](const NativeAssetLocation& asset) {
            return asset.kind == kind && asset.index == index;
        });
    return it == assets_.end() ? nullptr : &*it;
}

const NativeAssetLocation* NativeVfs::findLevel(
    std::uint32_t index) const noexcept {
    const auto it = std::find_if(
        levelAssets_.begin(), levelAssets_.end(),
        [index](const NativeAssetLocation& level) {
            return level.index == index;
        });
    return it == levelAssets_.end() ? nullptr : &*it;
}

const NativeAssetLocation* NativeVfs::findAssetContainingSector(
    std::uint32_t sector) const noexcept {
    if (assets_.empty()) {
        return nullptr;
    }

    const auto it = std::upper_bound(
        assets_.begin(), assets_.end(), sector,
        [](std::uint32_t value, const NativeAssetLocation& asset) {
            return value < asset.startSector;
        });
    if (it == assets_.begin()) {
        return nullptr;
    }

    const NativeAssetLocation& candidate = *std::prev(it);
    const std::uint64_t end =
        static_cast<std::uint64_t>(candidate.startSector) + candidate.sectorCount;
    return sector < end ? &candidate : nullptr;
}

bool NativeVfs::readSectors(std::uint32_t startSector,
                            std::uint32_t sectorCount,
                            std::uint8_t* destination,
                            std::size_t destinationCapacity,
                            std::string* sourceDescription) const {
    if (!ready_ || destination == nullptr || sectorCount == 0u) {
        return false;
    }

    const std::uint64_t totalBytes64 =
        static_cast<std::uint64_t>(sectorCount) * kSectorBytes;
    if (totalBytes64 > destinationCapacity ||
        totalBytes64 > std::numeric_limits<std::size_t>::max()) {
        return false;
    }

    struct Segment {
        const NativeAssetLocation* asset = nullptr;
        std::uint32_t sectorOffset = 0u;
        std::uint32_t sectorCount = 0u;
    };
    std::vector<Segment> segments;

    std::uint64_t currentSector = startSector;
    std::uint64_t remaining = sectorCount;
    while (remaining != 0u) {
        if (currentSector > std::numeric_limits<std::uint32_t>::max()) {
            return false;
        }

        const NativeAssetLocation* asset =
            findAssetContainingSector(static_cast<std::uint32_t>(currentSector));
        if (asset == nullptr) {
            return false;
        }

        const std::uint64_t assetEnd =
            static_cast<std::uint64_t>(asset->startSector) + asset->sectorCount;
        const std::uint64_t available = assetEnd - currentSector;
        const std::uint64_t take = std::min(remaining, available);
        segments.push_back({asset,
                            static_cast<std::uint32_t>(currentSector - asset->startSector),
                            static_cast<std::uint32_t>(take)});
        currentSector += take;
        remaining -= take;
    }

    // Resolve and read the complete range into staging memory first. The guest
    // destination is changed atomically only after every host-file segment has
    // succeeded, so a missing/corrupt file cannot leave a half-loaded resource.
    std::vector<std::uint8_t> staging(static_cast<std::size_t>(totalBytes64));
    std::size_t destinationOffset = 0u;
    for (const Segment& segment : segments) {
        const std::uint64_t byteOffset =
            static_cast<std::uint64_t>(segment.sectorOffset) * kSectorBytes;
        const std::size_t byteCount =
            static_cast<std::size_t>(segment.sectorCount) * kSectorBytes;
        if (!readFileRange(*segment.asset,
                           byteOffset,
                           staging.data() + destinationOffset,
                           byteCount)) {
            return false;
        }
        destinationOffset += byteCount;
    }
    std::memcpy(destination, staging.data(), staging.size());

    if (sourceDescription != nullptr) {
        if (segments.size() == 1u) {
            *sourceDescription = nativeAssetName(*segments.front().asset);
        } else {
            *sourceDescription = nativeAssetName(*segments.front().asset) + ".." +
                                 nativeAssetName(*segments.back().asset);
        }
    }
    return true;
}

bool NativeVfs::readAssetPrefix(NativeAssetKind kind,
                                std::uint32_t index,
                                std::uint8_t* destination,
                                std::size_t destinationCapacity,
                                std::size_t& bytesRead) const {
    bytesRead = 0u;
    if (!ready_ || destination == nullptr || destinationCapacity == 0u) {
        return false;
    }

    const NativeAssetLocation* asset = findAsset(kind, index);
    if (asset == nullptr) {
        return false;
    }

    std::error_code sizeError;
    const std::uint64_t fileSize = std::filesystem::file_size(asset->path, sizeError);
    if (sizeError || fileSize == 0u) {
        return false;
    }

    const std::size_t wanted = static_cast<std::size_t>(
        std::min<std::uint64_t>(fileSize, destinationCapacity));
    if (!readFileRange(*asset, 0u, destination, wanted)) {
        return false;
    }

    bytesRead = wanted;
    return true;
}

bool NativeVfs::copyDiscToc(std::uint8_t* destination,
                            std::size_t destinationCapacity,
                            std::size_t& bytesWritten) const {
    bytesWritten = 0u;
    if (!ready_ || destination == nullptr || tocImage_.empty() ||
        tocImage_.size() > destinationCapacity) {
        return false;
    }

    std::memcpy(destination, tocImage_.data(), tocImage_.size());
    bytesWritten = tocImage_.size();
    return true;
}

} // namespace ratchet::platform
