#include "openratchet/iop.h"
#include <iostream>
#include <SDL.h>
#include <algorithm>

namespace OpenRatchet {
namespace IOP {

class PAD_Module : public IOP_Module {
public:
    void Init() override {}
    uint32_t Dispatch(uint32_t func, uint32_t send_addr, uint32_t send_size, uint32_t recv_addr, uint32_t recv_size, EE_Memory* mem) override {
        if (func == 1 && send_size >= 8) return scePadRead(static_cast<int32_t>(mem->Read<uint32_t>(send_addr)), static_cast<int32_t>(mem->Read<uint32_t>(send_addr + 4)), recv_addr, mem);
        if (func == 2 && send_size >= 8) return scePadGetState(static_cast<int32_t>(mem->Read<uint32_t>(send_addr)), static_cast<int32_t>(mem->Read<uint32_t>(send_addr + 4)));
        return static_cast<uint32_t>(-1);
    }
};

static PAD_Module g_padModule;

void InitPAD() {
    RegisterModule(0x8000010F, &g_padModule);
}

int32_t scePadInit(int32_t mode) {
    if ((SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) == 0 && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0) return 0;
    return 1;
}

int32_t scePadRead(int32_t port, int32_t slot, uint32_t buffer_addr, EE_Memory* mem) {
    uint8_t pad_data[32] = {0};
    
    pad_data[0] = 0x00;
    pad_data[1] = 0x70; // 0x70 = DualShock 2 present

    uint16_t buttons = 0xFFFF;
    uint8_t rx = 0x80, ry = 0x80, lx = 0x80, ly = 0x80;
    SDL_GameController* controller = nullptr;
    for (int i = 0; i < SDL_NumJoysticks(); ++i) if (SDL_IsGameController(i)) { controller = SDL_GameControllerOpen(i); break; }
    auto press = [&](SDL_GameControllerButton b, uint16_t bit) { if (controller && SDL_GameControllerGetButton(controller, b)) buttons &= ~bit; };
    press(SDL_CONTROLLER_BUTTON_BACK,1u<<0); press(SDL_CONTROLLER_BUTTON_LEFTSTICK,1u<<1); press(SDL_CONTROLLER_BUTTON_RIGHTSTICK,1u<<2); press(SDL_CONTROLLER_BUTTON_START,1u<<3);
    press(SDL_CONTROLLER_BUTTON_DPAD_UP,1u<<4); press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,1u<<5); press(SDL_CONTROLLER_BUTTON_DPAD_DOWN,1u<<6); press(SDL_CONTROLLER_BUTTON_DPAD_LEFT,1u<<7);
    press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,1u<<10); press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER,1u<<11); press(SDL_CONTROLLER_BUTTON_Y,1u<<12); press(SDL_CONTROLLER_BUTTON_B,1u<<13); press(SDL_CONTROLLER_BUTTON_A,1u<<14); press(SDL_CONTROLLER_BUTTON_X,1u<<15);
    auto axis = [&](SDL_GameControllerAxis a) { return static_cast<uint8_t>(std::clamp((static_cast<int>(SDL_GameControllerGetAxis(controller,a)) + 32768) / 256, 0, 255)); };
    if (controller) { rx=axis(SDL_CONTROLLER_AXIS_RIGHTX); ry=axis(SDL_CONTROLLER_AXIS_RIGHTY); lx=axis(SDL_CONTROLLER_AXIS_LEFTX); ly=axis(SDL_CONTROLLER_AXIS_LEFTY); }

    pad_data[2] = buttons & 0xFF;
    pad_data[3] = (buttons >> 8) & 0xFF;
    pad_data[4] = rx;
    pad_data[5] = ry;
    pad_data[6] = lx;
    pad_data[7] = ly;
    
    // Bytes 8-19 are pressure sensitive values (0-255).
    // Let's just mock them as 0 (not pressed).
    for (int i = 8; i < 20; ++i) pad_data[i] = 0;

    for (int i = 0; i < 32; ++i) {
        mem->Write<uint8_t>(buffer_addr + i, pad_data[i]);
    }
    if (controller) SDL_GameControllerClose(controller);

    return 1;
}

int32_t scePadGetState(int32_t port, int32_t slot) {
    return 6; // 6 = Ready
}

} // namespace IOP
} // namespace OpenRatchet
