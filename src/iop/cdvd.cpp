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

// A completed async read: file data in host memory, ready to copy to guest.
struct PendingReadResult {
    std::vector<uint8_t> data;  // filled sectors (each 2048 bytes)
    uint32_t dest_buffer_addr;  // guest EE address to copy into
    uint32_t total_bytes;       // how many bytes to copy
    int error_code;
};

struct ReadChunk {
    std::string file_path;
    uint64_t file_offset;
    uint32_t file_bytes;    // bytes available in file
    uint32_t zero_bytes;    // pad to sector boundary
    uint32_t dest_offset;   // byte offset from dest_buffer_addr for this chunk
};

static std::vector<FileRecord> g_files;
static std::string g_isoPath = "games/Ratchet & Clank (USA) (En,Fr,De,Es,It).iso";
static std::future<PendingReadResult> g_pendingRead;
static EE_Memory* g_pendingMem = nullptr;  // memory to apply result to in sceCdSync
static int g_cdError = 0;
static std::mutex g_cdMutex;

// Forward declaration — defined later in this file
static void ApplyPendingResult(const PendingReadResult& result);

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
            if (line.rfind("ISO_PATH=", 0) == 0) {
                g_isoPath = line.substr(9);
                continue;
            }
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
    std::clog << "[CDVD] sceCdRead lsn=" << lsn << " sectors=" << sectors << " buffer=0x" << std::hex << buffer_addr << std::dec << "\n";

    // If a previous async read is still in-flight, wait for it before starting a new one.
    if (g_pendingRead.valid() &&
        g_pendingRead.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        g_cdError = 3; // busy
        return 0;
    }
    if (g_pendingRead.valid()) {
        // Previous read completed — apply it to guest memory before starting a new read.
        ApplyPendingResult(g_pendingRead.get());
    }


    if (sectors == 0) {
        g_cdError = 0;
        return 1; // 0 sectors read is a no-op success
    }

    const uint64_t total_bytes64 = static_cast<uint64_t>(sectors) * 2048;
    if (total_bytes64 > UINT32_MAX || !mem ||
        !mem->IsValidRange(buffer_addr, static_cast<size_t>(total_bytes64))) {
        std::clog << "[CDVD] sceCdRead INVALID ARGUMENTS! sectors=" << sectors << " buffer=0x" << std::hex << buffer_addr << std::dec << " valid=" << (mem ? mem->IsValidRange(buffer_addr, static_cast<size_t>(total_bytes64)) : 0) << "\n";
        g_cdError = 2;
        return 0;
    }

    std::vector<ReadChunk> chunks;
    uint32_t remaining_sectors = sectors;
    uint32_t cur_lsn    = lsn;
    uint32_t cur_offset = 0;  // byte offset from buffer_addr

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
            const uint32_t sect_offset_in_file = cur_lsn - best_file->lsn;
            const uint32_t file_end_lsn        = best_file->lsn + best_file->sector_count;
            // Read as many consecutive sectors as this file covers
            const uint32_t chunk_sectors = std::min(remaining_sectors,
                                                     file_end_lsn - cur_lsn);
            const uint64_t file_offset = static_cast<uint64_t>(sect_offset_in_file) * 2048u;
            const uint32_t file_bytes  = static_cast<uint32_t>(
                file_offset < best_file->size
                    ? std::min<uint64_t>(static_cast<uint64_t>(chunk_sectors) * 2048u,
                                         best_file->size - file_offset)
                    : 0u);
            const uint32_t total_chunk_bytes = chunk_sectors * 2048u;

            chunks.push_back({
                "data/raw/" + best_file->name,
                file_offset,
                file_bytes,
                total_chunk_bytes - file_bytes,
                cur_offset
            });

            cur_lsn    += chunk_sectors;
            cur_offset += total_chunk_bytes;
            remaining_sectors -= chunk_sectors;
        } else {
            std::cout << "[CDVD] Fallback: Reading LSN " << cur_lsn << " (" << remaining_sectors << " sectors) directly from ISO: " << g_isoPath << "\n";
            chunks.push_back({
                g_isoPath,
                static_cast<uint64_t>(cur_lsn) * 2048u,
                remaining_sectors * 2048u,
                0,
                cur_offset
            });
            cur_lsn += remaining_sectors;
            cur_offset += remaining_sectors * 2048u;
            remaining_sectors = 0;
        }
    }

    // Build a total buffer size
    const uint32_t total_bytes = sectors * 2048u;

    // Launch async read into a HOST-SIDE buffer — no EE_Memory access from background thread.
    g_pendingRead = std::async(std::launch::async,
        [chunks, total_bytes, buffer_addr]() -> PendingReadResult {
            PendingReadResult result{};
            result.dest_buffer_addr = buffer_addr;
            result.total_bytes      = total_bytes;
            result.data.resize(total_bytes, 0);

            for (const auto& chunk : chunks) {
                if (chunk.file_path.empty()) {
                    continue;
                }
                std::ifstream f(chunk.file_path, std::ios::binary);
                if (!f.is_open()) {
                    std::cerr << "[CDVD] Async Error: Failed to open " << chunk.file_path << "\n";
                    result.error_code = 1;
                    return result;
                }
                f.seekg(static_cast<std::streamoff>(chunk.file_offset), std::ios::beg);
                if (chunk.file_bytes > 0) {
                    f.read(reinterpret_cast<char*>(result.data.data() + chunk.dest_offset),
                           chunk.file_bytes);
                    if (static_cast<uint32_t>(f.gcount()) != chunk.file_bytes) {
                        result.error_code = 2;
                        return result;
                    }
                }
                // zero_bytes are already 0 from the resize.
            }
            result.error_code = 0;
            return result;
        });

    g_pendingMem  = mem;
    g_cdError = 0;
    return 1;
}

int32_t sceCdSeek(uint32_t lsn) {
    return 1;
}

// Helper: flush a completed PendingReadResult into guest EE memory
static void ApplyPendingResult(const PendingReadResult& result) {
    g_cdError = result.error_code;
    if (result.error_code == 0 && g_pendingMem) {
        const uint32_t copy_bytes = std::min<uint32_t>(result.total_bytes,
            static_cast<uint32_t>(result.data.size()));
        uint8_t* dest = g_pendingMem->GetRamPointer(result.dest_buffer_addr);
        if (dest) std::memcpy(dest, result.data.data(), copy_bytes);
    }
    g_pendingMem = nullptr;
}

int32_t sceCdSync(int32_t mode) {
    std::lock_guard lock(g_cdMutex);
    if (!g_pendingRead.valid()) return 0;

    if (mode == 0) {
        // Blocking wait — apply result to guest memory on this (main) thread
        ApplyPendingResult(g_pendingRead.get());
        return 0;
    } else {
        // Non-blocking poll
        if (g_pendingRead.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            ApplyPendingResult(g_pendingRead.get());
            return 0;
        }
        return 1; // still in-progress
    }
}

int32_t sceCdGetError() {
    return g_cdError;
}

} // namespace IOP
} // namespace OpenRatchet
