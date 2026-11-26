// BloodLib - daemon lifecycle and process entry (original: Daemon.cpp / Memory.cpp).
#include "Daemon.h"
#include "AlertSender.h"
#include "CrashHandler.h"
#include "DelayedRelease.h"
#include "Log.h"
#include "Memory.h"
#include "Network.h"
#include "PacketBuffer.h"

#include <iphlpapi.h>
#include <winver.h>
#include <shlwapi.h>
#include <time.h>

#pragma comment(lib, "iphlpapi.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "version.lib")

SOURCE_FILE("Daemon.cpp");

namespace blood {

namespace {

Daemon* g_instance = NULL;
int g_runMode = 0;
u64 g_processBaseTick = 0;
CRITICAL_SECTION g_stateLock;
bool g_stateLockReady = false;

void EnsureStateLock() {
    if (!g_stateLockReady) {
        InitializeCriticalSectionAndSpinCount(&g_stateLock, 4000);
        g_stateLockReady = true;
    }
}

void CopyAddressText(u32 networkOrder, wchar_t* output, size_t capacity) {
    in_addr address;
    address.S_un.S_addr = networkOrder;
    if (!InetNtopW(AF_INET, &address, output, static_cast<ULONG>(capacity))) output[0] = L'\0';
}

bool PostTimerToWorkers(Timer* timer) {
    Network* network = Network::Instance();
    if (!timer || !network || network->iocp() == NULL ||
        network->iocp() == INVALID_HANDLE_VALUE)
        return false;
    RefCnt_AddRef(timer, L"src\\Daemon.cpp", 0);
    if (!PostQueuedCompletionStatus(network->iocp(), 0,
                                    reinterpret_cast<ULONG_PTR>(timer), NULL)) {
        RefCnt_Release(timer, L"src\\Daemon.cpp", 0);
        return false;
    }
    return true;
}

class TextConsole : public Console {
public:
    bool Open() override { return true; }
    void Close() override {}
    void SetState(int) override {}
    void SetTitle(const wchar_t* title) override {
        if (title) SetConsoleTitleW(title);
    }
    void Print(int, const wchar_t* format, ...) override {
        if (!format) return;
        wchar_t message[0x900];
        va_list ap;
        va_start(ap, format);
        vswprintf_s(message, 0x900, format, ap);
        va_end(ap);
        wprintf(L"%s\n", message);
    }
};

} // namespace

void StartPerfCounters(Daemon*) {}
void StopPerfCounters() {}
void SetPerfCounterValue(int, u64) {}

static_assert(offsetof(Daemon, abi_) == 0x08, "Daemon ABI");
static_assert(offsetof(DaemonAbi, state) == 0x18, "Daemon ABI relative state");
static_assert(offsetof(Daemon, abi_) + offsetof(DaemonAbi, state) == 0x20, "Daemon ABI");
static_assert(offsetof(Daemon, abi_) + offsetof(DaemonAbi, stopEvent) == 0x28, "Daemon ABI");
static_assert(offsetof(Daemon, abi_) + offsetof(DaemonAbi, baseTick) == 0x38, "Daemon ABI");
static_assert(offsetof(Daemon, abi_) + offsetof(DaemonAbi, startTime) == 0x48, "Daemon ABI");
static_assert(offsetof(Daemon, abi_) + offsetof(DaemonAbi, hostname) == 0x98, "Daemon ABI");
static_assert(offsetof(Daemon, abi_) + offsetof(DaemonAbi, timers) == 0x718, "Daemon ABI");
static_assert(offsetof(Daemon, abi_) + offsetof(DaemonAbi, config) == 0x728, "Daemon ABI");
static_assert(sizeof(Daemon) == 0x7B0, "Daemon ABI");

Daemon* Daemon::Instance() { return g_instance; }

u64 DaemonMillis() {
    if (!g_processBaseTick) g_processBaseTick = GetTickCount64();
    u64 now = GetTickCount64();
    return now >= g_processBaseTick ? now - g_processBaseTick : 0;
}

TimerQueue* DaemonTimers() {
    Daemon* daemon = Daemon::Instance();
    if (daemon && daemon->Timers()) return daemon->Timers();
    // The achievement daemon is achievement::Daemon (a separate class with its own
    // Instance()) and never runs blood::Daemon::start(), so blood::Daemon::Instance()
    // is NULL here. Provide the single process-wide timer queue lazily; application
    // timers (flush / port restart) need it.
    static TimerQueue* fallback = []() {
        TimerQueue* q = new TimerQueue();
        q->Initialize(1000, 21);   // 1 s resolution, 20 s wheel + 1
        return q;
    }();
    return fallback;
}

Daemon::Daemon() {
    memset(&abi_, 0, sizeof(abi_));
    g_instance = this;
    abi_.state = 1;
    abi_.startTime = static_cast<time_t>(_time64(NULL));
    abi_.baseTick = GetTickCount64();
    abi_.startTick = static_cast<DWORD>(abi_.baseTick);
    if (!g_processBaseTick) g_processBaseTick = abi_.baseTick;
    wcscpy_s(abi_.version, 20, L"0.0.210.314");
    readVersion();
    wchar_t executable[MAX_PATH];
    GetModuleFileNameW(NULL, executable, MAX_PATH);
    wchar_t* slash = wcsrchr(executable, L'\\');
    if (slash) slash[1] = L'\0';
    wcscpy_s(abi_.exeDirectory, 0x82, executable);
    wcscpy_s(abi_.configPath, 0x82, executable);
}

Daemon::~Daemon() {
    if (g_instance == this) g_instance = NULL;
    if (abi_.timers) { delete abi_.timers; abi_.timers = NULL; }
    if (abi_.delayedRelease) { delete abi_.delayedRelease; abi_.delayedRelease = NULL; }
    if (abi_.threads) { delete abi_.threads; abi_.threads = NULL; }
    if (abi_.config) { delete abi_.config; abi_.config = NULL; }
    if (abi_.stopEvent) CloseHandle(abi_.stopEvent);
    abi_.stopEvent = NULL;
}

void Daemon::readVersion() {
    wchar_t path[MAX_PATH];
    DWORD length = GetModuleFileNameW(NULL, path, MAX_PATH);
    if (!length || length >= MAX_PATH) return;
    DWORD dummy = 0;
    DWORD size = GetFileVersionInfoSizeW(path, &dummy);
    if (!size) return;
    void* information = malloc(size);
    if (!information) return;
    if (GetFileVersionInfoW(path, 0, size, information)) {
        VS_FIXEDFILEINFO* fixed = NULL;
        UINT fixedLength = 0;
        if (VerQueryValueW(information, L"\\", reinterpret_cast<void**>(&fixed), &fixedLength) &&
            fixed) {
            swprintf_s(abi_.version, 20, L"%d.%d.%d.%d",
                       HIWORD(fixed->dwFileVersionMS), LOWORD(fixed->dwFileVersionMS),
                       HIWORD(fixed->dwFileVersionLS), LOWORD(fixed->dwFileVersionLS));
        }
    }
    free(information);
}

void Daemon::setCwdToExeDir() { SetCurrentDirectoryW(abi_.exeDirectory); }

void Daemon::SetConfigPath(const wchar_t* path) {
    if (!path) { abi_.configPath[0] = L'\0'; return; }
    wcscpy_s(abi_.configPath, 0x82, path);
}

void Daemon::RequestStop() {
    if (abi_.stopEvent) SetEvent(abi_.stopEvent);
}

void Daemon::SetState(int state) {
    EnsureStateLock();
    EnterCriticalSection(&g_stateLock);
    abi_.state = state;
    LeaveCriticalSection(&g_stateLock);
    setStatus(state);
    if (abi_.console) abi_.console->SetState(state);
}

bool Daemon::Reload(const wchar_t* what, int kind) {
    if (abi_.state != 4) {
        LogWrite(0, L"src\\Daemon.cpp", 0x16A,
                 L"[%s], reload failed; not running", GetLogTag());
        return false;
    }
    SetState(9);
    bool ok = reload(what, kind);
    if (ok) SetState(4);
    return ok;
}

bool Daemon::Cooldown() {
    if (abi_.state != 4) {
        LogWrite(0, L"src\\Daemon.cpp", 399,
                 L"[%s], cooldown failed; not running", GetLogTag());
        return false;
    }
    SetState(7);
    bool ok = cooldown();
    if (ok) SetState(8);
    return ok;
}

void Daemon::selectNetworkAddresses() {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    char host[256] = "";
    gethostname(host, static_cast<int>(sizeof(host)));
    MultiByteToWideChar(CP_ACP, 0, host, -1, abi_.hostname, 256);
    abi_.hostname[255] = L'\0';

    ULONG size = 0;
    GetAdaptersAddresses(AF_UNSPEC, 0, NULL, NULL, &size);
    IP_ADAPTER_ADDRESSES* adapters = static_cast<IP_ADAPTER_ADDRESSES*>(
        malloc(size ? size : 0x1000));
    if (adapters && GetAdaptersAddresses(AF_UNSPEC, 0, NULL, adapters, &size) == NO_ERROR) {
        u32 bestPrivate = 0;
        u32 any = 0;
        int bestClass = 0;
        for (IP_ADAPTER_ADDRESSES* adapter = adapters; adapter; adapter = adapter->Next) {
            for (IP_ADAPTER_UNICAST_ADDRESS* unicast = adapter->FirstUnicastAddress;
                 unicast; unicast = unicast->Next) {
                if (unicast->Address.iSockaddrLength < sizeof(sockaddr_in)) continue;
                sockaddr_in* socketAddress = reinterpret_cast<sockaddr_in*>(
                    unicast->Address.lpSockaddr);
                if (socketAddress->sin_family != AF_INET ||
                    socketAddress->sin_addr.S_un.S_addr == 0)
                    continue;
                u32 hostOrder = ntohl(socketAddress->sin_addr.S_un.S_addr);
                if ((hostOrder & 0xFFFF0000) == 0xA9FE0000) continue;
                if ((hostOrder >> 24) == 127 || (hostOrder >> 24) >= 224) continue;
                if (!any) any = socketAddress->sin_addr.S_un.S_addr;
                int classification = 0;
                if ((hostOrder & 0xFF000000) == 0x0A000000) classification = 3;
                else if ((hostOrder & 0xFFF00000) == 0xAC100000) classification = 2;
                else if ((hostOrder & 0xFFFF0000) == 0xC0A80000) classification = 1;
                if (classification > bestClass) {
                    bestClass = classification;
                    bestPrivate = socketAddress->sin_addr.S_un.S_addr;
                }
            }
        }
        abi_.internalNumber.S_un.S_addr = bestPrivate ? bestPrivate : any;
        abi_.publicNumber.S_un.S_addr = bestPrivate ? 0 : any;
    }
    if (adapters) free(adapters);
    CopyAddressText(abi_.publicNumber.S_un.S_addr, abi_.publicAddress, 20);
    CopyAddressText(abi_.internalNumber.S_un.S_addr, abi_.internalAddress, 20);
    if (!abi_.internalAddress[0] && abi_.publicAddress[0]) {
        wcscpy_s(abi_.internalAddress, 20, abi_.publicAddress);
        abi_.internalNumber = abi_.publicNumber;
        LogWrite(0, L"src\\Daemon.cpp", 0x349,
                 L"[%s], internal address is not found; set public address", GetLogTag());
    }
    if (!abi_.publicAddress[0])
        LogWrite(0, L"src\\Daemon.cpp", 0x34D, L"[%s], public address is not found", GetLogTag());
}

Console* CreateConsole(int runMode) {
    if (runMode == 0) return CreateServiceConsole();
    if (runMode == 1) return new TextConsole();
    if (runMode == 2) return CreateWindowConsole();
    CRASH(0x7D, L"== CRASH == [daemon], invalid run mode");
    return NULL;
}

Console* Daemon_CreateConsole() { return CreateConsole(g_runMode); }

bool Daemon::start(Console* console, bool asService) {
    CrashHandler_Init();
    abi_.threads = new ThreadManager();
    if (!abi_.threads->Init(0x80)) return false;
    abi_.threads->ownerTime = abi_.startTick;
    Log_Create();
    abi_.log = TheLog();
    abi_.console = console;
    if (!console || !console->Open()) {
        LogWrite(0, L"src\\Daemon.cpp", 0x143,
                 L"[%s], cannot open console, daemon: %s", GetLogTag(), GetName());
        if (console) console->Close();
        return false;
    }
    return asService ? runAsServiceConsole(this) : run();
}

bool Daemon::run() {
    srand(static_cast<unsigned>(time(NULL)));
    LogBuffer_InitTLS();
    PacketBuffer_InitTLS();
    if (!abi_.stopEvent) abi_.stopEvent = CreateEventW(NULL, FALSE, FALSE, NULL);
    u64 loadStart = GetTickCount64();
    wchar_t title[512];
    swprintf_s(title, 512, L"%s %s", GetLogTag(), GetName());
    if (abi_.console) abi_.console->SetTitle(title);
    SetState(3);
    if (!initLog()) {
        LogWrite(0, L"src\\Daemon.cpp", 0x1E7,
                 L"[%s], log initialize failed, daemon: %s", GetLogTag(), GetName());
        return false;
    }
    selectNetworkAddresses();
    LogWrite(1, L"src\\Daemon.cpp", 0x1E3,
             L"[%s], Starting, daemon: %s, version: %s, desc: %s",
             GetLogTag(), GetDescription(), Version(), GetDescription());
    LogWrite(1, L"src\\Daemon.cpp", 0x1E4,
             L"[%s], Network Configuration, daemon: %s, hostname: %s, public: %s, internal: %s",
             GetLogTag(), GetDescription(), HostName(), PublicAddress(), InternalAddress());
    if (!initialize()) {
        LogWrite(0, L"src\\Daemon.cpp", 0x1EE,
                 L"[%s], initialize error, daemon: %s", GetLogTag(), GetName());
        return false;
    }

    int interval = GetTimerInterval();
    abi_.timers = new TimerQueue();
    u32 buckets = interval > 0 ? static_cast<u32>(GetTimerMaxTime() * (1000 / interval) + 1) : 1;
    if (!abi_.timers->Initialize(interval > 0 ? static_cast<u32>(interval) : 1000, buckets)) {
        LogWrite(0, L"src\\Daemon.cpp", 0x1F9, L"[%s], timer queue initialize failed", GetLogTag());
        return false;
    }
    abi_.delayedRelease = new DelayedRelease();
    abi_.delayedRelease->SetThresholds(1000, 10000);
    u32 workers = abi_.config ? static_cast<u32>(abi_.config->WorkerThread()) : 0;
    Network* network = Network::Create(abi_.threads);
    if (!network || !network->Initialize(workers)) {
        LogWrite(0, L"src\\Daemon.cpp", 0x203,
                 L"[%s], network initialize error, daemon: %s", GetLogTag(), GetName());
        return false;
    }
    if (!initNetwork()) {
        LogWrite(0, L"src\\Daemon.cpp", 0x203,
                 L"[%s], network initialize error, daemon: %s", GetLogTag(), GetName());
        return false;
    }
    if (!onStart()) {
        LogWrite(0, L"src\\Daemon.cpp", 0x20A,
                 L"[%s], onStart fail, daemon: %s", GetLogTag(), GetName());
        return false;
    }
    SetState(4);
    LogWrite(1, L"src\\Daemon.cpp", 0x212,
             L"[%s], start, daemon: %s, version: %s, loading time: %I64d",
             GetLogTag(), GetDescription(), Version(),
             static_cast<i64>(GetTickCount64() - loadStart));
    StartPipeConsole(this);
    StartPerfCounters(this);
    timerLoop();
    LogWrite(1, L"src\\Daemon.cpp", 0x21A,
             L"[%s], stop, daemon: %s, version: %s", GetLogTag(), GetName(), Version());
    SetState(2);
    StopPerfCounters();
    StopPipeConsole();
    shutdown();
    if (Network::Instance()) Network::Instance()->Finalize();
    if (abi_.delayedRelease) { abi_.delayedRelease->FlushAll(); delete abi_.delayedRelease; abi_.delayedRelease = NULL; }
    if (abi_.timers) { delete abi_.timers; abi_.timers = NULL; }
    if (TheLog()) TheLog()->FlushAndStop();
    SetState(1);
    abi_.application[0] = 1;
    return true;
}

void Daemon::timerLoop() {
    int interval = GetTimerInterval();
    DWORD wait = interval > 0 ? static_cast<DWORD>(interval) : 600000;
    u64 lastHousekeeping = DaemonMillis();
    while (WaitForSingleObject(abi_.stopEvent, wait) != WAIT_OBJECT_0) {
        InterlockedIncrement(&abi_.loopCounter);
        if (abi_.threads) abi_.threads->RefreshTimes();
        if (interval > 0 && abi_.timers) {
            Timer* timer = abi_.timers->Update();
            while (timer) {
                Timer* next = timer->next;
                timer->next = timer->previous = NULL;
                if (!PostTimerToWorkers(timer))
                    RefCnt_Release(timer, L"src\\Daemon.cpp", 0x3E0);
                timer = next;
            }
        }
        if (abi_.delayedRelease) abi_.delayedRelease->Flush(DaemonMillis());
        if (TheLog()) TheLog()->Flush();
        if (DaemonMillis() - lastHousekeeping >= 600000) lastHousekeeping = DaemonMillis();
    }
}

bool Daemon_Start(Console* console, bool asService) {
    Daemon* daemon = Daemon::Instance();
    return daemon && daemon->start(console, asService);
}

void Daemon_FormatPath(wchar_t* output, const wchar_t* format, ...) {
    va_list ap;
    va_start(ap, format);
    vswprintf_s(output, 0x208, format, ap);
    va_end(ap);
}

static void PrintUsage(const wchar_t* executable) {
    wprintf(L"Usage: %s [<option>]\n", executable);
    wprintf(L"      %s\n", L"--help");
    wprintf(L"        : this screen.\n");
    wprintf(L"      %s\n", L"--version");
    wprintf(L"        : version.\n");
    wprintf(L"      %s\n", L"--window");
    wprintf(L"        : window console mode. DEFAULT\n");
    wprintf(L"      %s\n", L"--window-exitcmd");
    wprintf(L"        : window console mode and exit cmd\n");
    wprintf(L"      %s\n", L"--text");
    wprintf(L"        : text console mode.\n");
    wprintf(L"      %s [<postfix>] [<confdir>]\n", L"--install");
    wprintf(L"        : install service [with service name <postfix>] [with config dir <confdir>].\n");
    wprintf(L"      %s [<postfix>]\n", L"--uninstall");
    wprintf(L"        : uninstall service [with service name <postfix>].\n");
}

int DaemonMain(int argc, wchar_t** argv, wchar_t**, void*) {
    Memory_Init();
    AlertSender_Startup();
    Daemon* daemon = Daemon::Instance();
    if (!daemon) {
        LogWrite(0, L"src\\Daemon.cpp", 0xA5, L"== CRASH == ASSERT CRASH");
        Assert_FailW(L"src\\Daemon.cpp", 0xA5, L"== CRASH == ASSERT CRASH");
        Assert_FailA(L"src\\Daemon.cpp", 0xA5, L"");
        return 1;
    }
    for (int i = 1; i < argc; ++i) {
        if (_wcsicmp(argv[i], L"--version") == 0) {
            wprintf(L"%s\n", daemon->GetDescription());
            wprintf(L"%s %s %s / PlatformBinary server %s\n", daemon->GetName(), L"service",
                    daemon->Version(), L"0.0.210.359");
            daemon->PrintVersion();
            exit(0);
        }
    }
    wchar_t executable[MAX_PATH];
    GetModuleFileNameW(NULL, executable, MAX_PATH);
    PathRemoveFileSpecW(executable);
    PathAddBackslashW(executable);
    daemon->SetConfigPath(executable);
    g_runMode = GetConsoleWindow() ? 2 : 0;
    if (GetConsoleWindow()) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"--help") == 0) {
                PrintUsage(argv[0] ? argv[0] : L"");
                return 1;
            } else if (_wcsicmp(argv[i], L"--text") == 0) {
                g_runMode = 1;
            } else if (_wcsicmp(argv[i], L"--window") == 0) {
                g_runMode = 2;
            } else if (_wcsicmp(argv[i], L"--install") == 0) {
                InstallService(daemon, i + 1 < argc ? argv[i + 1] : NULL,
                               i + 2 < argc ? argv[i + 2] : NULL);
                return 0;
            } else if (_wcsicmp(argv[i], L"--uninstall") == 0) {
                UninstallService(daemon, i + 1 < argc ? argv[i + 1] : NULL);
                return 0;
            } else if (_wcsnicmp(argv[i], L"--conf", 6) == 0) {
                daemon->SetConfigPath(argv[i] + 7);
            } else {
                PrintUsage(argv[0] ? argv[0] : L"");
                return 1;
            }
        }
    }
    Console* console = Daemon_CreateConsole();
    if (!console) return 1;
    bool ok = Daemon_Start(console, g_runMode == 0);
    AlertSender_Shutdown();
    return ok ? 0 : 1;
}

} // namespace blood
