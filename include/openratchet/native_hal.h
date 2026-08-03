#pragma once

#include <SDL.h>
#include <functional>

namespace OpenRatchet {
namespace HAL {

bool Init();
void Shutdown();
void PollEvents(bool& running);

SDL_Window* GetWindow();

// Register an additional per-event callback (used by ImGui for input forwarding).
void SetExtraEventHandler(std::function<void(SDL_Event*)> handler);

} // namespace HAL
} // namespace OpenRatchet
