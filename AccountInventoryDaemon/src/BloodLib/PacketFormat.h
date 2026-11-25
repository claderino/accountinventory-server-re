// BloodLib - packet field engine (original: PacketBuffer.cpp sub_26990 / sub_26D20).
#pragma once
#include "BloodLib.h"
#include <stdarg.h>
#include <stdint.h>

namespace blood {

u16 Packet_ComposeFields(u8* destination, u32 capacity, const char* format,
                         u64 fieldCount, va_list* arguments);
u16 Packet_DecomposeFields(const u8* source, u32 available, const char* format,
                           u64 fieldCount, va_list* arguments);

// Ranking-compatible convenience names used by reconstruction tests.
u16 ComposeFormat(void* destination, int capacity, const char* format, int fieldCount, ...);
u16 ComposeFormatV(void* destination, int capacity, const char* format, int fieldCount,
                   va_list arguments);
u16 DecomposeFormat(const void* source, int available, const char* format, ...);
u16 DecomposeFormatV(const void* source, int available, const char* format,
                     va_list arguments);

} // namespace blood
