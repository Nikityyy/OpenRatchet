#pragma once

class PS2Runtime;

namespace ratchet {
void registerGuestBootstrapOverrides(PS2Runtime& runtime);
// Registers root-owned guest compatibility after the ELF function table exists.
// The set spans SIF, CDVD, DMAC, callbacks, and GS diagnostics; it is not
// limited to DMAC despite the historical entry-point name.
void registerGuestRuntimeOverrides(PS2Runtime& runtime);
}
