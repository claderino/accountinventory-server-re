// Private implementation details shared by the IOCP core sources of AchievementDaemon.
// (Original: BloodLib headers NetworkInternal-ish; see REPORT_framework_net.md §3.)
#pragma once
#include "Network.h"
#include "Session.h"
#include "Timer.h"
#include "DelayedRelease.h"
#include "PacketBuffer.h"

// This build's LogWrite0 is (file, line, errorCode, apiName, ...). The shared header's LOG0 macro
// still follows the ranking spelling, so use our own three-argument form here.
#undef LOG0
#define LOG0(line, apiName, errorCode) \
    blood::LogWrite0(kSourceFile, (line), (unsigned)(errorCode), (apiName))

namespace blood {

// Completion-context codes stored in OVERLAPPED +0x20 (original: OP_CONNECT / OP_RECV / OP_SEND).
const u32 kOpConnect       = 0x03;
const u32 kOpRecv          = 0x10;
const u32 kOpSend          = 0x12;
const u32 kAcceptContext   = 2;
const u32 kSessionEventClass = 3;   // Session::EventClass(), worker's hard-coded class 3
const u32 kListenerEventClass = 2;  // Listener::EventClass(), worker's hard-coded class 2
const u32 kTimerEventClass = 5;

#ifndef SO_UPDATE_ACCEPT_CONTEXT
#define SO_UPDATE_ACCEPT_CONTEXT 0x700B
#endif
#ifndef SO_UPDATE_CONNECT_CONTEXT
#define SO_UPDATE_CONNECT_CONTEXT 0x7010
#endif
#ifndef TCP_NODELAY
#define TCP_NODELAY 0x0001
#endif

const int kRecvCapacity = 0xFA8;   // 4008 bytes of framed data in the session receive buffer
const int kMinPacketSize = 2;      // TrustedPacketBufferHandler's minimum, stored in Session+0x78
const int kMaxAcceptSlots = 128;
const u32 kOpenPortCapacity = 10000;

// Winsock extension pointers resolved by Network::Initialize (SIO_GET_EXTENSION_FUNCTION_POINTER).
typedef BOOL (PASCAL *BloodAcceptExFn)(SOCKET, SOCKET, PVOID, DWORD, DWORD, DWORD,
                                       LPDWORD, LPOVERLAPPED);
typedef void (PASCAL *BloodGetAcceptExSockaddrsFn)(PVOID, DWORD, DWORD, DWORD,
                                                   LPSOCKADDR*, LPINT, LPSOCKADDR*, LPINT);
typedef BOOL (PASCAL *BloodConnectExFn)(SOCKET, const sockaddr*, int, PVOID, DWORD,
                                        LPDWORD, LPOVERLAPPED);
typedef BOOL (PASCAL *BloodDisconnectExFn)(SOCKET, LPOVERLAPPED, DWORD, DWORD);

extern BloodAcceptExFn             g_acceptEx;
extern BloodGetAcceptExSockaddrsFn g_getAcceptExSockaddrs;
extern BloodConnectExFn            g_connectEx;
extern BloodDisconnectExFn         g_disconnectEx;

bool IsNetRunning();
HANDLE NetIocp();
HANDLE NetIoCompletionPort();   // alias used by the worker; NULL until Initialize

// ---------------------------------------------------------------------------------------------
// OVERLAPPED layouts. The completion context is the first dword after OVERLAPPED (+0x20), as in
// the original image; the operation data follows at the verified offsets.
// ---------------------------------------------------------------------------------------------

// One of the 128 AcceptEx slots of a Listener (stride 0xC8).
struct OVERLAPPED_EX {
    OVERLAPPED overlapped;      // +0x00
    u32 context;                // +0x20: kAcceptContext
    u32 reserved0;              // +0x24
    u32 index;                  // +0x28 accept-slot index
    u32 reserved1;              // +0x2C
    SOCKET socket;              // +0x30 accept socket
    u8 address[0x80];           // +0x38 local(0x20) + remote(0x20) address buffer
    u32 localLength;            // +0xB8
    u32 remoteLength;           // +0xBC
};

// Receive completion context (embedded in the Session at +0x1000 in the original).
struct RecvContext {
    void* allocatorVtbl;        // +0x00 static-page allocator vtable
    OVERLAPPED overlapped;      // +0x08, passed to WSARecv / IOCP
    u32 context;                // +0x28: kOpRecv
    u32 reserved0;              // +0x2C
    u64 reserved1;              // +0x30
    WSABUF buf;                 // +0x38
    u32 bytesReceived;          // +0x48
    u8 data[kRecvCapacity];     // +0x4C
};

// Send descriptor: 42 gathered buffers initially, doubled on growth. One PacketBuffer
// reference is held for each entry until the IOCP completion retires the group.
struct SendBufferDesc {
    u8 allocated;               // +0x00
    u32 capacity;               // +0x04
    u32 count;                  // +0x08
    WSABUF* buffers;            // +0x10
    PacketBuffer** packets;     // +0x18
    SendBufferDesc* next;       // +0x20
};

// Send completion / queue context (embedded in the Session at +0x1A0).
struct SendContext {
    void* vtbl;                 // +0x00 vtable 0x794268
    OVERLAPPED overlapped;      // +0x08, passed to WSASend / IOCP
    u32 context;                // +0x28: kOpSend
    u32 reserved0;              // +0x2C
    Session* session;           // +0x30
    SendBufferDesc* sending;    // +0x38 group currently on the wire
    void* prototype;            // +0x40 packet splitter prototype (unused here)
    SendBufferDesc descs[2];    // +0x48, +0x70
    SendBufferDesc* queued;     // +0x98 descriptor being filled
    u32 highWater;              // +0xA0
};

// Reconnect context (original ConnectionCtx, Session+0xD8). Dormant in this build: no code in the
// image allocates it, and the application always passes option 1.
struct ConnectionCtx {
    OVERLAPPED overlapped;      // +0x00
    u32 context;                // +0x20: kOpConnect
    u32 reserved0;              // +0x24
    u32 reserved1;              // +0x28
    u32 reserved2;              // +0x2C
    SOCKET socket;              // +0x30
    sockaddr_in address;        // +0x38
    u8 connected;               // +0x48
    u8 reserved3[7];
    u64 deadline;               // +0x50
    u32 retryCount;             // +0x58
};

// Simple refcounted IOCP job. The creator's reference is transferred to the completion port on a
// successful PostQueuedCompletionStatus; HandleEvent releases it after running the callback.
class DelegateJob : public NetEventObject {
public:
    typedef bool (*Handler)(DelegateJob* job, OVERLAPPED* ov, u32 bytes);
    DelegateJob(u32 eventClass, u64 id, Handler handler, void* context);

    u32 EventClass() override { return eventClass_; }
    bool HandleEvent(OVERLAPPED* ov, u32 bytes) override;

    u64 id() const { return id_; }
    void* context() const { return context_; }

private:
    u32 eventClass_;
    u64 id_;
    Handler handler_;
    void* context_;
};

// Outgoing send queue (original: SessionBuffer.cpp, 2FE10/2FFB0/30170/30210/302D0/30440).
bool SessionSendBufferInitialize(SendContext* ctx, unsigned char* backingA, unsigned char* backingB);
void SessionSendBufferClear(SendContext* ctx);
SendBufferDesc* SessionSendBufferAdd(Session* session, Packet* packet);
SendBufferDesc* SessionSendBufferRemoveSent(Session* session);
void SendBufferDescGrow(SendBufferDesc* desc);
void SendBufferDescReleasePackets(SendBufferDesc* desc);

// One reference is added for every posted AcceptEx/WSARecv/WSASend and consumed by the worker
// when that completion is dispatched. These helpers make the ownership transfer explicit.
void PostOperationAddRef(EventObject* key);
void CompleteOperationRelease(EventObject* key);

} // namespace blood
