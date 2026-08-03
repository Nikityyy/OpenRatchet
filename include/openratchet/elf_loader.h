#pragma once
#include <string>
#include <cstdint>
#include "ee_context.h"

class EE_Memory;

class ELFLoader {
public:
    static bool LoadELF(const std::string& path, EE_Memory& mem, MIPS_EE_Context& ctx);
};
