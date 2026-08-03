#pragma once

#include <SDL.h>

namespace OpenRatchet {
namespace HAL {

bool Init();
void Shutdown();
void PollEvents(bool& running);

SDL_Window* GetWindow();

} // namespace HAL
} // namespace OpenRatchet
