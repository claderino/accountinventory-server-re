// BloodLib - send-side packet fragmentation (original: ..\Packet\FragmentPacket.cpp).
// This build has no receive-side concatenater/reassembly code.
#pragma once
#include "BloodLib.h"

namespace blood {

class FragmentPacketSplitter {
public:
    FragmentPacketSplitter();

    void SetCommand(u16 command) { command_ = command; }
    u16 GetCommand() const { return command_; }

    // Three-slot vtable, in this exact order.
    virtual bool AddBuffer(u16* previousHeader, u16* newHeader); // +0x00
    virtual bool Finalize(u16* lastHeader);                      // +0x08
    virtual void Clear();                                       // +0x10

    u16 command_;            // +0x08
    u32* first_;             // +0x10 running total in the first fragment
    u32* last_;              // +0x18 most recent fragment flag
    u64 unused_;             // +0x20
};

} // namespace blood
