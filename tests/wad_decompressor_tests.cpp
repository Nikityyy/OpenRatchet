#include "assets/wad_decompressor.h"

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <array>

namespace {

using ratchet::assets::WadDecompressStatus;
using ratchet::assets::WadDecompressResult;

[[noreturn]] void fail(std::string_view message) {
    std::cerr << "wad_decompressor_tests: " << message << '\n';
    std::exit(1);
}

void writeLe32(std::vector<std::uint8_t>& bytes,
               std::size_t offset,
               std::uint32_t value) {
    bytes[offset + 0u] = static_cast<std::uint8_t>(value & 0xffu);
    bytes[offset + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xffu);
    bytes[offset + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xffu);
    bytes[offset + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xffu);
}

std::vector<std::uint8_t> makeWad(const std::vector<std::uint8_t>& payload) {
    std::vector<std::uint8_t> wad(0x10u + payload.size(), 0u);
    wad[0] = 'W';
    wad[1] = 'A';
    wad[2] = 'D';
    std::copy(payload.begin(), payload.end(), wad.begin() + 0x10u);
    writeLe32(wad, 3u, static_cast<std::uint32_t>(wad.size()));
    return wad;
}

std::string outputString(const std::vector<std::uint8_t>& bytes,
                         std::size_t size) {
    return std::string(reinterpret_cast<const char*>(bytes.data()), size);
}

void requireOk(const WadDecompressResult& result,
               std::size_t expectedWritten,
               std::size_t expectedRead) {
    if (!result.ok()) {
        fail(std::string("expected ok, got ") +
             ratchet::assets::wadDecompressStatusName(result.status));
    }
    if (result.bytesWritten != expectedWritten) {
        fail("unexpected decompressed byte count");
    }
    if (result.bytesRead != expectedRead) {
        fail("unexpected encoded byte count consumed");
    }
}

void testInitialLiteralRun() {
    const auto wad = makeWad({23u, 'A', 'B', 'C', 'D', 'E', 'F'});
    std::vector<std::uint8_t> output(16u, 0xccu);
    const auto result = ratchet::assets::decompressWad(wad, output);
    requireOk(result, 6u, wad.size());
    if (outputString(output, result.bytesWritten) != "ABCDEF") {
        fail("initial literal run decoded incorrectly");
    }
}

void testNormalAndExtendedLiteralRuns() {
    {
        const auto wad = makeWad({3u, 'A', 'B', 'C', 'D', 'E', 'F'});
        std::vector<std::uint8_t> output(16u, 0u);
        const auto result = ratchet::assets::decompressWad(wad, output);
        requireOk(result, 6u, wad.size());
        if (outputString(output, result.bytesWritten) != "ABCDEF") {
            fail("normal literal packet decoded incorrectly");
        }
    }

    std::vector<std::uint8_t> payload{0u, 0u};
    for (char c = 'a'; c <= 'r'; ++c) {
        payload.push_back(static_cast<std::uint8_t>(c));
    }
    const auto wad = makeWad(payload);
    std::vector<std::uint8_t> output(32u, 0u);
    const auto result = ratchet::assets::decompressWad(wad, output);
    requireOk(result, 18u, wad.size());
    if (outputString(output, result.bytesWritten) != "abcdefghijklmnopqr") {
        fail("extended literal packet decoded incorrectly");
    }
}

void testShortAndMediumMatches() {
    {
        // Initial literal "ABC", then a 3-byte short match at distance 3.
        const auto wad = makeWad({20u, 'A', 'B', 'C', 0x48u, 0u});
        std::vector<std::uint8_t> output(16u, 0u);
        const auto result = ratchet::assets::decompressWad(wad, output);
        requireOk(result, 6u, wad.size());
        if (outputString(output, result.bytesWritten) != "ABCABC") {
            fail("short match decoded incorrectly");
        }
    }

    {
        // Initial literal "ABCDEFGH", then length-3 match at distance 3.
        const auto wad = makeWad(
            {25u, 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 0x21u, 0x08u, 0u});
        std::vector<std::uint8_t> output(24u, 0u);
        const auto result = ratchet::assets::decompressWad(wad, output);
        requireOk(result, 11u, wad.size());
        if (outputString(output, result.bytesWritten) != "ABCDEFGHFGH") {
            fail("medium match decoded incorrectly");
        }
    }
}

void testFarMatch() {
    std::vector<std::uint8_t> payload;
    payload.push_back(0xffu); // initial literal run: 255 - 17 = 238 bytes
    payload.insert(payload.end(), 238u, static_cast<std::uint8_t>('A'));

    // Grow the output beyond the 0x4000 minimum far-match distance using
    // overlapping distance-1 medium matches. token=0x20, ext=0xff gives a
    // 288-byte match; low/high zero encode distance 1.
    constexpr std::size_t kMediumMatchBytes = 288u;
    constexpr std::size_t kMediumMatchCount = 57u;
    for (std::size_t i = 0u; i < kMediumMatchCount; ++i) {
        payload.push_back(0x20u);
        payload.push_back(0xffu);
        payload.push_back(0u);
        payload.push_back(0u);
    }

    const std::size_t beforeFar = 238u + kMediumMatchBytes * kMediumMatchCount;
    if (beforeFar <= 0x4001u) {
        fail("far-match fixture did not build enough history");
    }

    // token 0x11 => length code 1 (3 output bytes). low=4/high=0 encodes
    // distance 0x4001 in the guest's far-match branch.
    payload.push_back(0x11u);
    payload.push_back(0x04u);
    payload.push_back(0u);

    const auto wad = makeWad(payload);
    std::vector<std::uint8_t> output(beforeFar + 3u, 0u);
    const auto result = ratchet::assets::decompressWad(wad, output);
    requireOk(result, beforeFar + 3u, wad.size());
    if (!std::all_of(output.begin(), output.end(),
                     [](std::uint8_t value) { return value == 'A'; })) {
        fail("far match decoded incorrectly");
    }
}

void testTrailingLiteralsAndEndMarker() {
    // Short match token low bits encode two trailing literals. The offset byte
    // for short matches is the token itself, matching guest 0x20b8a8.
    const auto wad = makeWad(
        {20u, 'A', 'B', 'C', 0x4au, 0u, 'x', 'y', 0x11u, 0u, 0u});
    std::vector<std::uint8_t> output(24u, 0u);
    const auto result = ratchet::assets::decompressWad(wad, output);
    requireOk(result, 8u, wad.size());
    if (outputString(output, result.bytesWritten) != "ABCABCxy") {
        fail("trailing literals/end marker decoded incorrectly");
    }
}

void testInputBlockMarker() {
    // Guest 0x20b840 treats zero-distance far matches with length != 1 as a
    // source-buffer refill marker. The next bytes begin at the next 0x2000
    // payload boundary.
    std::vector<std::uint8_t> payload(0x2000u + 7u, 0u);
    payload[0] = 20u;
    payload[1] = 'A';
    payload[2] = 'B';
    payload[3] = 'C';
    payload[4] = 0x12u;
    payload[5] = 0u;
    payload[6] = 0u;
    payload[7] = 0xeeu;
    payload[0x2000u + 0u] = 3u;
    payload[0x2000u + 1u] = 'D';
    payload[0x2000u + 2u] = 'E';
    payload[0x2000u + 3u] = 'F';
    payload[0x2000u + 4u] = 'G';
    payload[0x2000u + 5u] = 'H';
    payload[0x2000u + 6u] = 'I';

    const auto wad = makeWad(payload);
    std::vector<std::uint8_t> output(24u, 0u);
    const auto result = ratchet::assets::decompressWad(wad, output);
    requireOk(result, 9u, wad.size());
    if (outputString(output, result.bytesWritten) != "ABCDEFGHI") {
        fail("0x2000 source block marker decoded incorrectly");
    }
}

void testFailuresAreBounded() {
    {
        std::vector<std::uint8_t> bad(0x10u, 0u);
        std::vector<std::uint8_t> output(8u, 0u);
        const auto result = ratchet::assets::decompressWad(bad, output);
        if (result.status != WadDecompressStatus::InvalidMagic) {
            fail("invalid magic was accepted");
        }
    }

    {
        auto wad = makeWad({20u, 'A', 'B', 'C'});
        writeLe32(wad, 3u, static_cast<std::uint32_t>(wad.size() + 1u));
        std::vector<std::uint8_t> output(8u, 0u);
        const auto result = ratchet::assets::decompressWad(wad, output);
        if (result.status != WadDecompressStatus::InvalidEncodedSize) {
            fail("out-of-range encoded size was accepted");
        }
    }

    {
        // Match before any output exists.
        const auto wad = makeWad({0x10u, 0u, 0x04u, 0u});
        std::vector<std::uint8_t> output(8u, 0u);
        const auto result = ratchet::assets::decompressWad(wad, output);
        if (result.status != WadDecompressStatus::InvalidMatch) {
            fail("invalid backward match was accepted");
        }
    }

    {
        const auto wad = makeWad({23u, 'A', 'B', 'C', 'D', 'E', 'F'});
        std::vector<std::uint8_t> output(5u, 0u);
        const auto result = ratchet::assets::decompressWad(wad, output);
        if (result.status != WadDecompressStatus::OutputOverflow) {
            fail("output overflow was not rejected");
        }
    }
}


std::uint32_t fnv1a32(std::span<const std::uint8_t> bytes) {
    std::uint32_t hash = 2166136261u;
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 16777619u;
    }
    return hash;
}

std::uint64_t fnv1a64Update(std::uint64_t hash,
                            std::span<const std::uint8_t> bytes) {
    for (const std::uint8_t byte : bytes) {
        hash ^= byte;
        hash *= 1099511628211ull;
    }
    return hash;
}

void fnv1a64U64(std::uint64_t& hash, std::uint64_t value) {
    std::array<std::uint8_t, 8> bytes{};
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>((value >> (8u * i)) & 0xffu);
    }
    hash = fnv1a64Update(hash, bytes);
}

std::vector<std::uint8_t> readFile(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) {
        fail(std::string("unable to open fixture: ") + path.string());
    }
    const std::streamsize size = file.tellg();
    if (size < 0) {
        fail(std::string("unable to size fixture: ") + path.string());
    }
    file.seekg(0, std::ios::beg);
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size));
    if (!bytes.empty() &&
        !file.read(reinterpret_cast<char*>(bytes.data()), size)) {
        fail(std::string("unable to read fixture: ") + path.string());
    }
    return bytes;
}

void testRealBootWadFixture(const std::filesystem::path& path) {
    constexpr std::size_t kExpectedFileBytes = 0x51000u;
    constexpr std::uint32_t kExpectedEncodedBytes = 0x50e0fu;
    constexpr std::uint32_t kExpectedEncodedHash = 0xb90bb3e3u;
    constexpr std::size_t kExpectedOutputBytes = 0xa346cu;
    constexpr std::uint32_t kExpectedOutputHash = 0xd3cb9822u;

    const std::vector<std::uint8_t> wad = readFile(path);
    if (wad.size() != kExpectedFileBytes) {
        fail("boot WAD fixture file size does not match the target retail asset");
    }
    if (wad.size() < 0x10u || wad[0] != 'W' || wad[1] != 'A' || wad[2] != 'D') {
        fail("boot WAD fixture has invalid magic");
    }
    const std::uint32_t encodedBytes =
        static_cast<std::uint32_t>(wad[3]) |
        (static_cast<std::uint32_t>(wad[4]) << 8u) |
        (static_cast<std::uint32_t>(wad[5]) << 16u) |
        (static_cast<std::uint32_t>(wad[6]) << 24u);
    if (encodedBytes != kExpectedEncodedBytes || encodedBytes > wad.size()) {
        fail("boot WAD fixture encoded size does not match the target retail asset");
    }
    if (fnv1a32(std::span<const std::uint8_t>(wad.data(), encodedBytes)) !=
        kExpectedEncodedHash) {
        fail("boot WAD fixture fingerprint does not match the target retail asset");
    }

    std::vector<std::uint8_t> output(kExpectedOutputBytes, 0u);
    const auto result = ratchet::assets::decompressWad(wad, output);
    requireOk(result, kExpectedOutputBytes, kExpectedEncodedBytes);
    if (fnv1a32(output) != kExpectedOutputHash) {
        fail("boot WAD native output does not match the independent reference hash");
    }

    // The authentic decompressed boot WAD starts with a 0xc0-byte directory of
    // 24 (offset,size) pairs. Every populated payload is 0x40-aligned and the
    // final populated entry ends exactly at the native output size.
    if (output.size() < 0xc0u ||
        (static_cast<std::uint32_t>(output[0]) |
         (static_cast<std::uint32_t>(output[1]) << 8u) |
         (static_cast<std::uint32_t>(output[2]) << 16u) |
         (static_cast<std::uint32_t>(output[3]) << 24u)) != 0xc0u) {
        fail("boot WAD output directory header is not structurally valid");
    }

    std::size_t previousEnd = 0xc0u;
    std::size_t populated = 0u;
    for (std::size_t i = 0u; i < 24u; ++i) {
        const std::size_t at = i * 8u;
        const std::uint32_t offset =
            static_cast<std::uint32_t>(output[at + 0u]) |
            (static_cast<std::uint32_t>(output[at + 1u]) << 8u) |
            (static_cast<std::uint32_t>(output[at + 2u]) << 16u) |
            (static_cast<std::uint32_t>(output[at + 3u]) << 24u);
        const std::uint32_t size =
            static_cast<std::uint32_t>(output[at + 4u]) |
            (static_cast<std::uint32_t>(output[at + 5u]) << 8u) |
            (static_cast<std::uint32_t>(output[at + 6u]) << 16u) |
            (static_cast<std::uint32_t>(output[at + 7u]) << 24u);
        if (offset == 0u && size == 0u) {
            continue;
        }
        if (offset == 0u || size == 0u || (offset & 0x3fu) != 0u ||
            offset < previousEnd ||
            static_cast<std::uint64_t>(offset) + size > output.size()) {
            fail("boot WAD output directory contains an invalid payload range");
        }
        previousEnd = static_cast<std::size_t>(offset) + size;
        ++populated;
    }
    if (populated != 21u || previousEnd != output.size()) {
        fail("boot WAD output directory does not cover the authentic payload");
    }
}

void testRealWads2Corpus(const std::filesystem::path& directory) {
    constexpr std::size_t kExpectedFileCount = 165u;
    constexpr std::size_t kExpectedStreamCount = 249u;
    constexpr std::uint64_t kExpectedOutputBytes = 0x020f312cull;
    constexpr std::size_t kExpectedLargestOutput = 0x7318c0u;
    constexpr std::uint64_t kExpectedAggregateHash = 0x3afb362dea4dc735ull;
    constexpr std::size_t kOutputCapacity = 0x00800000u;

    std::vector<std::filesystem::path> paths;
    for (const auto& entry : std::filesystem::directory_iterator(directory)) {
        if (entry.is_regular_file() && entry.path().extension() == ".wad") {
            paths.push_back(entry.path());
        }
    }
    std::sort(paths.begin(), paths.end(), [](const auto& a, const auto& b) {
        return a.filename().generic_string() < b.filename().generic_string();
    });
    if (paths.size() != kExpectedFileCount) {
        fail("wads2 corpus file count does not match the target retail extraction");
    }

    std::size_t streamCount = 0u;
    std::uint64_t totalOutputBytes = 0u;
    std::size_t largestOutput = 0u;
    std::uint64_t aggregate = 14695981039346656037ull;
    std::vector<std::uint8_t> output(kOutputCapacity, 0u);

    for (const auto& path : paths) {
        const std::vector<std::uint8_t> bytes = readFile(path);
        for (std::size_t offset = 0u; offset + 0x10u <= bytes.size();
             offset += 0x40u) {
            if (bytes[offset + 0u] != 'W' || bytes[offset + 1u] != 'A' ||
                bytes[offset + 2u] != 'D') {
                continue;
            }

            std::fill(output.begin(), output.end(), 0u);
            const auto result = ratchet::assets::decompressWad(
                std::span<const std::uint8_t>(bytes.data() + offset,
                                              bytes.size() - offset),
                output);
            if (!result.ok()) {
                fail(std::string("native decoder rejected real WAD stream in ") +
                     path.filename().string());
            }
            if (result.bytesWritten > output.size()) {
                fail("real WAD corpus output exceeded the regression-test capacity");
            }

            ++streamCount;
            totalOutputBytes += result.bytesWritten;
            largestOutput = std::max(largestOutput, result.bytesWritten);
            fnv1a64U64(aggregate, offset);
            fnv1a64U64(aggregate, result.bytesWritten);
            aggregate = fnv1a64Update(
                aggregate,
                std::span<const std::uint8_t>(output.data(), result.bytesWritten));
        }
    }

    if (streamCount != kExpectedStreamCount ||
        totalOutputBytes != kExpectedOutputBytes ||
        largestOutput != kExpectedLargestOutput ||
        aggregate != kExpectedAggregateHash) {
        fail("wads2 corpus output does not match the independent reference manifest");
    }
}

} // namespace

int main(int argc, char** argv) {
    testInitialLiteralRun();
    testNormalAndExtendedLiteralRuns();
    testShortAndMediumMatches();
    testFarMatch();
    testTrailingLiteralsAndEndMarker();
    testInputBlockMarker();
    testFailuresAreBounded();

    for (int i = 1; i < argc; ++i) {
        const std::string_view arg(argv[i]);
        if (arg == "--boot" && i + 1 < argc) {
            testRealBootWadFixture(argv[++i]);
        } else if (arg == "--corpus" && i + 1 < argc) {
            testRealWads2Corpus(argv[++i]);
        } else {
            fail("invalid command-line fixture argument");
        }
    }

    std::cout << "wad_decompressor_tests: ok\n";
    return 0;
}
