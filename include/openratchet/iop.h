#pragma once
#include <cstdint>
#include <unordered_map>
#include <string>
#include "ee_memory.h"
#include "ee_context.h"

namespace OpenRatchet {
namespace IOP {

class IOP_Module {
public:
    virtual ~IOP_Module() = default;
    virtual void Init() {}
    virtual uint32_t Dispatch(uint32_t func, uint32_t send_addr, uint32_t send_size, uint32_t recv_addr, uint32_t recv_size, EE_Memory* mem) = 0;
};

// SIF
void InitSif();
void sceSifInitRpc(MIPS_EE_Context* ctx, EE_Memory* mem);
int32_t sceSifBindRpc(MIPS_EE_Context* ctx, EE_Memory* mem, uint32_t client_addr, uint32_t server_id, uint32_t mode);
int32_t sceSifCallRpc(MIPS_EE_Context* ctx, EE_Memory* mem, uint32_t client_addr, uint32_t func, uint32_t mode, uint32_t send_addr, uint32_t ssize, uint32_t recv_addr, uint32_t rsize, uint32_t end_func, uint32_t end_param);
void sceSifSetDma(MIPS_EE_Context* ctx, EE_Memory* mem, uint32_t dma_desc_addr, uint32_t count);
void RegisterModule(uint32_t server_id, IOP_Module* module);

// CDVD
int32_t sceCdInit(int32_t mode);
int32_t sceCdRead(uint32_t lsn, uint32_t sectors, uint32_t buffer_addr, int32_t mode, EE_Memory* mem);
int32_t sceCdSeek(uint32_t lsn);
int32_t sceCdSync(int32_t mode);
int32_t sceCdGetError();
void InitCDVD();

// PAD
int32_t scePadInit(int32_t mode);
int32_t scePadRead(int32_t port, int32_t slot, uint32_t buffer_addr, EE_Memory* mem);
int32_t scePadGetState(int32_t port, int32_t slot);
void InitPAD();

// SPU2
int32_t sceSdInit(int32_t flag);
int32_t sceSdSetParam(uint16_t entry, uint16_t value);
int32_t sceSdVoiceTrans(int16_t channel, int16_t mode, uint32_t m_addr, uint32_t size, uint32_t start);
void InitSPU2();

// MC
int32_t sceMcInit();
int32_t sceMcOpen();
int32_t sceMcRead();
int32_t sceMcWrite();
int32_t sceMcClose();
void InitMC();

// Main initialization
void InitIOP();

} // namespace IOP
} // namespace OpenRatchet
