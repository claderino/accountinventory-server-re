// BloodLib - one TCP session (original: source\Session.cpp; receive/send helpers in Packet.cpp).
#include "NetworkInternal.h"
#include "PacketBufferHandler.h"
#include "PacketFormat.h"
#include "Log.h"
#include <stddef.h>

SOURCE_FILE("Session.cpp");

namespace blood {

u64 Session_GetId(const Session* session) { return session ? session->id : 0; }

// Free entry point expected by the application's channel send helpers (declared in BloodLib.h).
void Session_SendPacket(Session* session, const char* fmt, va_list* args) {
    Session::SendPacket(session, fmt, *args);
}

Session::Session(Port* port, u32 portId, PacketBufferHandler* prototype)
    : id(0), portId(portId), state(Idle), option(0),
      recvPending(false), sendPending(false), connectInProgress(false), unbound(false),
      lastActivity(0), diagBitmap(0), port(port), channel(NULL),
      connection(NULL), recv(NULL), send(NULL), minPacketSize(kMinPacketSize),
      validator(NULL), splitterPrototype(NULL), reconnectTimer(NULL), reconnectCtx(NULL),
      pendingIo_(0), teardownPending_(false), idleEvent_(NULL) {
    tag = 0x70A;
    InitializeCriticalSectionAndSpinCount(&lock, 4000);
    idleEvent_ = CreateEventW(NULL, TRUE, FALSE, NULL);
    connection = new Connection;
    recv = new RecvContext;
    send = new SendContext;
    memset(recv, 0, sizeof(*recv));
    memset(send, 0, sizeof(*send));
    recv->allocatorVtbl = NULL;
    recv->context = kOpRecv;
    send->context = kOpSend;
    send->session = this;
    SessionSendBufferInitialize(send, NULL, NULL);
    if (prototype != NULL) {
        validator = prototype;   // the port clones the real handler before calling us
        minPacketSize = kMinPacketSize;
    }
}

Session::~Session() {
    // Clear both sides of the channel binding BEFORE releasing either object (SAFETY).
    if (channel != NULL) {
        Channel* ch = channel;
        if (ch->session == this) {
            ch->session = NULL;
            ch->sessionInvalid = true;
            // Release the session's channel reference outside the channel's lock.
            ch->Release();
        }
        channel = NULL;
    }
    connection->Close();
    ClearSendBuffers();
    if (idleEvent_ != NULL) CloseHandle(idleEvent_);
    delete send;
    delete recv;
    delete connection;
    DeleteCriticalSection(&lock);
}

bool Session::OnLastRelease() {
    // vtable slot 2 of the original: find the Port and return the Session to its pool.
    DestroyBody();
    return true;
}

void Session::DestroyBody() {
    if (port != NULL) port->RecycleSession(this);
    else delete this;
}

void Session::SetSocket(SOCKET s) { connection->pendingSocket_ = s; }
SOCKET Session::Socket() const { return connection->socket; }

Channel* Session::GetChannel() {
    if (channel == NULL || unbound) return NULL;
    channel->AddRef();
    return channel;
}

bool Session::Initialize(Port* owner, u64 sessionId, u32 opt) {
    id = sessionId;
    portId = (u32)(sessionId >> 32);
    port = owner;
    option = opt;
    state = Idle;
    recvPending = false;
    sendPending = false;
    connectInProgress = false;
    lastActivity = DaemonMillis();
    recv->bytesReceived = 0;
    ClearSendBuffers();
    return true;
}

bool Session::OnAccept(Port* owner, u64* acceptedSocket, u64 sessionId,
                       in_addr ip, u16 peerPort, u32 opt) {
    Initialize(owner, sessionId, opt);
    SessionHandler* mgr = owner ? owner->Handler() : NULL;
    // The original calls the manager BEFORE associating the accepted socket.
    if (mgr != NULL && !mgr->OnAccept(this)) {
        LOG(0, 460, L"[session], OnAccept failed, id:%I64x, this:%p", id, this);
        state = Idle;
        return false;
    }
    SOCKET accepted = acceptedSocket ? (SOCKET)*acceptedSocket : INVALID_SOCKET;
    bool ok = connection->Accept(accepted, (ULONG_PTR)this,
                                 owner ? owner->iocp_ : NetIocp(),
                                 ip.S_un.S_addr, peerPort);
    if (!ok) {
        LOG(0, 448, L"[session], Connection::Accept failed, id:%I64x, this:%p, error:%d",
            id, this, WSAGetLastError());
        state = Idle;
        return false;
    }
    connection->address = ip;
    connection->port = peerPort;
    state = Connected;
    LOG(1, 471,
        L"[session], accepted from, port id: %u, session id:%I64x, this:%p, ip:%s, port:%d, socket:%p",
        portId, id, this, IpToWideString(ip.S_un.S_addr), (int)peerPort,
        (void*)(ULONG_PTR)Socket());
    return true;
}

void Session::AddPendingIo() {
    InterlockedIncrement(&pendingIo_);
    AddRef();
}

void Session::ReleasePendingIo() {
    LONG n = InterlockedDecrement(&pendingIo_);
    if (n <= 0 && teardownPending_ && idleEvent_ != NULL) SetEvent(idleEvent_);
    Release();
}

void Session::WaitForPendingIo(u32 timeoutMs) {
    if (idleEvent_ == NULL || pendingIo_ == 0) return;
    WaitForSingleObject(idleEvent_, timeoutMs);
}

void Session::BeginTeardown() {
    teardownPending_ = true;
    EnterCriticalSection(&lock);
    if (connection->socket != INVALID_SOCKET) {
        CancelIoEx((HANDLE)connection->socket, NULL);
        connection->Close();
    }
    recvPending = false;
    sendPending = false;
    LeaveCriticalSection(&lock);
    WaitForPendingIo(2000);
}

bool Session::StartRecv() {
    if (state != Connected || Socket() == INVALID_SOCKET) {
        LOG(1, 700, L"[session], recv error, not connected, id:%I64x, this:%p, state:%d",
            id, this, (int)state);
        return false;
    }
    recv->buf.len = kRecvCapacity - recv->bytesReceived;
    recv->buf.buf = reinterpret_cast<char*>(recv->data + recv->bytesReceived);
    AddPendingIo();                       // SAFETY: one reference per posted WSARecv
    if (!connection->AsyncRecv(&recv->overlapped)) {
        ReleasePendingIo();
        LOG(0, 712,
            L"[session], recv error; Connection::AsyncRecv failed, id:%I64x, this:%p, error:%d",
            id, this, WSAGetLastError());
        return false;
    }
    recvPending = true;
    return true;
}

bool Session::StartRecvLocked() {
    EnterCriticalSection(&lock);
    bool ok = StartRecv();
    LeaveCriticalSection(&lock);
    return ok;
}

bool Session::ProcessRecv() {
    u32 used = recv->bytesReceived;
    u32 consumed = 0;
    TrustedPacketBufferHandler validator;
    while (minPacketSize <= used - consumed) {
        u16* hdr = reinterpret_cast<u16*>(recv->data + consumed);
        u16* out = NULL;
        int n = validator.Validate(this, hdr, (int)(used - consumed), &out);
        if (n < 0) return false;
        if (n == 0) break;
        if (out == NULL) {
            CRASH(0x235, L"== CRASH == ASSERT CRASH");
            return false;
        }
        // Non-owning view into the receive buffer; never AddRef/Release or write through it.
        Packet view;
        view.splitter = NULL;
        view.current = reinterpret_cast<PacketBuffer*>((u8*)hdr - offsetof(PacketBuffer, size));
        view.first = view.current;
        view.last = view.current;
        SessionHandler* mgr = port ? port->Handler() : NULL;
        if (mgr == NULL || !mgr->PacketHandler(this, &view)) {
            LOG(0, 772,
                L"[session], packet handler failed, id:%I64x, this:%p, port-running:%s",
                id, this, port != NULL && port->IsOpened() ? L"true" : L"false");
            return false;
        }
        consumed += (u32)n;
    }
    if (used < consumed) {
        LOG(0, 978, L"[session], clear recv buffer error, id:%I64x, this:%p", id, this);
        return false;
    }
    if (consumed < used) memmove(recv->data, recv->data + consumed, used - consumed);
    recv->bytesReceived = used - consumed;
    return true;
}

bool Session::HandleEvent(OVERLAPPED* ov, u32 bytes) {
    if (ov == NULL) return false;
    u32 context = *reinterpret_cast<u32*>((u8*)ov + 0x20);
    if (context == kOpConnect) {
        if (reconnectCtx == NULL || !connection->CompleteAsyncConnect(reconnectCtx)) return false;
        EnterCriticalSection(&lock);
        state = Connected;
        connectInProgress = false;
        ++diagBitmap;
        LeaveCriticalSection(&lock);
        SessionHandler* mgr = port ? port->Handler() : NULL;
        if (mgr == NULL || !mgr->OnConnect(this)) {
            if (mgr) mgr->OnClose(this, option == 2);
            return false;
        }
        if (!StartRecvLocked()) {
            if (mgr) mgr->OnClose(this, option == 2);
            return false;
        }
    } else if (context == kOpRecv) {
        if (bytes == 0) {
            LOG(0, 760, L"[session], recv zero; socket has closed, id:%I64x, this:%p", id, this);
            return false;
        }
        u32 total = recv->bytesReceived + bytes;
        if (total > kRecvCapacity || total == 0) {
            LOG(0, 765,
                L"[session], invalid received size, id:%I64x, this:%p, bytes:%d",
                id, this, (int)total);
            return false;
        }
        recv->bytesReceived = total;
        if (!ProcessRecv()) return false;
        if (!StartRecvLocked()) return false;
        lastActivity = DaemonMillis();
        diagBitmap++;
    } else if (context == kOpSend) {
        EnterCriticalSection(&lock);
        bool ok = OnSendComplete();
        LeaveCriticalSection(&lock);
        if (!ok) return false;
    } else {
        CRASH(0x34A, L"== CRASH == [session], invalid event, id:%I64x, this:%p, context:%d",
              id, this, (int)context);
        return false;
    }
    return true;
}

bool Session::OnFailedEvent(OVERLAPPED* ov) {
    u32 context = ov != NULL ? *reinterpret_cast<u32*>((u8*)ov + 0x20) : 0;
    EnterCriticalSection(&lock);
    if (context == kOpConnect) connectInProgress = false;
    else if (context == kOpRecv) {
        --diagBitmap;
        recvPending = false;
    } else if (context == kOpSend) {
        sendPending = false;
        ClearSendBuffers();
    } else {
        CRASH(0x38C, L"== CRASH == [session], invalid event, id:%I64x, this:%p, context:%d",
              id, this, (int)context);
    }
    bool remove = Close(false);
    LeaveCriticalSection(&lock);
    SessionHandler* mgr = port ? port->Handler() : NULL;
    if (mgr) mgr->OnClose(this, option == 2);
    if (remove) RemoveFromPort();
    return true;
}

bool Session::OnSendComplete() {
    if ((state != Connected && state != Closing) || Socket() == INVALID_SOCKET) return false;
    SendBufferDesc* next = SessionSendBufferRemoveSent(this);
    if (next != NULL) {
        send->context = kOpSend;
        send->sending = next;
        AddPendingIo();                   // SAFETY: one reference per posted WSASend
        if (!connection->AsyncSend(&send->overlapped)) {
            ReleasePendingIo();
            return false;
        }
        sendPending = true;
        return true;
    }
    if (state != Closing) {
        sendPending = false;
        return true;
    }
    return false;
}

bool Session::QueueAndSend(Packet* packet) {
    if (state != Connected || Socket() == INVALID_SOCKET) {
        LOG(0, 1254,
            L"[session], cannot send packet; async send failed, id:%I64x, this:%p, "
            L"state:%d, socket:%I32x, error:%d",
            id, this, (int)state, (int)(i32)Socket(), WSAGetLastError());
        return false;
    }
    SendBufferDesc* d = SessionSendBufferAdd(this, packet);
    if (d == NULL) return true;
    send->context = kOpSend;
    send->sending = d;
    AddPendingIo();                       // SAFETY: one reference per posted WSASend
    if (!connection->AsyncSend(&send->overlapped)) {
        ReleasePendingIo();
        send->sending = NULL;
        LOG(0, 1254,
            L"[session], cannot send packet; async send failed, id:%I64x, this:%p, "
            L"state:%d, socket:%I32x, error:%d",
            id, this, (int)state, (int)(i32)Socket(), WSAGetLastError());
        return false;
    }
    sendPending = true;
    return true;
}

void Session::SendPacket(Session* s, const char* fmt, va_list ap) {
    if (fmt == NULL) {
        CRASH(0x46D, L"== CRASH == ASSERT CRASH");
        return;
    }
    PacketBuffer* buf = PacketBuffer::Acquire();
    if (buf == NULL) return;
    buf->size = 2;
    u16 n = ComposeFormatV(buf->Fields(), 0xF9E, fmt, 0, ap);
    buf->size = (u16)(buf->size + n);
    Packet p;
    p.splitter = NULL;
    p.current = p.first = p.last = buf;
    EnterCriticalSection(&s->lock);
    s->QueueAndSend(&p);
    LeaveCriticalSection(&s->lock);
    buf->Release(kSourceFile, 0);
}

void Session::SendFmt(Session* s, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    SendPacket(s, fmt, ap);
    va_end(ap);
}

void Session::SendPacketPtr(Session* s, Packet* packet) {
    if (s == NULL || packet == NULL || packet->first == NULL) return;
    EnterCriticalSection(&s->lock);
    s->QueueAndSend(packet);
    LeaveCriticalSection(&s->lock);
}

void Session::ClearSendBuffers() { SessionSendBufferClear(send); }
void Session::RemoveFromPort() { if (port != NULL) port->RemoveSession(this); }

bool Session::Close(bool force) {
    if (state == Closed || force) {
        state = Closed;
        connection->Close();
    } else {
        state = Closing;
        if (!sendPending) connection->Close();
        else LOG(1, 641, L"[session], something remain to send, id:%I64x, this:%p", id, this);
    }
    // The original's state==4 branch here is unreachable (state was overwritten above).
    // Reconnect option 2 is rejected explicitly instead of dereferencing the dormant context.
    if (option == 2) {
        LOG(0, 650, L"[session], reconnect is not supported, id:%I64x, this:%p", id, this);
    }
    if (state != Idle && !recvPending && !sendPending && !connectInProgress &&
        Socket() == INVALID_SOCKET) {
        recv->bytesReceived = 0;
        ClearSendBuffers();
        state = Idle;
        return true;
    }
    return false;
}

bool Session::TryReconnect() {
    // No code in this image allocates ConnectionCtx or installs a Timer; option 2 is rejected.
    LOG(0, 700, L"[session], reconnect is not supported, id:%I64x, this:%p", id, this);
    return false;
}

bool Session::Connect() { return TryReconnect(); }

} // namespace blood
