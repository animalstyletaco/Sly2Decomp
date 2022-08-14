#pragma once

#include "common/common_types.h"
#include "common/versions.h"

constexpr int EE_MAIN_MEM_LOW_PROTECT = 512 * 1024;
constexpr int EE_MAIN_MEM_SIZE = 128 * (1 << 20);  // 128 MB, same as PS2 TOOL
constexpr u64 EE_MAIN_MEM_MAP = 0x2123000000;      // intentionally > 32-bit to catch pointer bugs

// when true, attempt to map the EE memory in the low 2 GB of RAM
// this allows us to use EE pointers as real pointers.  However, this might not always work,
// so this should be used only for debugging.
constexpr bool EE_MEM_LOW_MAP = false;

