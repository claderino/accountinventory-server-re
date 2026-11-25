// BloodLib - window console (original: source\WindowConsole.cpp).
//
// CROSS-FILE EXTERNS: blood::PipeConsole_QueueCommand(const wchar_t*) from PipeConsole.cpp,
// blood::TheLog(), blood::LogBuffer (Log.cpp), blood::Daemon (Daemon.cpp).
#include "BloodLib.h"
#include "Thread.h"
#include "Daemon.h"
#include "Log.h"
#include "BsConsole.h"

SOURCE_FILE("WindowConsole.cpp");

namespace blood {

void PipeConsole_QueueCommand(const wchar_t* command);

namespace {

const wchar_t* StateText(int s) {
    static const wchar_t* const kNames[10] = {
        L"UNKNOWN", L"STOPPED", L"STOPPING", L"STARTING", L"RUNNING",
        L"INVALID", L"FLUSHING", L"COOLDOWNING", L"COOLDOWNED", L"RELOADING"
    };
    return (s >= 0 && s < 10) ? kNames[s] : kNames[0];
}

static HANDLE g_consoleStopEvent = NULL;
void SetConsoleStopEventInternal(HANDLE stopEvent) { g_consoleStopEvent = stopEvent; }

void ExitCallback() {
    Daemon* d = Daemon::Instance();
    if (d) SetEvent(d->StopEvent());       // original 2CFC0: only signals the stop event
    else if (g_consoleStopEvent) SetEvent(g_consoleStopEvent);
}

void ReloadCallback() { PipeConsole_QueueCommand(L"reload-req"); }
void ShutdownCallback() {
    Daemon* d = Daemon::Instance();
    if (d) (void)d->Cooldown();
    else if (g_consoleStopEvent) SetEvent(g_consoleStopEvent);
}
int HookStub() { return 0; }               // original 18610: ret 0

class WindowConsole;

class WindowConsoleThread : public ThreadObject {
public:
    explicit WindowConsoleThread(WindowConsole* owner)
        : owner_(owner), startedHolder_(NULL) {
        started_ = CreateEventW(NULL, TRUE, FALSE, NULL);
        startedHolder_ = new HANDLE(started_);
    }
    ~WindowConsoleThread() override {
        if (startedHolder_) { delete startedHolder_; startedHolder_ = NULL; }
        if (started_) { CloseHandle(started_); started_ = NULL; }
    }
    const wchar_t* Name() const override { return L"WindowConsole"; }
    bool Run() override;
    void Stop() override { running = false; }
    void WaitStarted() {
        if (started_) WaitForSingleObject(started_, INFINITE);
    }
    HANDLE StartedHandle() const { return started_; }

private:
    WindowConsole* owner_;
    HANDLE started_;
    HANDLE* startedHolder_;
};

class WindowConsole : public Console {
public:
    WindowConsole()
        : window_(NULL), windowThread_(NULL), reloadEnabled_(true), shutdownEnabled_(true) {
        memset(&dll_, 0, sizeof(dll_));
    }
    ~WindowConsole() override { Close(); }

    bool Open() override {
        Daemon* d = Daemon::Instance();
        if (!ConsoleDll_Open(&dll_)) return false;   // no version check, no text fallback
        window_ = dll_.CreateConsoleWindow(dll_.module);
        if (window_ == NULL) {
            ConsoleDll_Close(&dll_);
            return false;
        }
        const wchar_t* name = d ? d->GetName() : L"AchievementDaemon";
        // Title AND class name are GetName() (both "AchievementDaemon").
        window_->Create(NULL, name, name, NULL, &ExitCallback,
                        &reloadEnabled_, &shutdownEnabled_, NULL, (void*)&HookStub);
        windowThread_ = new WindowConsoleThread(this);
        ThreadManager* mgr = d ? d->Threads() : NULL;
        if (mgr != NULL && mgr->Start(windowThread_)) windowThread_->WaitStarted();
        else {
            // No manager: run the thread directly so the started event still fires.
            HANDLE h = CreateThread(NULL, 0, StartedThunk, windowThread_, 0, NULL);
            windowThread_->WaitStarted();
            if (h) CloseHandle(h);
        }
        FreeConsole();
        return true;
    }

    void Close() override {
        if (windowThread_ != NULL) {
            windowThread_->Stop();
            ThreadManager* mgr = Daemon::Instance() ? Daemon::Instance()->Threads() : NULL;
            if (mgr != NULL) mgr->Stop(windowThread_, 2000, true, true);
            delete windowThread_;
            windowThread_ = NULL;
        }
        if (window_ != NULL) {
            window_->Destroy();
            if (dll_.DestroyConsoleWindow) dll_.DestroyConsoleWindow(window_);
            window_ = NULL;
        }
        while (pendingHead_) {
            PendingRecord* next = pendingHead_->next;
            pendingHead_->record->Release();
            delete pendingHead_;
            pendingHead_ = next;
        }
        if (dll_.IsOpen()) ConsoleDll_Close(&dll_);
    }

    void SetState(int state) override {
        // The original only calls DLL slot 11 (SetStateText) here.
        if (window_ != NULL) window_->SetStateText(StateText(state));
    }

    void SetTitle(const wchar_t* title) override {
        if (window_ != NULL) window_->SetTitle(title);
    }

    void Print(int level, const wchar_t* fmt, ...) override {
        (void)level;
        if (window_ == NULL || fmt == NULL) return;
        wchar_t text[0x7B8];
        va_list ap;
        va_start(ap, fmt);
        if (vswprintf_s(text, 0x7B8, fmt, ap) < 0) text[0x7B7] = L'\0';
        va_end(ap);
        window_->ShowMessage(text, 10000);
    }

    void Printf(const wchar_t* fmt, ...) {
        wchar_t text[0x7B8];
        va_list ap;
        va_start(ap, fmt);
        if (vswprintf_s(text, 0x7B8, fmt ? fmt : L"", ap) < 0) text[0x7B7] = L'\0';
        va_end(ap);
        if (window_) window_->ShowMessage(text, 10000);
    }

    void ProcessLogRecords(LogBuffer* list) {
        for (LogBuffer* record = list; record != NULL; record = record->next[0]) {
            if (window_ != NULL && record->enabled) window_->EnqueueLogRecord(record);
        }
    }

    bool WriteLog(LogBuffer* record) override {
        if (record == NULL || !record->enabled || window_ == NULL) return true;
        // bsConsole_sv64.dll keeps the LogBuffer pointer in its own ring buffer and
        // renders it later, so hold a reference until the window is destroyed.
        record->AddRef();
        PendingRecord* pending = new PendingRecord;
        pending->record = record;
        pending->next = pendingHead_;
        pendingHead_ = pending;
        window_->EnqueueLogRecord(record);
        return true;
    }

    bool IsWindowConsole() const override { return window_ != NULL; }
    HWND Hwnd() const override { return window_ ? window_->GetHwnd() : NULL; }
    void RunWindow() { if (window_) window_->ShowRun(); }

private:
    struct PendingRecord { LogBuffer* record; PendingRecord* next; };
    PendingRecord* pendingHead_ = NULL;

    static DWORD WINAPI StartedThunk(void* arg) {
        static_cast<WindowConsoleThread*>(arg)->Run();
        return 0;
    }
    ConsoleDll dll_;
    IConsoleWindow* window_;
    WindowConsoleThread* windowThread_;
    bool reloadEnabled_;
    bool shutdownEnabled_;
};

bool WindowConsoleThread::Run() {
    if (started_) SetEvent(started_);
    srand((unsigned)time(NULL));
    if (owner_) owner_->RunWindow();
    return true;
}

} // anonymous namespace

void SetConsoleStopEvent(HANDLE stopEvent) { SetConsoleStopEventInternal(stopEvent); }

Console* CreateWindowConsole() { return new WindowConsole; }

} // namespace blood
