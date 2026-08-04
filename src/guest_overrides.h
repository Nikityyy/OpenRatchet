#pragma once

class PS2Runtime;

namespace ratchet {
void registerGuestBootstrapOverrides(PS2Runtime& runtime);
void registerGuestDmacOverride(PS2Runtime& runtime);
}
