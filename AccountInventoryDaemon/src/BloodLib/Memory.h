// BloodLib - memory-related declarations used by daemon/framework sources.
#pragma once
#include "MemoryManager.h"

namespace blood {

uint32_t Memory_PageSize();
uint32_t Memory_ProcessorCount();
uint32_t Memory_AllocationGranularity();

} // namespace blood
