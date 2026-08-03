#include "openratchet/native_hal.h"
#include <iostream>
#include <functional>

namespace OpenRatchet {
namespace HAL {

static SDL_Window* g_window = nullptr;
static std::function<void(SDL_Event*)> g_extra_event_handler;

bool Init() {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER | SDL_INIT_AUDIO) != 0) {
        std::cerr << "SDL_Init Error: " << SDL_GetError() << std::endl;
        return false;
    }

    g_window = SDL_CreateWindow("OpenRatchet \xe2\x80\x94 Ratchet & Clank",
                                SDL_WINDOWPOS_CENTERED,
                                SDL_WINDOWPOS_CENTERED,
                                1280, 720,
                                SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE);
    if (!g_window) {
        std::cerr << "SDL_CreateWindow Error: " << SDL_GetError() << std::endl;
        SDL_Quit();
        return false;
    }

    return true;
}

void Shutdown() {
    g_extra_event_handler = nullptr;
    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = nullptr;
    }
    SDL_Quit();
}

void PollEvents(bool& running) {
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Forward to ImGui (or any registered consumer) before our own handling
        if (g_extra_event_handler) g_extra_event_handler(&event);

        if (event.type == SDL_QUIT) {
            running = false;
        } else if (event.type == SDL_WINDOWEVENT) {
            if (event.window.event == SDL_WINDOWEVENT_RESIZED) {
                // Handle resize if needed
            }
        }
    }
}

SDL_Window* GetWindow() {
    return g_window;
}

void SetExtraEventHandler(std::function<void(SDL_Event*)> handler) {
    g_extra_event_handler = std::move(handler);
}

} // namespace HAL
} // namespace OpenRatchet
