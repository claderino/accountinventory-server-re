// BloodLib - raw TCP alert sender (original: AlertSender.h).
#pragma once
#include "Thread.h"

namespace blood {

class AlertSender : public ThreadObject {
public:
    AlertSender();

    const wchar_t* Name() const override { return L"AlertSender"; }
    bool Run() override;
    void Stop() override;

    bool OpenSocket();
    sockaddr_in& Address() { return *reinterpret_cast<sockaddr_in*>(address); }
    const sockaddr_in& Address() const { return *reinterpret_cast<const sockaddr_in*>(address); }
    bool Initialize(const wchar_t* address);
    void Shutdown();
    void EnqueueAssert(const wchar_t* file, int line, const wchar_t* format, va_list* arguments);

    SOCKET socket;                 // +0x30
    WSAEVENT socketEvent;          // +0x38
    HANDLE queueEvent;             // +0x40
    const wchar_t* threadName;     // +0x48
    u32 maxQueue;                  // +0x50
    u16 reserved54;                 // +0x54
    u8 address[16];                 // +0x56 sockaddr_in, intentionally unaligned
    u8 flag6C;                     // +0x6C
    u8 reserved6D[0x103];          // +0x6D..0x16F
    CRITICAL_SECTION queueLock;    // +0x170
    void* queueHead;               // +0x198
    void* queueTail;               // +0x1A0
    volatile LONG queueCount;      // +0x1A8
};

void AlertSender_Startup();
void AlertSender_Shutdown();
bool AlertSender_Initialize(const wchar_t* address);

} // namespace blood
