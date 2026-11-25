// BloodLib - AcceptEx listener (original: source\Listener.cpp).
#include "NetworkInternal.h"
#include "Log.h"

SOURCE_FILE("Listener.cpp");

namespace blood {

struct Listener::Impl {
    OVERLAPPED_EX slots[kMaxAcceptSlots];
};

Listener::Listener(Port* port)
    : port_(port), listenSocket(INVALID_SOCKET), impl_(new Impl) {
    // The original never AddRefs the Port from the Listener; Port is a non-owning back pointer.
}

Listener::~Listener() {
    CloseListenSocket();
    delete impl_;
    impl_ = NULL;
}

void Listener::CloseListenSocket() {
    SOCKET s = (SOCKET)InterlockedExchangePointer(reinterpret_cast<PVOID volatile*>(&listenSocket),
                                                  reinterpret_cast<PVOID>(INVALID_SOCKET));
    if (s != INVALID_SOCKET) {
        closesocket(s);
        Release();                 // the self-reference taken by Initialize
    }
    if (impl_ != NULL) {
        for (int i = 0; i < kMaxAcceptSlots; ++i) {
            OVERLAPPED_EX& slot = impl_->slots[i];
            SOCKET accepted = (SOCKET)InterlockedExchangePointer(
                reinterpret_cast<PVOID volatile*>(&slot.socket),
                reinterpret_cast<PVOID>(INVALID_SOCKET));
            if (accepted != INVALID_SOCKET) closesocket(accepted);
        }
    }
}

bool Listener::Initialize(HANDLE iocp, u16 portNumber) {
    if (listenSocket != INVALID_SOCKET) {
        CRASH(0x22, L"== CRASH == ASSERT CRASH");
        return false;
    }
    memset(impl_->slots, 0, sizeof(impl_->slots));
    SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
        LOG0(82, L"WSASocket()", (u32)WSAGetLastError());
        return false;
    }
    sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_port = htons(portNumber);
    sin.sin_addr.S_un.S_addr = INADDR_ANY;
    if (bind(s, (sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
        LOG0(82, L"bind()", (u32)WSAGetLastError());
        closesocket(s);
        return false;
    }
    // Port+0x12 is always 1 in this build, so TCP_NODELAY is never enabled.
    listenSocket = s;
    AddRef();                      // self-reference for the live listen socket (line 0x47)
    if (CreateIoCompletionPort((HANDLE)s, iocp, (ULONG_PTR)this, 0) == NULL) {
        LOG0(82, L"CreateIoCompletionPort()", (u32)GetLastError());
        CloseListenSocket();
        return false;
    }
    if (listen(s, 128) == SOCKET_ERROR) {
        LOG0(82, L"bind()", (u32)WSAGetLastError());   // original logs bind() for listen failure
        CloseListenSocket();
        return false;
    }
    for (int i = 0; i < kMaxAcceptSlots; ++i) {
        OVERLAPPED_EX& slot = impl_->slots[i];
        memset(&slot, 0, sizeof(slot));
        slot.context = kAcceptContext;
        slot.index = (u32)i;
        slot.socket = INVALID_SOCKET;
        slot.localLength = 0x20;
        slot.remoteLength = 0x20;
        if (!PostAccept(i)) return false;
    }
    return true;
}

bool Listener::PostAccept(int slotIndex) {
    if (slotIndex < 0 || slotIndex >= kMaxAcceptSlots) {
        CRASH(0xBA, L"== CRASH == ASSERT CRASH");
        return false;
    }
    OVERLAPPED_EX& slot = impl_->slots[slotIndex];
    if (slot.socket == INVALID_SOCKET) {
        slot.socket = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
        if (slot.socket == INVALID_SOCKET) {
            LOG0(0xA0, L"WSASocket()", (u32)WSAGetLastError());
            return false;
        }
    }
    // SAFETY: one reference per posted AcceptEx, consumed by the worker on completion.
    AddRef();
    DWORD received = 0;
    BOOL ok = g_acceptEx(listenSocket, slot.socket, slot.address, 0, 0x20, 0x20,
                         &received, &slot.overlapped);
    if (ok == FALSE) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            LOG0(0xAE, L"AcceptEx()", (u32)err);
            Release();
            closesocket(slot.socket);
            slot.socket = INVALID_SOCKET;
            return false;
        }
    }
    return true;
}

bool Listener::CompleteAccept(OVERLAPPED* ov) {
    OVERLAPPED_EX* slot = reinterpret_cast<OVERLAPPED_EX*>(ov);
    if (slot == NULL || slot->index >= kMaxAcceptSlots) {
        CRASH(0xBA, L"== CRASH == ASSERT CRASH");
        return false;
    }
    if (port_ == NULL || !port_->IsOpened()) {
        LOG(0, 190, L"[listener], port is not opened, id:%d, port:%d",
            port_ ? (int)port_->id_ : 0, port_ ? (int)port_->listenPort_ : 0);
        return false;
    }
    if (setsockopt(slot->socket, SOL_SOCKET, SO_UPDATE_ACCEPT_CONTEXT,
                   (const char*)&listenSocket, sizeof(listenSocket)) == SOCKET_ERROR) {
        LOG0(0xC5, L"setsockopt()", (u32)WSAGetLastError());
        return false;
    }
    // The original applies these to Listener+0x58 (accept slot 0's socket), not to the accepted
    // socket; reproduce the (harmless) calls for binary compatibility.
    int zero = 0;
    if (impl_ != NULL) {
        setsockopt(impl_->slots[0].socket, SOL_SOCKET, 0x1002, (const char*)&zero, sizeof(zero));
        setsockopt(impl_->slots[0].socket, SOL_SOCKET, 0x1001, (const char*)&zero, sizeof(zero));
    }
    sockaddr* local = NULL;
    sockaddr* remote = NULL;
    int localLen = 0x20;
    int remoteLen = 0x20;
    g_getAcceptExSockaddrs(slot->address, 0, 0x20, 0x20, &local, &localLen, &remote, &remoteLen);
    sockaddr_in* peer = reinterpret_cast<sockaddr_in*>(remote);
    u16 peerPort = 0;
    in_addr peerIp;
    peerIp.S_un.S_addr = INADDR_ANY;
    if (peer != NULL) {
        peerIp = peer->sin_addr;
        peerPort = ntohs(peer->sin_port);
    }
    u64 id = 0;
    Session* s = port_->CreateSession(&id, slot->socket, peerIp, peerPort, port_->optionValue());
    if (s == NULL) {
        LOG(0, 224, L"[listener], cannot make new session, port:%d, remote-address:(%s:%d)",
            (int)port_->id_, IpToWideString(peerIp.S_un.S_addr), (int)peerPort);
        return false;
    }
    EnterCriticalSection(&s->lock);
    bool ok = s->StartRecv();
    LeaveCriticalSection(&s->lock);
    if (!ok) {
        s->Release();
        port_->RemoveBySession(s);
        return false;
    }
    s->Release();
    return true;
}

bool Listener::OnAccept(OVERLAPPED* ov) {
    if (ov == NULL || reinterpret_cast<OVERLAPPED_EX*>(ov)->context != kAcceptContext) {
        CRASH(0x77, L"== CRASH == ASSERT CRASH");
        return false;
    }
    OVERLAPPED_EX* slot = reinterpret_cast<OVERLAPPED_EX*>(ov);
    bool ok = CompleteAccept(ov);
    if (!ok && slot->socket != INVALID_SOCKET) closesocket(slot->socket);
    slot->socket = INVALID_SOCKET;
    return PostAccept((int)slot->index);
}

bool Listener::OnFailedEvent(OVERLAPPED* ov) {
    OVERLAPPED_EX* slot = reinterpret_cast<OVERLAPPED_EX*>(ov);
    LOG0(144,
         L"[listener], handle failed event received, port:%d, id:%u, socket:%I64x, internal:%I64x",
         (u32)WSAGetLastError(), port_ ? (int)port_->id_ : 0,
         slot ? slot->index : 0u,
         slot ? (u64)slot->socket : (u64)0,
         (u64)ov);
    if (slot != NULL && slot->socket != INVALID_SOCKET) closesocket(slot->socket);
    if (slot != NULL) slot->socket = INVALID_SOCKET;
    return slot != NULL && PostAccept((int)slot->index);
}

bool Listener::HandleEvent(OVERLAPPED* ov, u32) { return OnAccept(ov); }

} // namespace blood
