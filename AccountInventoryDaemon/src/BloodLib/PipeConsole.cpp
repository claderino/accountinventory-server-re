// BloodLib - named-pipe management console (original: source\PipeConsole.cpp).
// Four \\.\pipe\bns_daemon_%08x instances, 0x2000-byte UTF-16LE message buffers, 1000 ms poll.
#include "BloodLib.h"
#include "Thread.h"
#include "Daemon.h"
#include <shellapi.h>

#pragma comment(lib, "shell32.lib")

SOURCE_FILE("PipeConsole.cpp");

namespace blood {
namespace {

const int kPipeCount = 4;
const DWORD kBufferSize = 0x2000;
const DWORD kFrozenLimitMs = 60000;
const DWORD kFrozenCheckMs = 5000;

const wchar_t* const kStateNames[10] = {
    L"UNKNOWN", L"STOPPED", L"STOPPING", L"STARTING", L"RUNNING",
    L"INVALID", L"FLUSHING", L"COOLDOWNING", L"COOLDOWNED", L"RELOADING"
};

const wchar_t* StateName(int s) {
    return (s >= 0 && s < 10) ? kStateNames[s] : kStateNames[0];
}

struct CommandNode {
    CommandNode* next;
    CommandNode* prev;
    wchar_t** argv;
    int argc;
};

struct ThreadStateNode {
    ThreadStateNode* next;
    wchar_t name[129];
    wchar_t detail[129];
};

struct PipeCtx {
    OVERLAPPED overlapped;             // +0x00
    HANDLE event;                      // +0x18
    HANDLE pipe;                       // +0x20
    u32 state;                         // +0x28 0=connect, 1=read, 2=write
    u32 ioPending;                     // +0x2C
    DWORD bytes;                       // +0x30
    wchar_t readBuffer[0x1000];        // +0x38
    wchar_t reply[0x1000];             // +0x2038
};

class PipeCommandProcessor : public ThreadObject {
public:
    PipeCommandProcessor() : head(NULL), tail(NULL) {
        InitializeCriticalSectionAndSpinCount(&lock, 4000);
        event = CreateEventW(NULL, FALSE, FALSE, NULL);
    }
    ~PipeCommandProcessor() override {
        Clear();
        DeleteCriticalSection(&lock);
        if (event) CloseHandle(event);
    }
    const wchar_t* Name() const override { return L"PipeCommand"; }
    bool Run() override {
        for (;;) {
            if (!running) return true;
            HANDLE waits[2] = { event, NULL };
            Daemon* d = Daemon::Instance();
            waits[1] = d ? d->StopEvent() : NULL;
            DWORD count = waits[1] ? 2 : 1;
            DWORD r = WaitForMultipleObjects(count, waits, FALSE, 1000);
            if (r == WAIT_OBJECT_0 + 1) return true;
            if (r != WAIT_OBJECT_0) continue;
            ResetEvent(event);
            for (;;) {
                EnterCriticalSection(&lock);
                CommandNode* n = head;
                if (n != NULL) {
                    head = n->next;
                    if (head) head->prev = NULL; else tail = NULL;
                }
                LeaveCriticalSection(&lock);
                if (n == NULL) break;
                // Case-sensitive dispatch; return values are deliberately ignored.
                if (n->argc > 0 && wcscmp(n->argv[0], L"cooldown-req") == 0) {
                    if (d) (void)d->Cooldown();
                } else if (n->argc > 0 && wcscmp(n->argv[0], L"reload-req") == 0) {
                    if (d) (void)d->Reload(L"reload-req", 1);
                }
                if (n->argv) LocalFree(n->argv);
                delete n;
            }
        }
    }
    void Stop() override {
        running = false;
        if (event) SetEvent(event);
    }
    bool Queue(wchar_t** argv, int argc) {
        CommandNode* n = new CommandNode;
        n->next = n->prev = NULL;
        n->argv = argv;
        n->argc = argc;
        EnterCriticalSection(&lock);
        n->prev = tail;
        if (tail) tail->next = n; else head = n;
        tail = n;
        LeaveCriticalSection(&lock);
        SetEvent(event);
        return true;
    }
    void Clear() {
        CommandNode* n = head;
        while (n) {
            CommandNode* next = n->next;
            if (n->argv) LocalFree(n->argv);
            delete n;
            n = next;
        }
        head = tail = NULL;
    }
    HANDLE event;
    CRITICAL_SECTION lock;
    CommandNode* head;
    CommandNode* tail;
};

class PipeConsole : public ThreadObject {
public:
    PipeConsole() : commandProcessor(NULL), lastCommandTick(0), lastFrozenCheck(0) {
        memset(pipes, 0, sizeof(pipes));
        InitializeCriticalSectionAndSpinCount(&stateLock, 4000);
        InitializeCriticalSectionAndSpinCount(&frozenLock, 4000);
        frozenHead = frozenTail = NULL;
        stateHead = stateTail = NULL;
    }
    ~PipeConsole() override { Shutdown(); }

    const wchar_t* Name() const override { return L"PipeConsole"; }

    bool Start(Daemon* daemon) {
        daemon_ = daemon;
        wchar_t name[128];
        swprintf_s(name, 128, L"\\\\.\\pipe\\bns_daemon_%08x", (unsigned)GetCurrentProcessId());
        for (int i = 0; i < kPipeCount; ++i) {
            PipeCtx& p = pipes[i];
            p.event = CreateEventW(NULL, TRUE, FALSE, NULL);
            if (!p.event) return false;
            memset(&p.overlapped, 0, sizeof(p.overlapped));
            p.overlapped.hEvent = p.event;
            p.pipe = CreateNamedPipeW(name,
                PIPE_ACCESS_DUPLEX | FILE_FLAG_OVERLAPPED,
                PIPE_TYPE_MESSAGE | PIPE_READMODE_MESSAGE | PIPE_WAIT,
                kPipeCount, kBufferSize, kBufferSize, 5000, NULL);
            if (p.pipe == INVALID_HANDLE_VALUE) return false;
            p.state = 0;
            p.bytes = 0;
            ConnectPipe(&p);
        }
        commandProcessor = new PipeCommandProcessor;
        running = true;
        ThreadObject* t = commandProcessor;
        if (!g_manager || !g_manager->Start(t)) { running = false; return false; }
        if (!g_manager->Start(this)) { running = false; return false; }
        lastCommandTick = GetTickCount();
        lastFrozenCheck = lastCommandTick;
        return true;
    }

    void Shutdown() {
        running = false;
        if (commandProcessor) commandProcessor->Stop();
        for (int i = 0; i < kPipeCount; ++i) {
            PipeCtx& p = pipes[i];
            if (p.pipe && p.pipe != INVALID_HANDLE_VALUE) {
                DisconnectNamedPipe(p.pipe);
                CloseHandle(p.pipe);
                p.pipe = INVALID_HANDLE_VALUE;
            }
            if (p.event) { CloseHandle(p.event); p.event = NULL; }
        }
        ClearFrozen();
        ClearState();
        DeleteCriticalSection(&frozenLock);
        DeleteCriticalSection(&stateLock);
    }

    void Stop() override { running = false; }
    bool QueueCommand(wchar_t** argv, int argc) {
        return commandProcessor != NULL && commandProcessor->Queue(argv, argc);
    }

    bool Run() override {
        for (;;) {
            if (!running) return true;
            HANDLE events[kPipeCount];
            for (int i = 0; i < kPipeCount; ++i) events[i] = pipes[i].event;
            DWORD r = WaitForMultipleObjects(kPipeCount, events, FALSE, 1000);
            if (r >= WAIT_OBJECT_0 && r < WAIT_OBJECT_0 + kPipeCount) {
                PipeCtx& p = pipes[r - WAIT_OBJECT_0];
                ResetEvent(p.event);
                DWORD transferred = 0;
                BOOL ok = GetOverlappedResult(p.pipe, &p.overlapped, &transferred, FALSE);
                if (p.state == 0) {
                    if (ok || GetLastError() == ERROR_PIPE_CONNECTED) p.state = 1;
                    memset(&p.overlapped, 0, sizeof(p.overlapped));
                    p.overlapped.hEvent = p.event;
                    ReadFile(p.pipe, p.readBuffer, kBufferSize, &p.bytes, &p.overlapped);
                } else if (p.state == 1) {
                    if (ok) {
                        ProcessRequest(&p, transferred);
                        p.state = 2;
                        memset(&p.overlapped, 0, sizeof(p.overlapped));
                        p.overlapped.hEvent = p.event;
                        WriteFile(p.pipe, p.reply, p.bytes, &p.bytes, &p.overlapped);
                    }
                } else {
                    DisconnectNamedPipe(p.pipe);
                    memset(&p.overlapped, 0, sizeof(p.overlapped));
                    p.overlapped.hEvent = p.event;
                    ConnectNamedPipe(p.pipe, &p.overlapped);
                    p.state = 0;
                }
            } else if (r == WAIT_TIMEOUT) {
                DWORD now = GetTickCount();
                if (now - lastFrozenCheck >= kFrozenCheckMs) {
                    lastFrozenCheck = now;
                    CheckFrozenThreads(now);
                }
            } else {
                LOG(0, 476, L"[pipe-console], WaitForMultipleObject error, error:%d",
                    (int)GetLastError());
            }
        }
    }

    static void SetManager(ThreadManager* m) { g_manager = m; }

private:
    static ThreadManager* g_manager;

    static bool ConnectPipe(PipeCtx* p) {
        if (!ConnectNamedPipe(p->pipe, &p->overlapped)) {
            DWORD err = GetLastError();
            if (err != ERROR_IO_PENDING && err != ERROR_PIPE_CONNECTED) {
                LOG(0, 802, L"[pipe-console], connect failed, error:%d", (int)err);
                return false;
            }
        }
        return true;
    }

    static void FormatReply(PipeCtx* p, const wchar_t* fmt, ...) {
        va_list ap;
        va_start(ap, fmt);
        int n = vswprintf_s(p->reply, 0x1000, fmt, ap);
        va_end(ap);
        p->bytes = (DWORD)(n > 0 ? n * sizeof(wchar_t) : 0);
    }

    void ThreadStateText(wchar_t* out, size_t cap) {
        out[0] = 0;
        int state = daemon_ ? daemon_->State() : 0;
        if (state == 4) {
            EnterCriticalSection(&stateLock);
            for (ThreadStateNode* n = stateHead; n; n = n->next) {
                wchar_t line[300];
                swprintf_s(line, 300, L"%s:%s ", n->name, n->detail);
                wcsncat_s(out, cap, line, _TRUNCATE);
            }
            LeaveCriticalSection(&stateLock);
        } else if (state == 5) {
            EnterCriticalSection(&frozenLock);
            for (ThreadStateNode* n = frozenHead; n; n = n->next) {
                wchar_t line[300];
                swprintf_s(line, 300, L"%s ", n->name);
                wcsncat_s(out, cap, line, _TRUNCATE);
            }
            LeaveCriticalSection(&frozenLock);
        }
    }

    void ProcessRequest(PipeCtx* p, DWORD received) {
        if (received < kBufferSize) p->readBuffer[received / sizeof(wchar_t)] = L'\0';
        else p->readBuffer[0x1000 - 1] = L'\0';
        int argc = 0;
        wchar_t** argv = CommandLineToArgvW(p->readBuffer, &argc);
        wchar_t threadText[0x400];
        ThreadStateText(threadText, 0x400);
        bool queued = false;
        if (argv != NULL && argc > 0) {
            const wchar_t* cmd = argv[0];
            if (wcscmp(cmd, L"state-req") == 0) {
                FormatReply(p, L"state-ok %s %s", StateName(daemon_ ? daemon_->State() : 0),
                            threadText);
            } else if (wcscmp(cmd, L"state-req2") == 0) {
                FormatReply(p, L"state-ok2 %s %s %s",
                            StateName(daemon_ ? daemon_->State() : 0),
                            daemon_ ? daemon_->Version() : L"", threadText);
            } else if (wcscmp(cmd, L"cooldown-req") == 0 ||
                       wcscmp(cmd, L"reload-req") == 0) {
                queued = commandProcessor != NULL &&
                         commandProcessor->Queue(argv, argc);
                argv = NULL;   // the processor owns it now
                FormatReply(p, wcscmp(cmd, L"reload-req") == 0 ?
                            (queued ? L"reload-ok" : L"reload-fail") :
                            (queued ? L"cooldown-ok" : L"cooldown-fail"));
            } else {
                LOG(0, 226, L"[pipe-console], invalid request, cmd:%s", cmd);
                FormatReply(p, L"error \"invalid cmd\"");
            }
        } else {
            FormatReply(p, L"error \"invalid cmd\"");
        }
        if (argv) LocalFree(argv);
    }

    void CheckFrozenThreads(DWORD now) {
        ThreadStateNode* n = frozenHead;
        while (n) {
            ThreadStateNode* next = n->next;
            delete n;
            n = next;
        }
        frozenHead = frozenTail = NULL;
        if (commandProcessor && commandProcessor->running &&
            now - commandProcessor->cachedTime > kFrozenLimitMs) {
            AddThreadState(&frozenHead, &frozenTail, L"pipe-command", L"frozen");
            LOG(0, 311,
                L"[thread], thread frozen, name:%s, id:0x%08x, frozen-seconds:%d, max-frozen-seconds:%d",
                L"pipe-command", (unsigned)commandProcessor->threadId,
                (int)((now - commandProcessor->cachedTime) / 1000),
                (int)(kFrozenLimitMs / 1000));
        }
    }

    static void AddThreadState(ThreadStateNode** head, ThreadStateNode** tail,
                               const wchar_t* name, const wchar_t* detail) {
        ThreadStateNode* n = new ThreadStateNode;
        wcsncpy_s(n->name, 129, name ? name : L"", _TRUNCATE);
        wcsncpy_s(n->detail, 129, detail ? detail : L"", _TRUNCATE);
        n->next = NULL;
        if (*tail) (*tail)->next = n; else *head = n;
        *tail = n;
    }
    void ClearFrozen() {
        ThreadStateNode* n = frozenHead;
        while (n) { ThreadStateNode* next = n->next; delete n; n = next; }
        frozenHead = frozenTail = NULL;
    }
    void ClearState() {
        ThreadStateNode* n = stateHead;
        while (n) { ThreadStateNode* next = n->next; delete n; n = next; }
        stateHead = stateTail = NULL;
    }

    Daemon* daemon_;
    PipeCtx pipes[kPipeCount];
    PipeCommandProcessor* commandProcessor;
    CRITICAL_SECTION stateLock;
    CRITICAL_SECTION frozenLock;
    ThreadStateNode* frozenHead;
    ThreadStateNode* frozenTail;
    ThreadStateNode* stateHead;
    ThreadStateNode* stateTail;
    DWORD lastCommandTick;
    DWORD lastFrozenCheck;
};

ThreadManager* PipeConsole::g_manager = NULL;
PipeConsole* g_pipeConsole = NULL;

} // anonymous namespace

void StartPipeConsole(Daemon* daemon) {
    ThreadManager* mgr = daemon ? daemon->Threads() : NULL;
    static ThreadManager s_manager;
    static bool s_init = false;
    if (mgr == NULL && !s_init) { s_manager.Init(2); s_init = true; mgr = &s_manager; }
    PipeConsole::SetManager(mgr);
    if (g_pipeConsole == NULL) g_pipeConsole = new PipeConsole;
    (void)g_pipeConsole->Start(daemon);
}

void StopPipeConsole() {
    if (g_pipeConsole) { g_pipeConsole->Shutdown(); delete g_pipeConsole; g_pipeConsole = NULL; }
}

// Used by WindowConsole's reload callback: enqueue "reload-req" like the pipe command does.
void PipeConsole_QueueCommand(const wchar_t* command) {
    if (g_pipeConsole == NULL || command == NULL) return;
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(command, &argc);
    if (argv && argc > 0) (void)g_pipeConsole->QueueCommand(argv, argc);
    else if (argv) LocalFree(argv);
}

} // namespace blood
