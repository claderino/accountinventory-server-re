// BloodLib - repeat-aware packet format cursors (original: PacketUtil.h / PacketUtil.cpp).
#pragma once
#include "BloodLib.h"

namespace blood {

struct Packet;

class PacketFormatIterator {
public:
    virtual void Reset() = 0;                                      // +0x00
    virtual void SetFormat(const char* format, int startIndex) = 0; // +0x08
    virtual void AddIndex(int count) = 0;                           // +0x10
    virtual void SubIndex(int count) = 0;                           // +0x18
    virtual void AdjustCounts(Packet* packet, void* oldEnd) = 0;     // +0x20
    virtual const char* CurrentFormat() = 0;                        // +0x28
    virtual int GetIndex() = 0;                                    // +0x30
    virtual void SkipRepeatBlock() = 0;                            // +0x38

    const char* format; // +0x08
    int formatLen;      // +0x10
    int index;          // +0x14
};

class RepeatPacketComposer {
public:
    RepeatPacketComposer();

    virtual void Reset();
    virtual void SetFormat(const char* format, int startIndex);
    virtual void AddIndex(int count);
    virtual void SubIndex(int count);
    virtual void AdjustCounts(Packet* packet, void* oldEnd);
    virtual const char* CurrentFormat();
    virtual int GetIndex();
    virtual void SkipRepeatBlock();

    void BeginRepeat(Packet* packet);
    void EndRepeat();
    void NextElement();

    const char* format;        // +0x08
    int formatLen;             // +0x10
    int index;                 // +0x14
    u16* countSlot[3];         // +0x18
    int startIndex[3];// +0x30
    int depth;        // +0x3C (-1 = none)
    struct SubFormat {
        u8* countByte;
        u8* savedEnd;
        const char* format;
        int index;
    } sub[3];          // +0x40, stride 0x20
    int subTop;        // +0xA0
};

class RepeatPacketDecomposer {
public:
    RepeatPacketDecomposer();

    virtual void Reset();
    virtual void SetFormat(const char* format, int startIndex);
    virtual void AddIndex(int count);
    virtual void SubIndex(int count);
    virtual void AdjustCounts(Packet*, void*) {}
    virtual const char* CurrentFormat();
    virtual int GetIndex();
    virtual void SkipRepeatBlock();

    u16 BeginRepeat(const u16* wireCount, u16 available);

    const char* format;   // +0x08
    int formatLen;        // +0x10
    int index;            // +0x14
    u32 offset;           // +0x18 byte offset in the current packet buffer
    u16 reserved1C;       // +0x1C
    u8 reserved1E[2];     // +0x1E
    u16 counts[3];     // +0x20 inline count values
    int starts[3];     // +0x28
    int depth;         // +0x34 (-1 = none)
};

typedef RepeatPacketComposer PacketUtil;
typedef RepeatPacketDecomposer PacketDecomposer;

} // namespace blood
