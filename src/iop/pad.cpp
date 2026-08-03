// pad.cpp — PS2 controller HLE
// Fix: cache the controller handle instead of open/close every frame
#include "openratchet/iop.h"
#include <iostream>
#include <SDL.h>
#include <algorithm>

namespace OpenRatchet {
namespace IOP {

// Cached controller handle — opened once on first use
static SDL_GameController* g_controller = nullptr;

// Open the first available controller if not already open
static void EnsureController() {
    if (g_controller && SDL_GameControllerGetAttached(g_controller)) return;
    // Previous handle may be detached; close it
    if (g_controller) { SDL_GameControllerClose(g_controller); g_controller = nullptr; }
    for (int i = 0; i < SDL_NumJoysticks(); ++i) {
        if (SDL_IsGameController(i)) {
            g_controller = SDL_GameControllerOpen(i);
            if (g_controller) break;
        }
    }
}

class PAD_Module : public IOP_Module {
public:
    void Init() override {}
    uint32_t Dispatch(uint32_t func, uint32_t send_addr, uint32_t send_size,
                      uint32_t recv_addr, uint32_t recv_size, EE_Memory* mem) override {
        if (func == 1 && send_size >= 8)
            return static_cast<uint32_t>(scePadRead(
                static_cast<int32_t>(mem->Read<uint32_t>(send_addr)),
                static_cast<int32_t>(mem->Read<uint32_t>(send_addr + 4)),
                recv_addr, mem));
        if (func == 2 && send_size >= 8)
            return static_cast<uint32_t>(scePadGetState(
                static_cast<int32_t>(mem->Read<uint32_t>(send_addr)),
                static_cast<int32_t>(mem->Read<uint32_t>(send_addr + 4))));
        return static_cast<uint32_t>(-1);
    }
};

static PAD_Module g_padModule;

void InitPAD() {
    RegisterModule(0x8000010F, &g_padModule);
}

void UpdatePAD() {
    SDL_GameControllerUpdate();
    EnsureController(); // detect newly plugged-in controllers
}

int32_t scePadInit(int32_t /*mode*/) {
    if ((SDL_WasInit(SDL_INIT_GAMECONTROLLER) & SDL_INIT_GAMECONTROLLER) == 0 &&
        SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) != 0)
        return 0;
    return 1;
}

int32_t scePadRead(int32_t /*port*/, int32_t /*slot*/,
                   uint32_t buffer_addr, EE_Memory* mem) {
    uint8_t pad_data[32] = {};
    pad_data[0] = 0x00;
    pad_data[1] = 0x70; // DualShock 2 present

    uint16_t buttons = 0xFFFF; // all buttons released (active low)
    uint8_t  rx = 0x80, ry = 0x80, lx = 0x80, ly = 0x80;

    EnsureController();
    if (g_controller) {
        // Map SDL buttons → PS2 button bits (active low)
        auto press = [&](SDL_GameControllerButton b, uint16_t bit) {
            if (SDL_GameControllerGetButton(g_controller, b)) buttons &= ~bit;
        };
        press(SDL_CONTROLLER_BUTTON_BACK,        1u << 0);  // Select
        press(SDL_CONTROLLER_BUTTON_LEFTSTICK,   1u << 1);  // L3
        press(SDL_CONTROLLER_BUTTON_RIGHTSTICK,  1u << 2);  // R3
        press(SDL_CONTROLLER_BUTTON_START,       1u << 3);  // Start
        press(SDL_CONTROLLER_BUTTON_DPAD_UP,     1u << 4);
        press(SDL_CONTROLLER_BUTTON_DPAD_RIGHT,  1u << 5);
        press(SDL_CONTROLLER_BUTTON_DPAD_DOWN,   1u << 6);
        press(SDL_CONTROLLER_BUTTON_DPAD_LEFT,   1u << 7);
        // L2/R2 via triggers (> half travel = pressed)
        if (SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_TRIGGERLEFT)  > 16384) buttons &= ~(1u << 8);
        if (SDL_GameControllerGetAxis(g_controller, SDL_CONTROLLER_AXIS_TRIGGERRIGHT) > 16384) buttons &= ~(1u << 9);
        press(SDL_CONTROLLER_BUTTON_LEFTSHOULDER,  1u << 10); // L1
        press(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, 1u << 11); // R1
        press(SDL_CONTROLLER_BUTTON_Y,  1u << 12); // Triangle
        press(SDL_CONTROLLER_BUTTON_B,  1u << 13); // Circle
        press(SDL_CONTROLLER_BUTTON_A,  1u << 14); // Cross
        press(SDL_CONTROLLER_BUTTON_X,  1u << 15); // Square

        auto axis = [&](SDL_GameControllerAxis a) -> uint8_t {
            const int raw = SDL_GameControllerGetAxis(g_controller, a);
            return static_cast<uint8_t>(std::clamp((raw + 32768) / 256, 0, 255));
        };
        rx = axis(SDL_CONTROLLER_AXIS_RIGHTX);
        ry = axis(SDL_CONTROLLER_AXIS_RIGHTY);
        lx = axis(SDL_CONTROLLER_AXIS_LEFTX);
        ly = axis(SDL_CONTROLLER_AXIS_LEFTY);
    }

    pad_data[2] = buttons & 0xFF;
    pad_data[3] = (buttons >> 8) & 0xFF;
    pad_data[4] = rx;
    pad_data[5] = ry;
    pad_data[6] = lx;
    pad_data[7] = ly;
    // Bytes 8-19: pressure values — 0 (not pressed) is fine for now

    for (int i = 0; i < 32; ++i)
        mem->Write<uint8_t>(buffer_addr + i, pad_data[i]);

    return 1;
}

int32_t scePadGetState(int32_t /*port*/, int32_t /*slot*/) {
    return 6; // 6 = DUALSHOCK2 Ready
}

} // namespace IOP
} // namespace OpenRatchet
