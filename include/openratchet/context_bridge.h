#pragma once
#include "openratchet/ee_context.h"
#include "ps2_runtime.h"

void copyContextToR5900(const MIPS_EE_Context& src, R5900Context& dst);
void copyContextFromR5900(const R5900Context& src, MIPS_EE_Context& dst);
