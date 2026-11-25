// BloodLib - central logger and log worker (original: Log.cpp).
#include "Daemon.h"
#include "Log.h"
#include "PacketBuffer.h"

#include <new>

SOURCE_FILE("Log.cpp");

namespace blood {

static Console* g_logConsole = nullptr;
void SetLogConsole(Console* console) { g_logConsole = console; }

namespace {

alignas(8) unsigned char g_logStorage[sizeof(Log)];
Log* g_log = NULL;

void WriteFormattedV(int level, const wchar_t* file, int line, const wchar_t* fmt,
                     va_list args) {
    Log* log = TheLog();
    if (!log || !log->enabled || level < 0 || level >= LogLevelCount ||
        log->levelCount[level] <= 0)
        return;
    LogBuffer* record = LogBuffer_Acquire();
    if (!record) return;
    record->SetHeader(level, file, line);
    if (fmt) {
        va_list copy;
        va_copy(copy, args);
        vswprintf_s(record->message, 0x7B8, fmt, copy);
        va_end(copy);
    }
    log->Dispatch(record);
    // Mirror every log line into the active console (text or window) so the daemon
    // window shows the same output as the log file, like the original.
    if (g_logConsole) {
        // The window console displays log records in its main output pane; other
        // consoles only have a status line.
        if (!g_logConsole->WriteLog(record))
            g_logConsole->Print(level, L"%s", record->message);
    }
    record->Release();
}

} // namespace

const wchar_t* LogLevelName(int level) {
    static const wchar_t* const names[12] = {
        L"error", L"debug", L"trace", L"notice", L"resource", L"archive",
        L"undef1", L"undef2", L"undef3", L"undef4", L"undef5", L"undef6"
    };
    return level >= 0 && level < 12 ? names[level] : L"";
}

static_assert(offsetof(Log, enabled) == 0x30, "Log ABI");
static_assert(offsetof(Log, event) == 0x38, "Log ABI");
static_assert(offsetof(Log, media) == 0x40, "Log ABI");
static_assert(offsetof(Log, mediaCount) == 0x88, "Log ABI");
static_assert(offsetof(Log, levelHeads) == 0x90, "Log ABI");
static_assert(offsetof(Log, levelCount) == 0xF0, "Log ABI");
static_assert(sizeof(Log) == 0x120, "Log ABI");

Log::Log()
    : enabled(false), flushRequested(false), event(NULL), mediaCount(0) {
    field18 = 0x3C;
    memset(reserved32, 0, sizeof(reserved32));
    memset(media, 0, sizeof(media));
    memset(reserved8C, 0, sizeof(reserved8C));
    memset(levelHeads, 0, sizeof(levelHeads));
    for (int i = 0; i < LogLevelCount; ++i) levelCount[i] = 0;
}

Log::~Log() {
    if (g_log == this) g_log = NULL;
    if (event) CloseHandle(event);
    event = NULL;
}

bool Log::Init() {
    g_log = this;
    enabled = true;
    return true;
}

bool Log_Create() {
    if (g_log) return true;
    g_log = new (g_logStorage) Log();
    g_log->Init();
    Daemon* daemon = Daemon::Instance();
    if (daemon && daemon->Threads())
        daemon->Threads()->Start(g_log, NULL);
    return true;
}

Log* TheLog() { return g_log; }

bool Log::AddMedia(LogMedia* medium) {
    if (!g_log || !medium || !medium->Supports() || mediaCount >= 9) return false;
    medium->index = static_cast<int>(mediaCount);
    media[mediaCount++] = medium;
    for (int level = 0; level < LogLevelCount; ++level) {
        medium->next[level] = NULL;
        if (!medium->levelEnabled[level]) continue;
        LogMedia** link = &levelHeads[level];
        while (*link) link = &(*link)->next[level];
        *link = medium;
        InterlockedIncrement(&levelCount[level]);
    }
    return true;
}

void Log::Dispatch(LogBuffer* record) {
    if (!record || record->level < 0 || record->level >= LogLevelCount) return;
    bool needFlush = false;
    for (LogMedia* medium = levelHeads[record->level]; medium; medium = medium->next[record->level]) {
        if (!medium->AddRecord(record)) needFlush = true;
    }
    if (needFlush && event) SetEvent(event);
}

void Log::FlushAndStop() {
    if (running) {
        flushRequested = true;
        if (event) SetEvent(event);
        running = false;
    }
}

void Log::SignalFlush() {
    if (running) {
        flushRequested = true;
        if (event) SetEvent(event);
    }
}

void Log::Flush() {
    for (u32 i = 0; i < mediaCount; ++i)
        if (media[i]) media[i]->Flush();
}

bool Log::Run() {
    LogBuffer_InitTLS();
    PacketBuffer_InitTLS();
    event = CreateEventW(NULL, FALSE, FALSE, NULL);
    while (running) {
        if (event) WaitForSingleObject(event, 1000);
        Daemon* daemon = Daemon::Instance();
        cachedTime = daemon ? daemon->StartTick() : GetTickCount64();
        for (u32 i = 0; i < 9; ++i)
            if (media[i]) media[i]->FlushQueue();
        if (flushRequested) {
            flushRequested = false;
            for (u32 i = 0; i < mediaCount; ++i)
                if (media[i]) media[i]->Flush();
        }
    }
    return true;
}

void LogWriteV(int level, const wchar_t* file, int line, const wchar_t* fmt, va_list args) {
    WriteFormattedV(level, file, line, fmt, args);
}

void LogWrite(int level, const wchar_t* file, int line, const wchar_t* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    LogWriteV(level, file, line, fmt, ap);
    va_end(ap);
}

void LogWriteA(const wchar_t* file, int line, DWORD errorCode, const wchar_t* api,
               va_list* args) {
    Log* log = TheLog();
    if (!log || !log->enabled || log->levelCount[0] <= 0) return;
    wchar_t systemMessage[512];
    systemMessage[0] = L'\0';
    FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS, NULL,
                   errorCode, 0, systemMessage, 512, NULL);
    size_t length = wcslen(systemMessage);
    while (length && (systemMessage[length - 1] == L'\r' || systemMessage[length - 1] == L'\n'))
        systemMessage[--length] = L'\0';
    wchar_t formatted[512];
    swprintf_s(formatted, 512, L"%s, error: %d, msg: %s",
               api ? api : L"", static_cast<int>(errorCode), systemMessage);
    WriteFormattedV(0, file, line, formatted, *args);
}

void LogWrite0(const wchar_t* file, int line, DWORD errorCode, const wchar_t* api, ...) {
    va_list ap;
    va_start(ap, api);
    LogWriteA(file, line, errorCode, api, &ap);
    va_end(ap);
}

} // namespace blood
