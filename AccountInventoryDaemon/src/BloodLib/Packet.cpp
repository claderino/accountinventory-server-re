// BloodLib - packet chain operations (original: Packet.cpp).
#include "Packet.h"
#include "PacketFormat.h"

#include <string.h>

SOURCE_FILE("Packet.cpp");

namespace blood {

static_assert(offsetof(Packet, splitter) == 0x00, "Packet ABI");
static_assert(offsetof(Packet, current) == 0x08, "Packet ABI");
static_assert(offsetof(Packet, first) == 0x10, "Packet ABI");
static_assert(offsetof(Packet, last) == 0x18, "Packet ABI");
static_assert(sizeof(Packet) == 0x20, "Packet ABI");

void Packet_Init(Packet* packet) {
    if (!packet) return;
    packet->splitter = NULL;
    packet->current = NULL;
    packet->first = NULL;
    packet->last = NULL;
}

int Packet_GetSize(const Packet* packet) {
    return packet && packet->current ? packet->current->size : 0;
}

u16 Packet_GetCommand(const Packet* packet) {
    if (!packet || !packet->current || packet->current->size < 4) return 0;
    u16 command = 0;
    memcpy(&command, packet->current->fields, sizeof(command));
    return command;
}

int Packet_GetFree(const Packet* packet) {
    return packet && packet->current ? kMaxPacketSize - packet->current->size : 0;
}

u8* Packet_GetEnd(const Packet* packet) {
    if (!packet || !packet->current) return NULL;
    return reinterpret_cast<u8*>(&packet->current->size) + packet->current->size;
}

u8* Packet_BeginAppend(Packet* packet) {
    if (!packet) return NULL;
    PacketBuffer* previous = packet->current;
    u16* previousHeader = previous ? &previous->size : NULL;
    PacketBuffer* buffer = PacketBuffer::Acquire();
    if (!buffer) return NULL;
    buffer->size = 2;
    buffer->next = NULL;
    if (!packet->first) packet->first = buffer;
    else packet->last->next = buffer;
    packet->last = buffer;
    packet->current = buffer;
    if (packet->splitter && !packet->splitter->AddBuffer(previousHeader, &buffer->size))
        return NULL;
    return buffer->End();
}

u8* Packet_NewBuffer(Packet* packet, PacketUtil* cursor) {
    if (!cursor) CRASH(0x41, L"== CRASH == ASSERT CRASH");
    void* oldEnd = Packet_GetEnd(packet);
    u8* result = Packet_BeginAppend(packet);
    if (result) cursor->AdjustCounts(packet, oldEnd);
    return result;
}

u16 Packet_ComposeAtV(Packet* packet, PacketUtil* cursor, int fieldCount,
                      va_list* arguments) {
    if (!packet || !cursor || !packet->current)
        CRASH(0x8B, L"== CRASH == ASSERT CRASH");
    int count = fieldCount < 0 ? cursor->formatLen - cursor->index : fieldCount;
    u16* header = &packet->current->size;
    u16 written = Packet_ComposeFields(reinterpret_cast<u8*>(header) + *header,
                                       kMaxPacketSize - *header, cursor->CurrentFormat(),
                                       count, arguments);
    cursor->AddIndex(fieldCount);
    Packet_Advance(packet, static_cast<i16>(written));
    return written;
}

u16 Packet_ComposeAt(Packet* packet, PacketUtil* cursor, int fieldCount, ...) {
    if (!packet || !cursor || !packet->current)
        CRASH(0x7A, L"== CRASH == ASSERT CRASH");
    va_list ap;
    va_start(ap, fieldCount);
    u16 written = Packet_ComposeAtV(packet, cursor, fieldCount, &ap);
    va_end(ap);
    return written;
}

void Packet_AddBytes(Packet* packet, const void* source, u16 length) {
    if (!packet || !packet->current || static_cast<i16>(length) < 0 ||
        static_cast<u16>(kMaxPacketSize - packet->current->size) < length)
        CRASH(0xE2, L"== CRASH == ASSERT CRASH");
    if (!length) return;
    memmove(packet->current->End(), source, length);
    packet->current->size = static_cast<u16>(packet->current->size + length);
}

void Packet_Advance(Packet* packet, i16 delta) {
    if (!packet || !packet->current)
        CRASH(0xF6, L"== CRASH == ASSERT CRASH");
    u16 freeBytes = static_cast<u16>(kMaxPacketSize - packet->current->size);
    if (freeBytes < static_cast<u16>(delta))
        CRASH(0xF6, L"== CRASH == ASSERT CRASH");
    packet->current->size = static_cast<u16>(packet->current->size + delta);
}

short Packet_Decompose(Packet* packet, const char* format, ...) {
    if (!packet || !format || !packet->current)
        CRASH(0x1A9, L"== CRASH == ASSERT CRASH");
    va_list ap;
    va_start(ap, format);
    u16 result = Packet_DecomposeFields(packet->current->fields, packet->current->size - 2,
                                        format, static_cast<u64>(-1), &ap);
    va_end(ap);
    return static_cast<short>(result);   // sub_1D660 returns the engine's byte count
}

u16 Packet_DecomposeAt(Packet* packet, PacketDecomposer* decomposer, int fieldCount,
                       va_list* arguments) {
    if (!decomposer) {
        LOG(0, 0x1B8, L"[packet], invalid pdr");
        CRASH(0x1B9, L"== CRASH == ASSERT CRASH");
        return 0;
    }
    if (!packet || !packet->current) {
        LOG(0, 0x1BD, L"== CRASH == [packet], invalid CurHeader");
        CRASH(0x1BE, L"== CRASH == ASSERT CRASH");
        return 0;
    }
    int size = packet->current->size;
    if (static_cast<int>(decomposer->offset) >= size - 2) {
        LOG(0, 0x1C5,
            L"[packet], pdr->offset %d >= CurHeader->size %d - HEADER_SIZE %d",
            decomposer->offset, size, 2);
        return 0;
    }
    int count = fieldCount < 0 ? decomposer->formatLen - decomposer->index : fieldCount;
    u16 read = Packet_DecomposeFields(packet->current->fields + decomposer->offset,
                                      size - decomposer->offset - 2,
                                      decomposer->CurrentFormat(), count, arguments);
    decomposer->index += fieldCount;
    decomposer->offset += read;
    return read;
}

void Packet_Release(Packet* packet) {
    if (!packet) return;
    PacketBuffer* buffer = packet->first;
    while (buffer) {
        PacketBuffer* next = buffer->next;
        buffer->Release(L"src\\Packet.cpp", 0x23);
        buffer = next;
    }
    packet->current = NULL;
    packet->first = NULL;
    packet->last = NULL;
    if (packet->splitter) packet->splitter->Clear();
}

PacketComposer::PacketComposer(u16 command, const char* format, int startIndex) {
    Packet_Init(&packet);
    splitter.SetCommand(command);
    splitter.Clear();
    packet.splitter = &splitter;
    util.SetFormat(format, startIndex);
}

PacketComposer::~PacketComposer() { Packet_Release(&packet); }

u16 PacketComposer::Compose(int fieldCount, ...) {
    va_list ap;
    va_start(ap, fieldCount);
    u16 written = Packet_ComposeAtV(&packet, &util, fieldCount, &ap);
    va_end(ap);
    return written;
}

} // namespace blood
