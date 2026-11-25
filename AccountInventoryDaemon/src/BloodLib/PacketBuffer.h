// BloodLib - reference-counted 4000-byte wire buffer (original: PacketBuffer.h).
#pragma once
#include "BloodLib.h"

namespace blood {

class Crypto;

#pragma pack(push, 1)
class PacketBuffer {
public:
    static const int kCapacity = kMaxPacketSize - 2;
    static PacketBuffer* Acquire();

    virtual ~PacketBuffer() {}
    virtual void Release(const wchar_t* file, int line); // vtable +0x08

    void AddRef() { InterlockedIncrement(&refCount); }
    int FreeSize() const { return kCapacity - size; }
    u8* Fields() { return fields; }
    u8* Plaintext() { return reinterpret_cast<u8*>(this) + 0x1E; }
    const u8* Fields() const { return fields; }
    u8* End() { return fields + (size - 2); }
    const u8* End() const { return fields + (size - 2); }

    u32 tag;                   // +0x08 0x50A
    volatile LONG refCount;    // +0x0C
    u64 scratch;               // +0x10
    u8 reserved18[8];          // +0x18..0x1F
    u16 size;                  // +0x20 wire size including this word
    u8 fields[kCapacity];      // +0x22
    u32 cipherLength;          // +0xFC0
    u32 reservedFC4;           // +0xFC4
    u8* cipherData;            // +0xFC8
    PacketBuffer* next;        // +0xFD0 chain / pool link
};
#pragma pack(pop)

void PacketBuffer_InitTLS();
bool Packet_EncryptChain(PacketBuffer* first, Crypto* encrypt, Crypto* decrypt);

} // namespace blood
