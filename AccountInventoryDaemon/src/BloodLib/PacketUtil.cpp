// BloodLib - repeat-aware packet format cursors (original: PacketUtil.cpp).
#include "Packet.h"
#include "PacketUtil.h"

#include <string.h>

SOURCE_FILE("PacketUtil.cpp");

namespace blood {
namespace {

const wchar_t* WideFormat(const char* format) {
    thread_local wchar_t buffer[512];
    if (!format) return L"";
    MultiByteToWideChar(CP_ACP, 0, format, -1, buffer, 512);
    return buffer;
}

} // namespace

static_assert(offsetof(PacketFormatIterator, format) == 0x08, "PacketFormatIterator ABI");
static_assert(offsetof(PacketFormatIterator, formatLen) == 0x10, "PacketFormatIterator ABI");
static_assert(offsetof(PacketFormatIterator, index) == 0x14, "PacketFormatIterator ABI");
static_assert(offsetof(RepeatPacketComposer, countSlot) == 0x18, "RepeatPacketComposer ABI");
static_assert(offsetof(RepeatPacketComposer, startIndex) == 0x30, "RepeatPacketComposer ABI");
static_assert(offsetof(RepeatPacketComposer, depth) == 0x3C, "RepeatPacketComposer ABI");
static_assert(offsetof(RepeatPacketComposer, sub) == 0x40, "RepeatPacketComposer ABI");
static_assert(offsetof(RepeatPacketComposer, subTop) == 0xA0, "RepeatPacketComposer ABI");
static_assert(sizeof(RepeatPacketComposer) == 0xA8, "RepeatPacketComposer ABI");
static_assert(offsetof(RepeatPacketDecomposer, offset) == 0x18, "RepeatPacketDecomposer ABI");
static_assert(offsetof(RepeatPacketDecomposer, counts) == 0x20, "RepeatPacketDecomposer ABI");
static_assert(offsetof(RepeatPacketDecomposer, starts) == 0x28, "RepeatPacketDecomposer ABI");
static_assert(offsetof(RepeatPacketDecomposer, depth) == 0x34, "RepeatPacketDecomposer ABI");
static_assert(sizeof(RepeatPacketDecomposer) == 0x38, "RepeatPacketDecomposer ABI");

RepeatPacketComposer::RepeatPacketComposer()
    : depth(-1), subTop(-1) {
    format = NULL;
    formatLen = 0;
    index = 0;
    for (int i = 0; i < 3; ++i) {
        countSlot[i] = NULL;
        startIndex[i] = 0;
        sub[i].countByte = NULL;
        sub[i].savedEnd = NULL;
        sub[i].format = NULL;
        sub[i].index = 0;
    }
}

void RepeatPacketComposer::Reset() {
    index = 0;
    depth = -1;
    subTop = -1;
}

void RepeatPacketComposer::SetFormat(const char* newFormat, int startIndex) {
    if (!newFormat || startIndex < 0)
        CRASH(0x34, L"== CRASH == ASSERT CRASH");
    format = newFormat;
    formatLen = static_cast<int>(strlen(format));
    index = startIndex;
    if (formatLen <= startIndex)
        CRASH(0x38, L"== CRASH == ASSERT CRASH");
}

void RepeatPacketComposer::AddIndex(int count) {
    if (subTop < 0) index += count;
    else sub[subTop].index += count;
}

void RepeatPacketComposer::SubIndex(int count) {
    if (subTop < 0) index -= count;
    else sub[subTop].index -= count;
}

void RepeatPacketComposer::AdjustCounts(Packet* packet, void* oldEnd) {
    if (subTop < 0) return;
    unsigned char low = static_cast<unsigned char>(reinterpret_cast<uintptr_t>(oldEnd));
    for (int i = 0; i <= subTop; ++i) {
        SubFormat& entry = sub[i];
        if (entry.countByte) {
            signed char delta = static_cast<signed char>(
                low - static_cast<unsigned char>(reinterpret_cast<uintptr_t>(entry.savedEnd)));
            *entry.countByte = static_cast<u8>(static_cast<unsigned char>(*entry.countByte + delta));
        }
        entry.savedEnd = Packet_GetEnd(packet);
    }
}

const char* RepeatPacketComposer::CurrentFormat() {
    if (subTop < 0) return format + index;
    if (!sub[subTop].format) CRASH(0x90, L"== CRASH == ASSERT CRASH");
    return sub[subTop].format + sub[subTop].index;
}

int RepeatPacketComposer::GetIndex() { return index; }

void RepeatPacketComposer::SkipRepeatBlock() {
    const char* activeFormat = NULL;
    int* activeIndex = NULL;
    if (subTop < 0) {
        activeFormat = format;
        activeIndex = &index;
    } else {
        activeFormat = sub[subTop].format;
        activeIndex = &sub[subTop].index;
    }
    if (!activeFormat || *activeIndex < 1 || activeFormat[*activeIndex - 1] != 'C')
        CRASH(0xD7, L"== CRASH == ASSERT CRASH");
    int nesting = 1;
    while (true) {
        char character = activeFormat[*activeIndex];
        if (character == 0) CRASH(0xDB, L"== CRASH == ASSERT CRASH");
        if (character == 'C') ++nesting;
        else if (character == ';') --nesting;
        ++*activeIndex;
        if (nesting <= 0) break;
    }
}

void RepeatPacketComposer::BeginRepeat(Packet* packet) {
    int newDepth = ++depth;
    if (!packet) {
        LOG(0, 0x31, L"[packet], packet is null pointer");
        CRASH(0x32, L"== CRASH == ASSERT CRASH");
        return;
    }
    u16* header = packet->current ? &packet->current->size : NULL;
    int freeBytes = header ? kMaxPacketSize - *header : 0;
    if (static_cast<u16>(freeBytes) < 2) {
        LOG(0, 0x36, L"[packet], not enough free size in packet, free size:%d, format:%s",
            freeBytes, WideFormat(format));
        CRASH(0x37, L"== CRASH == ASSERT CRASH");
        return;
    }
    if (newDepth > 2) {
        LOG(0, 0x3B, L"[packet], too many nested Repeat Format, current depth:%d, format:%s",
            newDepth, WideFormat(format));
        return;
    }
    if (subTop < 0) {
        if (!format || format[index] != 'C') {
            LOG(0, 0x3F,
                L"[packet], invalid PushRepeatCount call when format is not 'C', format:%s, index:%d",
                WideFormat(format), index);
            CRASH(0x40, L"== CRASH == ASSERT CRASH");
            return;
        }
    } else if (!sub[subTop].format || sub[subTop].format[sub[subTop].index] != 'C') {
        LOG(0, 0x45,
            L"[packet], invalid PushRepeatCount call when inner format is not 'C', format:%s, index:%d, innerFormat:%s, innerIndex:%d",
            WideFormat(format), index, WideFormat(sub[subTop].format), sub[subTop].index);
        CRASH(0x46, L"== CRASH == ASSERT CRASH");
        return;
    }
    countSlot[newDepth] = reinterpret_cast<u16*>(reinterpret_cast<u8*>(header) + *header);
    if (subTop < 0) {
        startIndex[newDepth] = index;
        ++index;
    } else {
        startIndex[newDepth] = sub[subTop].index;
        ++sub[subTop].index;
    }
    *countSlot[newDepth] = 0;
    Packet_Advance(packet, 2);
}

void RepeatPacketComposer::EndRepeat() {
    int currentDepth = depth;
    if (currentDepth < 0) {
        LOG(0, 0x59, L"[packet], invalid pop opertion , top:%d", currentDepth);
        CRASH(0x5A, L"== CRASH == ASSERT CRASH");
        return;
    }
    if (subTop < 0) {
        if (!format || format[startIndex[currentDepth]] != 'C') {
            LOG(0, 0x5E,
                L"[packet], invalid pop repeat operation when format's start[top] is not 'C', format:%s, top:%d, start[top]:%d",
                WideFormat(format), currentDepth, startIndex[currentDepth]);
            CRASH(0x5F, L"== CRASH == ASSERT CRASH");
            return;
        }
    } else if (!sub[subTop].format || sub[subTop].format[startIndex[currentDepth]] != 'C') {
        LOG(0, 0x63,
            L"[packet], invalid pop repeat operation when sub format's start[top] is not 'C', format:%s, subformat:%s, top:%d, start[top]:%d",
            WideFormat(format), WideFormat(sub[subTop].format), currentDepth,
            startIndex[currentDepth]);
        CRASH(0x64, L"== CRASH == ASSERT CRASH");
        return;
    }
    countSlot[currentDepth] = NULL;
    --depth;
    SkipRepeatBlock();
}

void RepeatPacketComposer::NextElement() {
    if (depth < 0) return;
    int next = startIndex[depth] + 1;
    if (subTop < 0) index = next;
    else sub[subTop].index = next;
    if (countSlot[depth]) ++*countSlot[depth];
}

RepeatPacketDecomposer::RepeatPacketDecomposer()
    : offset(0), reserved1C(0), depth(-1) {
    format = NULL;
    formatLen = 0;
    index = 0;
    for (int i = 0; i < 3; ++i) {
        counts[i] = 0;
        starts[i] = 0;
    }
}

void RepeatPacketDecomposer::Reset() {
    index = 0;
    offset = 0;
    depth = -1;
}

void RepeatPacketDecomposer::SetFormat(const char* newFormat, int startIndex) {
    if (!newFormat || startIndex < 0)
        CRASH(0x34, L"== CRASH == ASSERT CRASH");
    format = newFormat;
    formatLen = static_cast<int>(strlen(format));
    index = startIndex;
    if (formatLen <= startIndex)
        CRASH(0x38, L"== CRASH == ASSERT CRASH");
}

void RepeatPacketDecomposer::AddIndex(int count) { index += count; }
void RepeatPacketDecomposer::SubIndex(int count) { index -= count; }
const char* RepeatPacketDecomposer::CurrentFormat() { return format + index; }
int RepeatPacketDecomposer::GetIndex() { return index; }

void RepeatPacketDecomposer::SkipRepeatBlock() {
    if (!format || index < 1 || format[index - 1] != 'C')
        CRASH(0xD7, L"== CRASH == ASSERT CRASH");
    int nesting = 1;
    while (true) {
        char character = format[index];
        if (character == 0) CRASH(0xDB, L"== CRASH == ASSERT CRASH");
        if (character == 'C') ++nesting;
        else if (character == ';') --nesting;
        ++index;
        if (nesting <= 0) break;
    }
}

u16 RepeatPacketDecomposer::BeginRepeat(const u16* wireCount, u16 available) {
    int newDepth = ++depth;
    if (available < 2) {
        LOG(0, 0x108, L"[packet], invalid buffer size, size:%d", available);
        return 0xFFFF;
    }
    if (!format || format[index] != 'C') {
        LOG(0, 0x10F,
            L"== CRASH == [packet], invalid format index, index:%d, char:%d",
            index, format ? static_cast<unsigned char>(format[index]) : 0);
        CRASH(0x110, L"== CRASH == ASSERT CRASH");
        return 0xFFFF;
    }
    if (newDepth > 2) CRASH(0x113, L"== CRASH == ASSERT CRASH");
    if (!wireCount) CRASH(0x114, L"== CRASH == ASSERT CRASH");
    u16 count = *wireCount;
    counts[newDepth] = count;
    starts[newDepth] = index;
    ++index;
    offset += 2;
    return count;
}

} // namespace blood
