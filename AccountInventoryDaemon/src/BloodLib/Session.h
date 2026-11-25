// BloodLib - one TCP session of a port (original: Session.cpp / SessionBuffer.cpp / Packet.cpp).
//
// The AchievementDaemon Session is a six-slot EventObject (vtable 0x92DA0):
//   slot 0 scalar deleting destructor thunk (0x28EE0)
//   slot 1 RefCnt destructor dispatch (0x15B0)
//   slot 2 destructor / recycle body (0x2A7A0)
//   slot 3 Release helper (0x15C0)
//   slot 4 EventClass() -> 3 (0x0AC30)
//   slot 5 HandleEvent() (0x29DE0)
// There is no virtual OnFailedEvent and no ChannelManager::SendFmt path in this build.
#pragma once
#include "BloodLib.h"
#include "Channel.h"
#include "Packet.h"

namespace blood {

class Port;
class Channel;
struct RecvContext;
struct SendContext;
struct ConnectionCtx;
struct Packet;

class Connection {
public:
    Connection();
    ~Connection();

    bool Accept(ULONG_PTR completionKey, HANDLE iocpHandle, u32 ip, u16 peerPort);
    bool Accept(SOCKET accepted, ULONG_PTR completionKey, HANDLE iocpHandle, u32 ip, u16 peerPort);
    bool ConnectAndAssociate(ConnectionCtx* ctx, bool noDelayDisabled);
    bool CompleteAsyncConnect(ConnectionCtx* ctx);
    static SOCKET CreateConnectSocket(bool noDelayDisabled);
    bool AsyncRecv(OVERLAPPED* ov);
    bool AsyncSend(OVERLAPPED* ov);
    void Close();

    SOCKET socket;           // +0x08 INVALID_SOCKET when closed
    in_addr address;         // +0x10 remote IPv4, network byte order
    u16 port;                // +0x14 remote port
    HANDLE iocp;

private:
    Connection(const Connection&);
    Connection& operator=(const Connection&);
    SOCKET pendingSocket_;
    Session* owner_;
    friend class Session;
};

// Accessor used by PacketBufferHandler.cpp without including Session.h.
u64 Session_GetId(const Session* session);

class Session : public NetEventObject {
public:
    enum State {
        Idle = 0, Closing = 1, Closed = 2, Connecting = 3,
        ReconnectWait = 4, Connected = 5
    };
    static const u32 kEventClass = 3;

    Session(Port* port, u32 portId, PacketBufferHandler* prototype);
    virtual ~Session();

    // --- data used by the application --------------------------------------------------------
    u64 id;                      // +0x68  ((u64)portId << 32) | slot
    u32 portId;                  // +0x70
    u32 state;                   // +0x50
    u32 option;                  // +0x54 / +0x100
    bool recvPending;            // +0x58
    bool sendPending;            // +0x59
    bool connectInProgress;      // +0x5A
    bool unbound;                // +0xD0
    u64 lastActivity;            // +0x80 daemon tick of the last accepted packet
    u64 diagBitmap;              // +0x88 diagnostic bit masks (meaning UNVERIFIED)
    CRITICAL_SECTION lock;       // +0x18 serialises send / receive start
    Port* port;                  // +0xB8 owning port (one reference held)
    Channel* channel;            // +0xC0 bound channel (one reference held)
    Connection* connection;      // +0x188 embedded connection
    RecvContext* recv;           // +0x1000 receive completion context
    SendContext* send;           // +0x1A0 send queue / completion context
    u32 minPacketSize;           // +0x78 from the packet-handler prototype (2)
    PacketBufferHandler* validator;    // +0xE8 per-session clone
    void* splitterPrototype;     // +0xF0 (unused in this build)
    Timer* reconnectTimer;       // +0xF8 (dormant)
    ConnectionCtx* reconnectCtx; // +0xD8 (dormant: never allocated in this image)

    // --- API used by application code ----------------------------------------------------------
    Channel* GetChannel();                            // AddRef'd bound channel, or NULL
    static Channel* GetBoundChannel(Session* s) { return s ? s->GetChannel() : NULL; }
    bool IsAuthed() const { return channel != NULL && !unbound; }
    u64 IdleTime() const { return DaemonMillis() - lastActivity; }
    SOCKET Socket() const;

    bool Initialize(Port* owner, u64 sessionId, u32 opt);
    bool OnAccept(Port* owner, u64* acceptedSocket, u64 sessionId, in_addr ip, u16 peerPort, u32 opt);
    bool StartRecv();
    bool StartRecvLocked();
    bool Close(bool force);
    void RemoveFromPort();
    void BeginTeardown();                             // SAFETY: cancel and await outstanding I/O

    static void SendFmt(Session* s, const char* fmt, ...);
    static void SendPacket(Session* s, const char* fmt, va_list ap);
    static void SendPacketPtr(Session* s, Packet* packet);   // application-side helper

    // --- IOCP event object ---------------------------------------------------------------------
    u32 EventClass() override { return kEventClass; }
    bool HandleEvent(OVERLAPPED* ov, u32 bytes) override;
    bool OnFailedEvent(OVERLAPPED* ov) override;
    bool OnSendComplete();                            // 2A7E0: called with the send lock held

    // SAFETY bookkeeping for per-operation references (see IMPL_framework_net.md).
    void AddPendingIo();
    void ReleasePendingIo();
    bool HasPendingIo() const { return pendingIo_ != 0; }
    void WaitForPendingIo(u32 timeoutMs);

private:
    Session(const Session&);
    Session& operator=(const Session&);
    friend class Connection;
    friend class Port;
    friend class Channel;
    friend class SessionSendBuffer;

    bool ProcessRecv();
    bool QueueAndSend(Packet* packet);
    bool TryReconnect();
    bool Connect();
    void SetSocket(SOCKET s);
    void ClearSendBuffers();
    bool OnLastRelease() override;                    // last Release: recycle via the SessionPool
    void DestroyBody();                               // vtable slot 2 target: recycle via the Port

    volatile long pendingIo_;
    bool teardownPending_;
    HANDLE idleEvent_;
};

} // namespace blood
