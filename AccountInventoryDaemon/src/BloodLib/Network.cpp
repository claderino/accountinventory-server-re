// BloodLib - IOCP network core (original: source\Network.cpp).
//
// CROSS-FILE EXTERNS NEEDED BY THIS MODULE (declared here / in the other agents' headers):
//   blood::LogWrite(level, file, line, fmt, ...)         - this build has NO tag/function args
//   blood::LogWrite0(file, line, errorCode, apiName)
//   blood::Assert_FailW / Assert_FailA(file, line, fmt, ...)
//   blood::DaemonMillis()                               - Daemon.cpp
//   blood::TheDelayedRelease().FlushAll()               - DelayedRelease.cpp
//   blood::ThreadManager / IoWorkerThread               - Thread.cpp / IoWorkerThread.cpp
//   blood::PacketBufferHandler                          - PacketBufferHandler.cpp
//   AchievementDaemon::FailedEventObjectHandler (sub_02BB0) - application; install with
//       Network::Instance()->SetFailedEventHandler(...) from AchievementDaemon::InitializeNetwork.
// No packet pump and no event-class 0x80 jobs exist in this image.
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include "NetworkInternal.h"
#include "Log.h"

SOURCE_FILE("Network.cpp");

namespace blood {

// ---------------------------------------------------------------------------------------------
// Globals of the original Network module (RVA B79D0/B79D8/B79E0/B7795/B7AD0/B7B08,
// B79E8..B7A00 for the extension pointers).
// ---------------------------------------------------------------------------------------------
BloodAcceptExFn             g_acceptEx = NULL;
BloodGetAcceptExSockaddrsFn g_getAcceptExSockaddrs = NULL;
BloodConnectExFn            g_connectEx = NULL;
BloodDisconnectExFn         g_disconnectEx = NULL;

static Network* g_network = NULL;
static ThreadManager g_workerMgr;
static bool g_netRunning = false;
static u32 g_cryptCounter = 0;
static long g_initCount = 0;
static Port* g_ports[24] = { 0 };

bool IsNetRunning() { return g_netRunning; }
HANDLE NetIocp() { return g_network ? g_network->iocp() : INVALID_HANDLE_VALUE; }
HANDLE NetIoCompletionPort() { return NetIocp(); }

void PostOperationAddRef(EventObject* key) {
    if (key != NULL) RefCnt_AddRef(key, kSourceFile, 0);
}
void CompleteOperationRelease(EventObject* key) {
    if (key == NULL) return;
    if (key->EventClass() == kSessionEventClass) {
        static_cast<Session*>(key)->ReleasePendingIo();   // drops the operation reference
    } else {
        RefCnt_Release(key, kSourceFile, 0);              // Listener / job operation reference
    }
}

Network* Network::Instance() { return g_network; }

Network::Network()
    : workerCount_(0), workers_(NULL), iocp_(INVALID_HANDLE_VALUE), terminated_(false),
      daemonThreads_(NULL), failedHandler_(NULL), auxiliaryThreadManager_(NULL) {
    memset(ports_, 0, sizeof(ports_));
    if (g_network != NULL) {
        CRASH(0x34, L"== CRASH == ASSERT CRASH");
    }
    // The original allocates a 0x18-byte termination key at global B79E0 with refcount 1.
    // It is never posted by Finalize and never freed; we keep no object here.
}

Network::~Network() {
    if (g_network == this) g_network = NULL;
}

Network* Network::Create(ThreadManager* daemonThreads) {
    if (g_network != NULL) {
        CRASH(0x346, L"== CRASH == ASSERT CRASH");
    }
    Network* net = new Network;
    g_network = net;
    net->daemonThreads_ = daemonThreads;
    WSADATA data;
    if (WSAStartup(0x0202, &data) != 0) {
        LOG0(0x15A, L"WSAStartup", (u32)WSAGetLastError());
        return NULL;   // g_network stays set, as in the original
    }
    return net;
}

// GUIDs of the four Winsock extension functions (sub_24F20).
static const GUID kGuidAcceptEx = {
    0xb5367df1, 0xcbac, 0x11cf, { 0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92 }
};
static const GUID kGuidGetAcceptExSockaddrs = {
    0xb5367df2, 0xcbac, 0x11cf, { 0x95, 0xca, 0x00, 0x80, 0x5f, 0x48, 0xa1, 0x92 }
};
static const GUID kGuidConnectEx = {
    0x25a207b9, 0xddf3, 0x4660, { 0x8e, 0xe9, 0x76, 0xe5, 0x8c, 0x74, 0x06, 0x3e }
};
static const GUID kGuidDisconnectEx = {
    0x7fda2e11, 0x8630, 0x436f, { 0xa0, 0x31, 0xf5, 0x36, 0xa6, 0xee, 0xc1, 0x57 }
};

static bool ResolveOne(SOCKET s, const GUID& guid, void** out, int line) {
    if (*out != NULL) return true;
    DWORD bytes = 0;
    int r = WSAIoctl(s, SIO_GET_EXTENSION_FUNCTION_POINTER,
                     const_cast<GUID*>(&guid), sizeof(GUID), out,
                     sizeof(void*), &bytes, NULL, NULL);
    if (r == SOCKET_ERROR) {
        LOG0(line, L"WSAIoctl()", (u32)WSAGetLastError());
        return false;
    }
    return true;
}

bool Network::ResolveWinsockExtensions() {
    SOCKET s = WSASocketW(AF_INET, SOCK_STREAM, IPPROTO_TCP, NULL, 0, WSA_FLAG_OVERLAPPED);
    if (s == INVALID_SOCKET) {
        LOG0(0x19D, L"WSASocket()", (u32)WSAGetLastError());
        return false;
    }
    bool ok = ResolveOne(s, kGuidAcceptEx, (void**)&g_acceptEx, 0x1AA) &&
              ResolveOne(s, kGuidGetAcceptExSockaddrs, (void**)&g_getAcceptExSockaddrs, 0x1B7) &&
              ResolveOne(s, kGuidConnectEx, (void**)&g_connectEx, 0x1C4) &&
              ResolveOne(s, kGuidDisconnectEx, (void**)&g_disconnectEx, 0x1CF);
    closesocket(s);
    return ok;
}

bool Network::Initialize(u32 workerCount) {
    if (g_network == NULL) {
        CRASH(0x54, L"== CRASH == ASSERT CRASH");
        return false;
    }
    // Unlike ranking, this build has no "already initialized" early return.
    if (!ResolveWinsockExtensions()) return false;
    g_netRunning = true;
    iocp_ = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
    if (iocp_ == NULL || iocp_ == INVALID_HANDLE_VALUE) {
        LOG0(0x59, L"CreateIoCompletionPort", (u32)GetLastError());
        g_netRunning = false;
        return false;
    }
    if (!InitWorkers(workerCount)) {
        LOG(0, 0x60, L"[network], Worker Thread Initialize Error");
        return false;
    }
    // First-call crypto guard: the original logs value 4 on the second Initialize.
    if (g_cryptCounter <= 0) {
        g_cryptCounter = 4000;
        return true;
    }
    LOG(0, 0x67, L"[network], Crypt Module initialized failed(%d)", 4);
    return false;
}

bool Network::InitWorkers(u32 count) {
    if (count == 0) {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        count = si.dwNumberOfProcessors * 2;
    }
    ThreadManager* mgr = daemonThreads_ ? daemonThreads_ : &g_workerMgr;
    // blood::Daemon::start() pre-initialises its ThreadManager to 128 slots; do not re-Init it.
    if (mgr->capacity == 0 && !mgr->Init(count)) return false;
    if (mgr->capacity < count) return false;
    workerCount_ = count;
    workers_ = new IoWorkerThread[count];
    for (u32 i = 0; i < count; ++i) {
        IoWorkerThread& w = workers_[i];
        w.Init(i + 1, failedHandler_);
        if (!mgr->Start(&w)) return false;
    }
    Sleep(1000);
    return true;
}

void Network::Finalize() {
    terminated_ = true;
    g_netRunning = false;

    // SAFETY: drain all deferred callbacks and timer jobs before ports can be destroyed.
    TheDelayedRelease().FlushAll();

    if (iocp_ != NULL && iocp_ != INVALID_HANDLE_VALUE) {
        ThreadManager* mgr = daemonThreads_ ? daemonThreads_ : &g_workerMgr;
        // SAFETY: cancel and await outstanding I/O while the workers are still alive so their
        // completions can release the per-operation references. The original stops workers first.
        for (u32 i = 0; i < 24; ++i) {
            Port* p = g_ports[i];
            if (p != NULL) { p->Stop(); p->BeginTeardownSessions(); }
        }
        for (u32 i = 0; i < workerCount_; ++i) workers_[i].Stop();
        mgr->StopAll(2000);
        mgr->Finalize();

        // Do not post the termination key: the original never does.
        for (u32 i = 0; i < 24; ++i) {
            Port* p = g_ports[i];
            if (p == NULL) continue;
            p->Clear();
            p->Clear();                   // the original calls Clear twice
            g_ports[i] = NULL;
        }
        CloseHandle(iocp_);
        iocp_ = INVALID_HANDLE_VALUE;
    }
    delete[] workers_;
    workers_ = NULL;
    InterlockedDecrement(&g_initCount);
}

bool Network::OpenPort(u32 id, u16 listenPort, SessionHandler* handler,
                       PacketBufferHandler* prototype) {
    if (id == 0 || g_ports[id - 1] != NULL) {   // original checks only id != 0 and slot empty
        CRASH(0x2E8, L"== CRASH == ASSERT CRASH");
        return false;
    }
    if (listenPort == 0 || handler == NULL || prototype == NULL) {
        CRASH(0x2EF, L"== CRASH == ASSERT CRASH");
        return false;
    }
    Port* p = new Port;
    if (p == NULL) {
        CRASH(0x346, L"== CRASH == ASSERT CRASH");
        return false;
    }
    // This build always requests 10000 sessions and preallocates all of them.
    if (!p->Initialize(id, listenPort, kOpenPortCapacity, iocp_, handler, prototype)) {
        delete p;
        return false;
    }
    if (p->listener() == NULL) {
        LOG(0, 0x14A, L"[port], Initialize first!");
        p->Clear();
        delete p;
        return false;
    }
    p->Start();
    p->activeFlag = 1;
    p->initialized = true;
    g_ports[id - 1] = p;
    return true;
}

bool Network::StartPort(u32 id) {
    if (id == 0 || id > 24 || g_ports[id - 1] == NULL) {
        CRASH(0x2D5, L"== CRASH == ASSERT CRASH");
        return false;
    }
    Port* p = g_ports[id - 1];
    if (p->listener() == NULL) {
        LOG(0, 0x14A, L"[port], Initialize first!");
        return false;
    }
    p->Start();
    p->initialized = true;
    return true;
}

bool Network::StopPort(u32 id) {
    if (id == 0 || id > 24 || g_ports[id - 1] == NULL) {
        CRASH(0x2DB, L"== CRASH == ASSERT CRASH");
        return false;
    }
    // The original ignores its argument and always clears port 1; we use it correctly.
    g_ports[id - 1]->Stop();
    return true;
}

Port* Network::FindPort(u32 id) {
    if (id == 0 || id > 24) return NULL;
    return g_ports[id - 1];
}

bool WideToInetAddr(const wchar_t* ip, in_addr* out) {
    char buf[32] = { 0 };
    if (ip == NULL) return false;
    WideCharToMultiByte(CP_ACP, 0, ip, -1, buf, sizeof(buf) - 1, NULL, NULL);
    in_addr a;
    a.S_un.S_addr = inet_addr(buf);
    if (out) *out = a;
    return a.S_un.S_addr != INADDR_NONE;
}

const wchar_t* IpToWideString(u32 ipv4NetworkOrder) {
    static __declspec(thread) char ansi[32];
    static __declspec(thread) wchar_t wide[31];
    in_addr a;
    a.S_un.S_addr = ipv4NetworkOrder;
    ansi[0] = 0;
    wide[0] = 0;
    const char* p = inet_ntoa(a);
    if (p != NULL) strcpy_s(ansi, sizeof(ansi), p);
    MultiByteToWideChar(CP_ACP, 0, ansi, -1, wide, 31);
    return wide;
}

} // namespace blood
