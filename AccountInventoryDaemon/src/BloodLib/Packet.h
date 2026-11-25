// BloodLib - packet chain under construction or received (original: Packet.cpp).
#pragma once
#include "FragmentPacket.h"
#include "PacketBuffer.h"
#include "PacketUtil.h"

namespace blood {

struct Packet {
    FragmentPacketSplitter* splitter; // +0x00
    PacketBuffer* current;            // +0x08
    PacketBuffer* first;              // +0x10
    PacketBuffer* last;               // +0x18
};

void Packet_Init(Packet* packet);
int Packet_GetSize(const Packet* packet);
u16 Packet_GetCommand(const Packet* packet);
int Packet_GetFree(const Packet* packet);
u8* Packet_GetEnd(const Packet* packet);
u8* Packet_BeginAppend(Packet* packet);
u8* Packet_NewBuffer(Packet* packet, PacketUtil* cursor);
u16 Packet_ComposeAtV(Packet* packet, PacketUtil* cursor, int fieldCount, va_list* arguments);
u16 Packet_ComposeAt(Packet* packet, PacketUtil* cursor, int fieldCount, ...);
void Packet_AddBytes(Packet* packet, const void* source, u16 length);
void Packet_Advance(Packet* packet, i16 delta);
short Packet_Decompose(Packet* packet, const char* format, ...);
u16 Packet_DecomposeAt(Packet* packet, PacketDecomposer* decomposer, int fieldCount,
                       va_list* arguments);
void Packet_Release(Packet* packet);

// Convenience wrapper for the two application handlers that build large replies. The original
// emits these objects inline; this class only packages the same three subobjects.
class PacketComposer {
public:
    PacketComposer(u16 command, const char* format, int startIndex = 2);
    ~PacketComposer();

    u8* NewBuffer() { return Packet_NewBuffer(&packet, &util); }
    u16 Compose(int fieldCount, ...);
    u16 ComposeV(int fieldCount, va_list arguments) {
        return Packet_ComposeAtV(&packet, &util, fieldCount, &arguments);
    }
    void BeginRepeat() { util.BeginRepeat(&packet); }
    void EndRepeat() { util.EndRepeat(); }
    void Finalize() {
        if (packet.splitter && packet.last)
            packet.splitter->Finalize(&packet.last->size);
    }
    int Free() const { return Packet_GetFree(&packet); }
    void EnsureFree(int bytes) { if (Free() < bytes) NewBuffer(); }

    Packet packet;
    FragmentPacketSplitter splitter;
    PacketUtil util;

private:
    PacketComposer(const PacketComposer&);
    PacketComposer& operator=(const PacketComposer&);
};

} // namespace blood
