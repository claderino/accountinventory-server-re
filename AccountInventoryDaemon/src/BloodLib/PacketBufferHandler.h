// BloodLib - trusted receive framing validator (original: PacketBufferHandler.h).
#pragma once
#include "BloodLib.h"

namespace blood {

class PacketBuffer;
class Session;

class TrustedPacketBufferHandler {
public:
    // Seven-slot original vtable. Only Validate/GetSize are used by the daemon.
    virtual void Unknown0() {}
    virtual void* Create(void* object) { return object ? object : new TrustedPacketBufferHandler(); }
    virtual void Destroy(void*, void* memory) { Memory_Free(memory); }
    virtual bool Unknown3() { return true; }
    virtual int Unknown4() { return 0; }
    virtual int Validate(Session* session, const u16* header, int available,
                         PacketBuffer** output);
    virtual int GetSize(PacketBuffer** output, const u16* header) {
        if (output) *output = reinterpret_cast<PacketBuffer*>(const_cast<u16*>(header));
        return *header;
    }

    int Validate(Session* session, const u16* header, int available, u16** output) {
        PacketBuffer* buffer = NULL;
        int result = Validate(session, header, available, &buffer);
        if (output) *output = reinterpret_cast<u16*>(buffer);
        return result;
    }
};

} // namespace blood
