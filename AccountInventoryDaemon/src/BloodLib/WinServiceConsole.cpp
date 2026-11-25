// BloodLib - Windows service console (original: source\WinServiceConsole.cpp).
// Service name L"ACHIEVEMENTD"; description "Blade&Soul OuterWorld AchievementDaemon".
#include "BloodLib.h"
#include "Thread.h"
#include "Daemon.h"
#include <winsvc.h>
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

SOURCE_FILE("WinServiceConsole.cpp");

namespace blood {
namespace {

SERVICE_STATUS_HANDLE g_status = NULL;
SERVICE_STATUS g_statusData;
Console* g_serviceConsole = NULL;
DWORD g_lastError = 0;

void ReportStatus(DWORD state) {
    if (g_status == NULL) return;
    g_statusData.dwCurrentState = state;
    g_statusData.dwCheckPoint = 0;
    g_statusData.dwWaitHint = (state == SERVICE_START_PENDING) ? 5000 : 0;
    SetServiceStatus(g_status, &g_statusData);
}

void ReportStopped() {
    if (g_status == NULL) return;
    g_statusData.dwCurrentState = SERVICE_STOPPED;
    g_statusData.dwWin32ExitCode = 1;
    g_statusData.dwCheckPoint = 0;
    g_statusData.dwWaitHint = 0;
    SetServiceStatus(g_status, &g_statusData);
}

wchar_t* FormatError(wchar_t* out) {
    DWORD err = g_lastError;
    wchar_t system[512] = { 0 };
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                   NULL, err, 0, system, 512, NULL);
    for (wchar_t* p = system; *p; ++p) if (*p == L'\r' || *p == L'\n') *p = L' ';
    swprintf_s(out, 1024, L"%s (0x%x)", system, (unsigned)err);
    return out;
}

void WriteEventLog(bool error, const wchar_t* message) {
    g_lastError = GetLastError();
    HANDLE h = RegisterEventSourceW(NULL, L"ACHIEVEMENTD");
    if (h == NULL) return;
    wchar_t text[1024];
    const wchar_t* daemonName = L"AchievementDaemon";
    swprintf_s(text, 1024, L"%s %s: %d", daemonName, message ? message : L"", (int)g_lastError);
    const wchar_t* strings[2] = { daemonName, text };
    ReportEventW(h, error ? EVENTLOG_ERROR_TYPE : EVENTLOG_INFORMATION_TYPE,
                 0, 0, NULL, 2, 0, strings, NULL);
    CloseEventLog(h);
}

void WINAPI ControlCallback(DWORD control) {
    Daemon* d = Daemon::Instance();
    // The original forward the control to Console vtable slot 12; the reconstruction uses the
    // daemon's stop request, which is what the application's handler ultimately does.
    if (d != NULL) {
        if (control == SERVICE_CONTROL_STOP || control == SERVICE_CONTROL_SHUTDOWN)
            d->RequestStop();
        else
            d->setStatus((int)control);
    }
}

class WinServiceConsole : public Console {
public:
    bool Open() override {
        if (g_serviceConsole != NULL) {
            CRASH(0x20, L"== CRASH == ASSERT CRASH");
            return false;
        }
        g_serviceConsole = this;
        return true;
    }
    void Close() override { if (g_serviceConsole == this) g_serviceConsole = NULL; }

    void SetState(int state) override {
        // Only 4 (RUNNING) and 2 (STOP_PENDING) are handled in this build.
        if (state == 4) ReportStatus(SERVICE_RUNNING);
        else if (state == 2) ReportStatus(SERVICE_STOP_PENDING);
    }
    void SetTitle(const wchar_t*) override {}
    void Print(int level, const wchar_t* fmt, ...) override {
        wchar_t text[256];
        va_list ap;
        va_start(ap, fmt);
        if (vswprintf_s(text, 255, fmt ? fmt : L"", ap) < 0) text[254] = L'\0';
        va_end(ap);
        WriteEventLog(level == 0, text);
    }
    bool IsWindowConsole() const override { return false; }
};

void WINAPI ServiceMainThunk(DWORD argc, LPWSTR* argv) {
    (void)argc;
    (void)argv;
    ServiceMainEntry(Daemon::Instance());
}

} // anonymous namespace

Console* CreateServiceConsole() {
    WinServiceConsole* c = new WinServiceConsole;
    g_serviceConsole = c;
    return c;
}

bool runAsServiceConsole(Daemon* daemon) {
    SERVICE_TABLE_ENTRYW table[2];
    table[0].lpServiceName = (LPWSTR)daemon->GetServiceName();
    table[0].lpServiceProc = ServiceMainThunk;
    table[1].lpServiceName = NULL;
    table[1].lpServiceProc = NULL;
    if (!StartServiceCtrlDispatcherW(table)) return false;
    return true;
}

void ServiceMainEntry(Daemon* daemon) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    for (int i = 1; argv != NULL && i < argc; ++i) {
        if (wcsncmp(argv[i], L"--conf", 6) == 0) daemon->SetConfigPath(argv[i] + 7);
    }
    if (argv) LocalFree(argv);

    memset(&g_statusData, 0, sizeof(g_statusData));
    g_statusData.dwServiceType = SERVICE_WIN32_OWN_PROCESS;
    g_statusData.dwCurrentState = SERVICE_START_PENDING;
    g_statusData.dwControlsAccepted = SERVICE_ACCEPT_STOP;
    g_statusData.dwCheckPoint = 1;
    g_statusData.dwWaitHint = 5000;
    g_status = RegisterServiceCtrlHandlerW(L"ACHIEVEMENTD", ControlCallback);
    if (g_status == NULL) { WriteEventLog(true, L"RegisterServiceCtrlHandler failed"); return; }
    if (!SetServiceStatus(g_status, &g_statusData)) {
        WriteEventLog(true, L"SetServiceStatus failed");
        return;
    }
    if (g_serviceConsole == NULL) g_serviceConsole = new WinServiceConsole;
    daemon->SetState(3);
    (void)daemon->run();
    // SAFETY: the original never reports SERVICE_STOPPED on a clean run; we do.
    ReportStopped();
}

bool InstallService(Daemon* daemon, const wchar_t* postfix, const wchar_t* confDir) {
    wchar_t name[256];
    swprintf_s(name, 256, L"ACHIEVEMENTD%s", postfix ? postfix : L"");
    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(NULL, exe, MAX_PATH);
    wchar_t binaryPath[1024];
    swprintf_s(binaryPath, 1024, L"\"%s\" \"--conf=%s\"", exe, confDir ? confDir : L"");
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, 0xF003F);
    if (scm == NULL) return false;
    SC_HANDLE svc = CreateServiceW(scm, name, name, 0xF01FF, SERVICE_WIN32_OWN_PROCESS,
                                   SERVICE_DEMAND_START, SERVICE_ERROR_NORMAL, binaryPath,
                                   NULL, NULL, L"", NULL, NULL);
    if (svc == NULL) { CloseServiceHandle(scm); return false; }
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return true;
}

bool UninstallService(Daemon* daemon, const wchar_t* postfix) {
    (void)daemon;
    wchar_t name[256];
    swprintf_s(name, 256, L"ACHIEVEMENTD%s", postfix ? postfix : L"");
    SC_HANDLE scm = OpenSCManagerW(NULL, NULL, 0xF003F);
    if (scm == NULL) return false;
    SC_HANDLE svc = OpenServiceW(scm, name, 0xF01FF);
    if (svc == NULL) { CloseServiceHandle(scm); return false; }
    SERVICE_STATUS status;
    memset(&status, 0, sizeof(status));
    ControlService(svc, SERVICE_CONTROL_STOP, &status);
    // The original polls with no iteration cap; cap at 60 iterations (SAFETY).
    for (int i = 0; i < 60; ++i) {
        Sleep(1000);
        if (!QueryServiceStatus(svc, &status) || status.dwCurrentState != SERVICE_STOP_PENDING)
            break;
    }
    bool deleted = DeleteService(svc) != 0;
    CloseServiceHandle(svc);
    CloseServiceHandle(scm);
    return deleted;
}

} // namespace blood
