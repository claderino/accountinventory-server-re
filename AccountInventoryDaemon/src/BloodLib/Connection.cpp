// BloodLib - socket connection (original: source\Connection.cpp, embedded in Session).
#include "NetworkInternal.h"
#include "Log.h"

SOURCE_FILE("Connection.cpp");

namespace blood {

Connection::Connection()
    : socket(INVALID_SOCKET), port(0), iocp(NULL), pendingSocket_(INVALID_SOCKET), owner_(NULL) {
    address.S_un.S_addr = INADDR_ANY;
}

Connection::~Connection() { Close(); }

void Connection::Close() {
    if (socket != INVALID_SOCKET) {
        closesocket(socket);
        socket = INVALID_SOCKET;
    }
}

bool Connection::Accept(ULONG_PTR completionKey, HANDLE iocpHandle, u32 ip, u16 peerPort) {
    return Accept(pendingSocket_, completionKey, iocpHandle, ip, peerPort);
}

bool Connection::Accept(SOCKET accepted, ULONG_PTR completionKey, HANDLE iocpHandle,
                        u32 ip, u16 peerPort) {
    if (socket != INVALID_SOCKET) {
        CRASH(0x2E, L"== CRASH == ASSERT CRASH");
        return false;
    }
    if (accepted == INVALID_SOCKET || completionKey == 0 || iocpHandle == NULL ||
        iocpHandle == INVALID_HANDLE_VALUE) {
        LOG(0, 0x32,
            L"[connection], Accept failed, invalid parameter SOCKET:0x%I64x, ipaddress:%d, port:%d",
            (u64)accepted, (int)ip, (int)peerPort);
        return false;
    }
    if (CreateIoCompletionPort((HANDLE)accepted, iocpHandle, completionKey, 0) == NULL) {
        LOG0(0x39, L"CreateIoCompletionPort", (u32)GetLastError());
        return false;
    }
    socket = accepted;
    pendingSocket_ = INVALID_SOCKET;
    address.S_un.S_addr = ip;
    port = peerPort;
    iocp = iocpHandle;
    return true;
}

SOCKET Connection::CreateConnectSocket(bool noDelayDisabled) {
    SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
        LOG0(0xFB, L"WSASocket()", (u32)WSAGetLastError());
        return INVALID_SOCKET;
    }
    int zero = 0;
    setsockopt(s, SOL_SOCKET, 0x1002, (const char*)&zero, sizeof(zero));
    setsockopt(s, SOL_SOCKET, 0x1001, (const char*)&zero, sizeof(zero));
    if (!noDelayDisabled) {
        int one = 1;
        if (setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char*)&one, sizeof(one)) == SOCKET_ERROR) {
            LOG0(0x108, L"setsockopt(), setup TCP_NODELAY opt failed", (u32)WSAGetLastError());
            closesocket(s);
            return INVALID_SOCKET;
        }
    }
    sockaddr_in sin;
    memset(&sin, 0, sizeof(sin));
    sin.sin_family = AF_INET;
    sin.sin_addr.S_un.S_addr = INADDR_ANY;
    sin.sin_port = 0;
    if (bind(s, (sockaddr*)&sin, sizeof(sin)) == SOCKET_ERROR) {
        LOG0(0x114, L"bind()", (u32)WSAGetLastError());
        closesocket(s);
        return INVALID_SOCKET;
    }
    return s;
}

bool Connection::ConnectAndAssociate(ConnectionCtx* ctx, bool noDelayDisabled) {
    if (ctx == NULL) return false;
    if (socket != INVALID_SOCKET) {
        CRASH(0x67, L"== CRASH == ASSERT CRASH");
        return false;
    }
    if (ctx->socket == INVALID_SOCKET) {
        ctx->socket = CreateConnectSocket(noDelayDisabled);
        if (ctx->socket == INVALID_SOCKET) return false;
    }
    iocp = NetIocp();
    if (CreateIoCompletionPort((HANDLE)ctx->socket, iocp, (ULONG_PTR)owner_, 0) == NULL) {
        LOG0(0x7A, L"CreateIoCompletionPort", (u32)GetLastError());
        return false;
    }
    ctx->context = kOpConnect;
    DWORD sent = 0;
    BOOL ok = g_connectEx(ctx->socket, (sockaddr*)&ctx->address, sizeof(ctx->address),
                          NULL, 0, &sent, &ctx->overlapped);
    if (ok == FALSE) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            LOG0(0x8A, L"ConnectEx()", (u32)err);
            return false;
        }
    }
    sockaddr_in local;
    int len = sizeof(local);
    getsockname(ctx->socket, (sockaddr*)&local, &len);
    LOG(1, 0x97, L"[session], test, reconnect, this:%p, addr:(%s:%d)",
        owner_, IpToWideString(ctx->address.sin_addr.S_un.S_addr),
        (int)ntohs(ctx->address.sin_port));
    return true;
}

bool Connection::CompleteAsyncConnect(ConnectionCtx* ctx) {
    if (ctx == NULL) return false;
    if (setsockopt(ctx->socket, SOL_SOCKET, SO_UPDATE_CONNECT_CONTEXT, NULL, 0) == SOCKET_ERROR) {
        LOG0(0xA1, L"setsocketopt()", (u32)WSAGetLastError());
        closesocket(ctx->socket);
        ctx->socket = INVALID_SOCKET;
        return false;
    }
    socket = ctx->socket;
    address = ctx->address.sin_addr;
    port = ntohs(ctx->address.sin_port);
    ctx->deadline = 0;
    ctx->connected = 1;
    ctx->socket = INVALID_SOCKET;
    return true;
}

bool Connection::AsyncRecv(OVERLAPPED* ov) {
    if (socket == INVALID_SOCKET || ov == NULL) return false;
    // overlapped is at +0x08 inside RecvContext (offset 0 is the allocator vtable slot)
    RecvContext* ctx = reinterpret_cast<RecvContext*>(reinterpret_cast<char*>(ov) - offsetof(RecvContext, overlapped));
    DWORD received = 0;
    DWORD flags = 0;
    int r = WSARecv(socket, &ctx->buf, 1, &received, &flags, ov, NULL);
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            LOG0(0xBE, L"WSARecv()", (u32)err);
            return false;
        }
    }
    return true;
}

bool Connection::AsyncSend(OVERLAPPED* ov) {
    if (socket == INVALID_SOCKET || ov == NULL) return false;
    // overlapped is at +0x08 inside SendContext
    SendContext* ctx = reinterpret_cast<SendContext*>(reinterpret_cast<char*>(ov) - offsetof(SendContext, overlapped));
    if (ctx->sending == NULL || ctx->sending->count == 0) return false;
    DWORD sent = 0;
    int r = WSASend(socket, ctx->sending->buffers, ctx->sending->count, &sent, 0, ov, NULL);
    if (r == SOCKET_ERROR) {
        int err = WSAGetLastError();
        if (err != WSA_IO_PENDING) {
            LOG0(0xE1, L"WSASend()", (u32)err);
            return false;
        }
    }
    return true;
}

} // namespace blood
