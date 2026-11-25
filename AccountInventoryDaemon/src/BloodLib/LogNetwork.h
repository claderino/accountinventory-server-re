// BloodLib - network log medium (original: LogNetwork.cpp).
#pragma once
#include "Log.h"

namespace blood {

class Session;
class PacketBuffer;

class LogNetwork : public LogMedia {
public:
    LogNetwork();
    virtual ~LogNetwork();

    void Flush() override {}
    void WriteLog(LogBuffer* head) override;
    bool Attach(Session* session);
    void Detach(Session* session);

    u32 queuedCount;              // +0xB8
    u32 reservedBC;               // +0xBC
    CRITICAL_SECTION connectionLock; // +0xC0
    Session* sessions[16];           // +0xE8..+0x167
};

LogNetwork* LogNetwork_Create();
void LogNetwork_ReleaseMedia();

// Session owner performs codec + enqueue for a completed log packet.
bool Session_SendLogPacket(Session* session, PacketBuffer* packet);

} // namespace blood
