// Differential test: original bsConsole_sv64.dll vs. the reconstruction.
//
//   difftest.exe <original.dll> <reconstruction.dll>
//   difftest.exe /visual <one.dll> <orig|new>     (internal child mode)
//
// Both DLLs are loaded with LoadLibraryW and driven through the SAME blood::IConsoleWindow
// interface.  The original vtable is also checked slot-by-slot against report section 3.1.
// The visual part runs each DLL in its own child process: Win32 window classes are registered
// per process/module, and both DLLs register identical class names, so a single process cannot
// host two independent copies of the console window safely.
// BloodLib.h includes winsock2.h before windows.h; keep this include first to avoid the
// winsock.h/winsock2.h collision.
#include "..\..\RankingDaemon\src\BloodLib\BsConsole.h"

#include <windows.h>
#include <stdio.h>
#include <wchar.h>
#include <vector>

static long g_fail = 0;
static long g_total = 0;
static volatile LONG g_exitCount = 0;

static void Report(const char* what, bool ok) {
    ++g_total;
    if (!ok) ++g_fail;
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
}

// ---------------------------------------------------------------------------------------------
struct Dll {
    HMODULE module;
    const wchar_t* (*GetVersion)();
    blood::IConsoleWindow* (*CreateWindowObj)(HMODULE);
    void (*DestroyWindowObj)(blood::IConsoleWindow*);
};

static bool LoadDll(const wchar_t* path, Dll* d, wchar_t* err, DWORD errLen) {
    memset(d, 0, sizeof(*d));
    d->module = LoadLibraryW(path);
    if (!d->module) {
        swprintf_s(err, errLen, L"LoadLibraryW failed (error %lu)", GetLastError());
        return false;
    }
    d->GetVersion = reinterpret_cast<const wchar_t* (*)()>(GetProcAddress(d->module, "GetInterfaceVersion"));
    d->CreateWindowObj = reinterpret_cast<blood::IConsoleWindow* (*)(HMODULE)>(
        GetProcAddress(d->module, "CreateConsoleWindow"));
    d->DestroyWindowObj = reinterpret_cast<void (*)(blood::IConsoleWindow*)>(
        GetProcAddress(d->module, "DestroyConsoleWindow"));
    if (!d->GetVersion || !d->CreateWindowObj || !d->DestroyWindowObj) {
        swprintf_s(err, errLen, L"GetProcAddress failed (error %lu)", GetLastError());
        FreeLibrary(d->module);
        d->module = NULL;
        return false;
    }
    return true;
}

static void ExitCallback() { InterlockedIncrement(&g_exitCount); }

// ---------------------------------------------------------------------------------------------
// Mirror of the daemon's LogBuffer (report section 3.5) with a working embedded interface so the
// original DLL's level/enabled queries through its vtable are also exercised.
// ---------------------------------------------------------------------------------------------
#pragma pack(push, 8)
struct FakeIface {
    ULONG_PTR* vtbl;
    int level;
    int enabled;
};
struct TestRecord {
    void* vtbl;                       // +0x00
    int size;                         // +0x08
    int refs;                         // +0x0C
    unsigned char pad10[0x18 - 0x10];
    FakeIface* iface;                 // +0x18
    unsigned char pad20[0x78 - 0x20];
    int level;                        // +0x78
    unsigned char pad7C[0x8C - 0x7C];
    bool enabled;                     // +0x8C
    unsigned char pad8D;
    wchar_t text[0x7B8];              // +0x8E
};
#pragma pack(pop)

static ULONG_PTR F_Enabled(void* p) { return ((FakeIface*)p)->enabled ? 1 : 0; }
static ULONG_PTR F_Release(void*) { return 1; }
static int F_Level(void* p) { return ((FakeIface*)p)->level; }
static ULONG_PTR F_Noop(void*) { return 1; }

static ULONG_PTR g_fakeVtbl[8] = {
    (ULONG_PTR)F_Enabled, (ULONG_PTR)F_Release, (ULONG_PTR)F_Level, (ULONG_PTR)F_Noop,
    (ULONG_PTR)F_Noop, (ULONG_PTR)F_Noop, (ULONG_PTR)F_Noop, (ULONG_PTR)F_Noop
};
// The record's own vtable at +0x00 (the original's destructor thunk) must also be valid: the
// queue may call through it when it releases a record.
static ULONG_PTR g_recordVtbl[8] = {
    (ULONG_PTR)F_Noop, (ULONG_PTR)F_Noop, (ULONG_PTR)F_Noop, (ULONG_PTR)F_Noop,
    (ULONG_PTR)F_Noop, (ULONG_PTR)F_Noop, (ULONG_PTR)F_Noop, (ULONG_PTR)F_Noop
};

static std::vector<TestRecord*> g_records;

static TestRecord* MakeRecord(int level, bool enabled, const wchar_t* text) {
    TestRecord* r = (TestRecord*)calloc(1, sizeof(TestRecord));
    FakeIface* iface = (FakeIface*)calloc(1, sizeof(FakeIface));
    iface->vtbl = g_fakeVtbl;
    iface->level = level;
    iface->enabled = enabled ? 1 : 0;
    r->vtbl = g_recordVtbl;
    r->size = 0x10A;
    r->refs = 1;
    r->iface = iface;
    r->level = level;
    r->enabled = enabled;
    wcsncpy_s(r->text, 0x7B8, text, 0x7B7);
    g_records.push_back(r);
    return r;
}

static void FreeRecords() {
    for (size_t i = 0; i < g_records.size(); ++i) {
        free(g_records[i]->iface);
        free(g_records[i]);
    }
    g_records.clear();
}

struct EnqCtx {
    blood::IConsoleWindow* obj;
    int count;
    bool visual;
};

static DWORD WINAPI EnqueueThread(void* arg) {
    EnqCtx* ctx = (EnqCtx*)arg;
    wchar_t longText[0x7B8];
    for (int i = 0; i < 0x7B7; ++i) longText[i] = (wchar_t)(L'a' + (i % 26));
    longText[0x7B7] = 0;
    for (int i = 0; i < ctx->count; ++i) {
        int level = ctx->visual ? (1 + (i % 3)) : (i % 4);
        bool enabled = (i % 7) != 0;
        const wchar_t* text = (i % 11 == 0) ? L"" : ((i % 5 == 0) ? longText : L"differential test log line");
        TestRecord* r = MakeRecord(level, enabled, text);
        ctx->obj->EnqueueLogRecord((blood::LogBuffer*)r);
        if ((i % 25) == 0) Sleep(1);
    }
    return 0;
}

// ---------------------------------------------------------------------------------------------
// Slot-by-slot verification of the original vtable against the report's table 3.1.
// ---------------------------------------------------------------------------------------------
static const ULONG_PTR kExpectedRva[17] = {
    0x10F0, 0x11B0, 0x1240, 0x1290, 0x12A0, 0x12B0, 0x1310, 0x1320, 0x1330,
    0x1340, 0x1380, 0x13C0, 0x13F0, 0x1450, 0x14B0, 0x14C0, 0x10C0
};

static bool CheckOriginalSlots(blood::IConsoleWindow* obj, HMODULE module) {
    ULONG_PTR* vt = *(ULONG_PTR**)obj;
    ULONG_PTR base = (ULONG_PTR)module;
    char msg[128];
    bool all = true;
    for (int i = 0; i < 17; ++i) {
        ULONG_PTR got = vt[i] - base;
        bool ok = (got == kExpectedRva[i]);
        if (!ok) {
            sprintf_s(msg, sizeof(msg), "original vtable slot %d RVA=%p (expected %p)",
                      i, (void*)got, (void*)kExpectedRva[i]);
            printf("FAIL: %s\n", msg);
            ++g_fail;
        }
        ++g_total;
        all = all && ok;
    }
    // The next qword is the version string, not a vtable entry.
    return all;
}

static void ExerciseNonVisual(Dll* d, blood::IConsoleWindow* obj, const wchar_t* tag) {
    wchar_t title[256];
    swprintf_s(title, 256, L"bsConsole_diff_%s_%lu", tag, GetCurrentProcessId());
    bool reloadFlag = true, shutdownFlag = true;

    bool created = obj->Create(NULL, title, L"RankingDaemon", NULL, ExitCallback,
                               &reloadFlag, &shutdownFlag, NULL, NULL);
    Report("Create returns true", created);
    Report("GetHwnd is NULL before ShowRun", obj->GetHwnd() == NULL);
    obj->SetTitle(L"non-visual phase");
    obj->SetStateText(L"STOPPED");
    obj->SetBusyText(L"busy", 20);
    obj->ShowMessage(L"message", 20);
    obj->EnableConnect(true);
    obj->EnableConnect(false);
    obj->EnableReload(true);
    obj->EnableReload(false);
    obj->EnableShutdown(true);
    obj->EnableShutdown(false);
    obj->SetConnected(true);
    obj->Connect();
    obj->Disconnect();
    obj->SetCounters(1, 2, 3, 4, 5, 6);
    Report("non-visual interface calls survived", true);

    EnqCtx ctx = { obj, 40, false };
    HANDLE th = CreateThread(NULL, 0, EnqueueThread, &ctx, 0, NULL);
    if (th) { WaitForSingleObject(th, 10000); CloseHandle(th); Report("multi-thread EnqueueLogRecord survived", true); }
    else Report("multi-thread EnqueueLogRecord survived", false);

    // Do not call ResetAndDestroy here: the original's slot 16 performs global DLL cleanup, which
    // would invalidate the other live window object still used by the parent process.
    obj->Destroy();
    d->DestroyWindowObj(obj);
    Report("Destroy + DestroyConsoleWindow survived", true);
}

// ---------------------------------------------------------------------------------------------
// Visual verification in a child process.
// ---------------------------------------------------------------------------------------------
struct ClassCtx {
    bool logView, tool, statusBar;
    wchar_t found[24][80];
    int n;
};

static BOOL CALLBACK EnumClasses(HWND hwnd, LPARAM lp) {
    ClassCtx* c = (ClassCtx*)lp;
    wchar_t cls[80];
    GetClassNameW(hwnd, cls, 80);
    if (c->n < 24) { wcsncpy_s(c->found[c->n], 80, cls, 79); ++c->n; }
    if (wcscmp(cls, L"BnS Console:LogView") == 0) c->logView = true;
    if (wcscmp(cls, L"BnS Console:Tool") == 0) c->tool = true;
    if (wcscmp(cls, L"msctls_statusbar32") == 0) c->statusBar = true;
    return TRUE;
}

static DWORD WINAPI ShowRunThread(void* arg) {
    ((blood::IConsoleWindow*)arg)->ShowRun();
    return 0;
}

static int VisualMain(const wchar_t* dllPath, const wchar_t* tag) {
    const unsigned kBitHwnd = 1, kBitTitle = 2, kBitChildren = 4, kBitExit = 8, kBitEnded = 16;
    unsigned bits = 0;
    Dll d;
    wchar_t err[256];
    if (!LoadDll(dllPath, &d, err, 256)) { wprintf(L"ERROR %ls: %ls\n", tag, err); return 0; }

    wchar_t title[256];
    swprintf_s(title, 256, L"bsConsole_diff_%s_%lu", tag, GetCurrentProcessId());
    bool reloadFlag = true, shutdownFlag = true;
    g_exitCount = 0;
    blood::IConsoleWindow* obj = d.CreateWindowObj(d.module);
    if (!obj) { wprintf(L"ERROR %ls: CreateConsoleWindow returned NULL\n", tag); FreeLibrary(d.module); return 0; }
    if (!obj->Create(NULL, title, L"RankingDaemon", NULL, ExitCallback,
                     &reloadFlag, &shutdownFlag, NULL, NULL)) {
        wprintf(L"ERROR %ls: Create returned false\n", tag);
        d.DestroyWindowObj(obj); FreeLibrary(d.module); return 0;
    }

    HANDLE th = CreateThread(NULL, 0, ShowRunThread, obj, 0, NULL);
    if (!th) { wprintf(L"ERROR %ls: CreateThread failed %lu\n", tag, GetLastError()); d.DestroyWindowObj(obj); FreeLibrary(d.module); return 0; }

    HWND hwnd = NULL;
    DWORD t0 = GetTickCount();
    while (GetTickCount() - t0 < 6000) {
        hwnd = obj->GetHwnd();
        if (hwnd && IsWindow(hwnd)) break;
        Sleep(20);
    }
    if (hwnd && IsWindow(hwnd)) {
        bits |= kBitHwnd;
        wchar_t wanted[256];
        swprintf_s(wanted, 256, L"BSConsole Diff %s", tag);
        obj->SetTitle(wanted);
        Sleep(150);
        wchar_t actual[256] = L"";
        GetWindowTextW(hwnd, actual, 256);
        bool titleOk = wcscmp(actual, wanted) == 0;
        if (titleOk) bits |= kBitTitle;
        else wprintf(L"  %ls title='%ls' expected='%ls'\n", tag, actual, wanted);

        ClassCtx classes;
        memset(&classes, 0, sizeof(classes));
        EnumChildWindows(hwnd, EnumClasses, (LPARAM)&classes);
        if (classes.logView && classes.tool && classes.statusBar) bits |= kBitChildren;
        else {
            wprintf(L"  %ls missing children (logview=%d tool=%d status=%d); found:", tag,
                    classes.logView, classes.tool, classes.statusBar);
            for (int i = 0; i < classes.n; ++i) wprintf(L" [%ls]", classes.found[i]);
            wprintf(L"\n");
        }

        EnqCtx ctx = { obj, 200, true };
        HANDLE enq = CreateThread(NULL, 0, EnqueueThread, &ctx, 0, NULL);
        if (enq) { WaitForSingleObject(enq, 15000); CloseHandle(enq); }
        else wprintf(L"  %ls enqueue thread failed %lu\n", tag, GetLastError());
        Sleep(300);

        PostMessageW(hwnd, WM_CLOSE, 0, 0);
        t0 = GetTickCount();
        while (GetTickCount() - t0 < 4000 && g_exitCount == 0) Sleep(20);
        if (g_exitCount > 0) bits |= kBitExit;
        else wprintf(L"  %ls exit callback was NOT called\n", tag);
    } else {
        wprintf(L"ERROR %ls: no window after 6 s (GetHwnd=%p)\n", tag, (void*)hwnd);
    }

    if (WaitForSingleObject(th, 8000) == WAIT_OBJECT_0) bits |= kBitEnded;
    CloseHandle(th);
    obj->Destroy();
    d.DestroyWindowObj(obj);
    FreeRecords();
    FreeLibrary(d.module);

    wprintf(L"VISUAL %ls: hwnd=%d title=%d children=%d exit=%d ended=%d (bits=%u)\n",
            tag, !!(bits & kBitHwnd), !!(bits & kBitTitle), !!(bits & kBitChildren),
            !!(bits & kBitExit), !!(bits & kBitEnded), bits);
    fflush(stdout);
    return (int)bits;
}

// ---------------------------------------------------------------------------------------------
static int RunVisualChild(const wchar_t* self, const wchar_t* dll, const wchar_t* tag) {
    wchar_t cmdline[2048];
    swprintf_s(cmdline, 2048, L"\"%s\" /visual \"%s\" %s", self, dll, tag);
    STARTUPINFOW si; memset(&si, 0, sizeof(si)); si.cb = sizeof(si);
    PROCESS_INFORMATION pi; memset(&pi, 0, sizeof(pi));
    if (!CreateProcessW(self, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) {
        printf("FAIL: cannot start visual child for %ls (error %lu)\n", tag, GetLastError());
        ++g_fail; ++g_total;
        return 0;
    }
    WaitForSingleObject(pi.hProcess, 60000);
    DWORD code = 0;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    return (int)code;
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc == 4 && wcscmp(argv[1], L"/visual") == 0)
        return VisualMain(argv[2], argv[3]);
    if (argc != 3) {
        printf("usage: difftest.exe <original.dll> <reconstruction.dll>\n");
        return 2;
    }

    Dll orig, mine;
    wchar_t err[256];
    if (!LoadDll(argv[1], &orig, err, 256)) { wprintf(L"FAIL: original: %ls\n", err); return 2; }
    if (!LoadDll(argv[2], &mine, err, 256)) { wprintf(L"FAIL: reconstruction: %ls\n", err); FreeLibrary(orig.module); return 2; }

    const wchar_t* vo = orig.GetVersion();
    const wchar_t* vm = mine.GetVersion();
    Report("GetInterfaceVersion available on both", vo != NULL && vm != NULL);
    if (vo && vm) Report("GetInterfaceVersion == L\"1\" on both",
                         wcscmp(vo, L"1") == 0 && wcscmp(vm, L"1") == 0);

    blood::IConsoleWindow* o = orig.CreateWindowObj(orig.module);
    blood::IConsoleWindow* m = mine.CreateWindowObj(mine.module);
    Report("CreateConsoleWindow returns non-NULL on both", o != NULL && m != NULL);
    if (o && m) CheckOriginalSlots(o, orig.module);

    if (o) ExerciseNonVisual(&orig, o, L"orig");
    if (m) ExerciseNonVisual(&mine, m, L"new");

    FreeRecords();
    FreeLibrary(orig.module);
    FreeLibrary(mine.module);

    wchar_t self[MAX_PATH];
    GetModuleFileNameW(NULL, self, MAX_PATH);
    int bo = RunVisualChild(self, argv[1], L"orig");
    int bn = RunVisualChild(self, argv[2], L"new");
    const char* names[5] = { "window created", "title set", "three expected children",
                             "exit callback called", "ShowRun thread ended" };
    for (int i = 0; i < 5; ++i) {
        unsigned bit = 1u << i;
        Report(names[i], (bo & bit) && (bn & bit));
    }

    printf("\n%s: %ld items compared, %ld failures\n", g_fail ? "FAIL" : "PASS", g_total, g_fail);
    fflush(stdout);
    return g_fail ? 1 : 0;
}
