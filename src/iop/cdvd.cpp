#include "openratchet/iop.h"
#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <string>
#include <algorithm>
#include <future>

namespace OpenRatchet {
namespace IOP {

struct FileRecord {
    std::string name;
    uint32_t lsn;
    uint32_t size;
};

static std::vector<FileRecord> g_files;
static std::future<int> g_pendingRead;
static int g_cdError = 0;

class CDVD_Module : public IOP_Module {
public:
    void Init() override {}
    uint32_t Dispatch(uint32_t func, uint32_t send_addr, uint32_t send_size, uint32_t recv_addr, uint32_t recv_size, EE_Memory* mem) override {
        return 0;
    }
};

static CDVD_Module g_cdvdModule;

void InitCDVD() {
    RegisterModule(0x80000059, &g_cdvdModule);
}

int32_t sceCdInit(int32_t mode) {
    g_files.clear();
    std::ifstream mf("data/manifest.txt");
    if (!mf.is_open()) {
        std::cerr << "[CDVD] Failed to open data/manifest.txt\n";
        return 0;
    }

    std::string line;
    bool inFiles = false;
    while (std::getline(mf, line)) {
        if (line == "---FILES---") {
            inFiles = true;
            continue;
        }
        if (inFiles) {
            std::stringstream ss(line);
            std::string name, lsn_str, size_str;
            if (std::getline(ss, name, ',') &&
                std::getline(ss, lsn_str, ',') &&
                std::getline(ss, size_str)) {
                g_files.push_back({name, (uint32_t)std::stoul(lsn_str), (uint32_t)std::stoul(size_str)});
            }
        }
    }
    std::cout << "[CDVD] Initialized with " << g_files.size() << " files from manifest\n";
    return 1;
}

int32_t sceCdRead(uint32_t lsn, uint32_t sectors, uint32_t buffer_addr, int32_t mode, EE_Memory* mem) {
    std::string target_file;
    uint32_t file_offset = 0;
    uint32_t read_bytes = sectors * 2048;

    for (const auto& f : g_files) {
        if (lsn >= f.lsn && (lsn - f.lsn) * 2048 < f.size) {
            target_file = "data/raw/" + f.name;
            file_offset = (lsn - f.lsn) * 2048;
            break;
        }
    }

    if (target_file.empty()) {
        std::cerr << "[CDVD] Error: LSN " << lsn << " not found in manifest.\n";
        g_cdError = 1;
        return 0; 
    }

    g_pendingRead = std::async(std::launch::async, [target_file, file_offset, read_bytes, buffer_addr, mem]() {
        std::ifstream f(target_file, std::ios::binary);
        if (!f.is_open()) {
            std::cerr << "[CDVD] Async Error: Failed to open " << target_file << "\n";
            return 1;
        }
        f.seekg(file_offset, std::ios::beg);
        
        std::vector<uint8_t> tmp(read_bytes);
        f.read(reinterpret_cast<char*>(tmp.data()), read_bytes);
        
        for (uint32_t i = 0; i < read_bytes; ++i) {
            mem->Write<uint8_t>(buffer_addr + i, tmp[i]);
        }
        
        return 0; 
    });

    g_cdError = 0;
    return 1;
}

int32_t sceCdSeek(uint32_t lsn) {
    return 1;
}

int32_t sceCdSync(int32_t mode) {
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
