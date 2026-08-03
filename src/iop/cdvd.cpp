#include "openratchet/iop.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <future>
#include <chrono>
#include <limits>
#include <mutex>
#include <cctype>

namespace OpenRatchet {
namespace IOP {

struct FileRecord {
    std::string name;
    uint32_t lsn;
    uint32_t size;
    uint32_t sector_count;
    int priority;
};

struct ReadChunk {
    std::string file_path;
    uint64_t file_offset;
    uint32_t file_bytes;
    uint32_t zero_bytes;
    uint32_t dest_buffer_addr;
};

static std::vector<FileRecord> g_files;
static std::future<int> g_pendingRead;
static int g_cdError = 0;
static std::mutex g_cdMutex;

class CDVD_Module : public IOP_Module {
public:
    void Init() override {}
    uint32_t Dispatch(uint32_t func, uint32_t send_addr, uint32_t send_size, uint32_t recv_addr, uint32_t recv_size, EE_Memory* mem) override {
        if (send_size < 12) return static_cast<uint32_t>(-1);
        const uint32_t lsn = mem->Read<uint32_t>(send_addr);
        const uint32_t sectors = mem->Read<uint32_t>(send_addr + 4);
        const uint32_t buffer = mem->Read<uint32_t>(send_addr + 8);
        const int32_t result = func == 1 ? sceCdRead(lsn, sectors, buffer, 0, mem) :
                               func == 2 ? sceCdSeek(lsn) :
                               func == 3 ? sceCdSync(static_cast<int32_t>(lsn)) :
                               func == 4 ? sceCdGetError() : -1;
        if (recv_size >= 4) mem->Write<uint32_t>(recv_addr, static_cast<uint32_t>(result));
        return static_cast<uint32_t>(result);
    }
};

static CDVD_Module g_cdvdModule;

void InitCDVD() {
    RegisterModule(0x80000059, &g_cdvdModule);
    g_cdvdModule.Init();
    sceCdInit(0);
}

static std::string NormalizePath(std::string p) {
    std::replace(p.begin(), p.end(), '\\', '/');
    return p;
}

static bool IsSafeRelativePath(const std::string& path) {
    if (path.empty() || path.front() == '/' || path.find(':') != std::string::npos) return false;
    std::stringstream stream(path);
    std::string component;
    while (std::getline(stream, component, '/'))
        if (component.empty() || component == "." || component == "..") return false;
    return true;
}

static bool IsSha256(const std::string& value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return std::isxdigit(c) != 0;
    });
}

int32_t sceCdInit(int32_t mode) {
    (void)mode;
    std::lock_guard lock(g_cdMutex);
    if (g_pendingRead.valid()) g_pendingRead.wait();
    g_files.clear();
    g_cdError = 0;

    std::ifstream manifest("data/manifest.txt");
    if (!manifest.is_open()) {
        std::cerr << "[CDVD] Failed to open data/manifest.txt\n";
        g_cdError = 1;
        return 0;
    }

    std::string line;
    bool in_files = false;
    try {
        while (std::getline(manifest, line)) {
            if (line == "---FILES---") { in_files = true; continue; }
            if (!in_files || line.empty()) continue;

            std::stringstream stream(line);
            std::string name, lsn_text, size_text, sectors_text, hash;
            if (!std::getline(stream, name, ',') || !std::getline(stream, lsn_text, ',') ||
                !std::getline(stream, size_text, ',') || !std::getline(stream, sectors_text, ',') ||
                !std::getline(stream, hash) || !IsSha256(hash)) {
                throw std::runtime_error("malformed file record");
            }
            const uint64_t lsn = std::stoull(lsn_text);
            const uint64_t size = std::stoull(size_text);
            const uint64_t sectors = std::stoull(sectors_text);
            if (lsn > UINT32_MAX || size > UINT32_MAX || sectors == 0 || sectors > UINT32_MAX ||
                size > sectors * 2048ull) throw std::runtime_error("file record out of range");

            name = NormalizePath(name);
            if (!IsSafeRelativePath(name)) throw std::runtime_error("unsafe file path");
            if (lsn + sectors > static_cast<uint64_t>(UINT32_MAX) + 1ull)
                throw std::runtime_error("sector range overflow");
            const int priority = name.rfind("levels/", 0) == 0
                                     ? (name.find("/level.wad") != std::string::npos ? 10 : 30)
                                     : 20;
            g_files.push_back({name, static_cast<uint32_t>(lsn), static_cast<uint32_t>(size),
                               static_cast<uint32_t>(sectors), priority});
        }
    } catch (const std::exception& error) {
        std::cerr << "[CDVD] Invalid manifest: " << error.what() << '\n';
        g_files.clear();
        g_cdError = 1;
        return 0;
    }

    // Sort deterministic: priority descending, LSN ascending, path length descending
    std::sort(g_files.begin(), g_files.end(), [](const FileRecord& a, const FileRecord& b) {
        if (a.priority != b.priority) return a.priority > b.priority;
        if (a.lsn != b.lsn) return a.lsn < b.lsn;
        return a.name.size() > b.name.size();
    });

    if (g_files.empty()) {
        std::cerr << "[CDVD] Manifest contains no extracted file records\n";
        g_cdError = 1;
        return 0;
    }

    std::cout << "[CDVD] Initialized with " << g_files.size() << " extracted files\n";
    return 1;
}

int32_t sceCdRead(uint32_t lsn, uint32_t sectors, uint32_t buffer_addr, int32_t mode, EE_Memory* mem) {
    (void)mode;
    std::lock_guard lock(g_cdMutex);
    if (g_pendingRead.valid() && g_pendingRead.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        g_cdError = 3;
        return 0;
    }
    if (g_pendingRead.valid()) g_cdError = g_pendingRead.get();
    const uint64_t total_bytes64 = static_cast<uint64_t>(sectors) * 2048;
    if (sectors == 0 || total_bytes64 > UINT32_MAX || !mem ||
        !mem->IsValidRange(buffer_addr, static_cast<size_t>(total_bytes64))) {
        g_cdError = 2;
        return 0;
    }

    std::vector<ReadChunk> chunks;
    uint32_t remaining_sectors = sectors;
    uint32_t cur_lsn = lsn;
    uint32_t cur_buffer = buffer_addr;

    while (remaining_sectors > 0) {
        const FileRecord* best_file = nullptr;
        for (const auto& f : g_files) {
            const uint64_t end_lsn = static_cast<uint64_t>(f.lsn) + f.sector_count;
            if (cur_lsn >= f.lsn && static_cast<uint64_t>(cur_lsn) < end_lsn) {
                best_file = &f;
                break; // g_files is pre-sorted by priority descending
            }
        }

        if (best_file) {
            uint32_t sect_offset_in_file = cur_lsn - best_file->lsn;
            const uint32_t chunk_sectors = std::min(remaining_sectors, 1u);
            const uint64_t file_offset = static_cast<uint64_t>(sect_offset_in_file) * 2048u;
            const uint32_t file_bytes = file_offset < best_file->size
                                            ? std::min<uint64_t>(2048u, best_file->size - file_offset)
                                            : 0u;

            chunks.push_back({
                "data/raw/" + best_file->name,
                file_offset,
                file_bytes,
                2048u - file_bytes,
                cur_buffer
            });

            cur_lsn += chunk_sectors;
            cur_buffer += 2048u;
            remaining_sectors -= chunk_sectors;
        } else {
            std::cerr << "[CDVD] Error: LSN " << cur_lsn << " is not covered by extracted data\n";
            g_cdError = 1;
            return 0;
        }
    }

    g_pendingRead = std::async(std::launch::async, [chunks, mem]() {
        for (const auto& chunk : chunks) {
            std::ifstream f(chunk.file_path, std::ios::binary);
            if (!f.is_open()) {
                std::cerr << "[CDVD] Async Error: Failed to open " << chunk.file_path << "\n";
                return 1;
            }
            f.seekg(chunk.file_offset, std::ios::beg);

            std::vector<uint8_t> tmp(chunk.file_bytes);
            f.read(reinterpret_cast<char*>(tmp.data()), chunk.file_bytes);
            if (static_cast<uint32_t>(f.gcount()) != chunk.file_bytes) return 2;

            for (uint32_t i = 0; i < chunk.file_bytes; ++i) {
                mem->Write<uint8_t>(chunk.dest_buffer_addr + i, tmp[i]);
            }
            for (uint32_t i = 0; i < chunk.zero_bytes; ++i)
                mem->Write<uint8_t>(chunk.dest_buffer_addr + chunk.file_bytes + i, 0);
        }
        return 0;
    }
);

    g_cdError = 0;
    return 1;
}

int32_t sceCdSeek(uint32_t lsn) {
    return 1;
}

int32_t sceCdSync(int32_t mode) {
    std::lock_guard lock(g_cdMutex);
    if (g_pendingRead.valid()) {
        if (mode == 0) {
            g_pendingRead.wait();
            g_cdError = g_pendingRead.get();
            return 0;
        } else {
            if (g_pendingRead.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
                g_cdError = g_pendingRead.get();
                return 0;
            }
            return 1;
        }
    }
    return 0;
}

int32_t sceCdGetError() {
    return g_cdError;
}

} // namespace IOP
} // namespace OpenRatchet
