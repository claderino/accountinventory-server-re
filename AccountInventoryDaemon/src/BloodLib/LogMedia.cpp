// BloodLib - base log medium and its per-record queue (original: LogMedia.cpp).
#include "Log.h"

SOURCE_FILE("LogMedia.cpp");

namespace blood {

static_assert(offsetof(LogMedia, index) == 0x08, "LogMedia ABI");
static_assert(offsetof(LogMedia, next) == 0x10, "LogMedia ABI");
static_assert(offsetof(LogMedia, levelEnabled) == 0x70, "LogMedia ABI");
static_assert(offsetof(LogMedia, appendLock) == 0x80, "LogMedia ABI");
static_assert(offsetof(LogMedia, head) == 0xA8, "LogMedia ABI");
static_assert(offsetof(LogMedia, tail) == 0xB0, "LogMedia ABI");
static_assert(sizeof(LogMedia) == 0xB8, "LogMedia ABI");

LogMedia::LogMedia()
    : index(-1), head(NULL), tail(NULL) {
    InitializeCriticalSectionAndSpinCount(&appendLock, 4000);
    memset(next, 0, sizeof(next));
    memset(levelEnabled, 1, sizeof(levelEnabled));
    memset(reserved7C, 0, sizeof(reserved7C));
}

LogMedia::~LogMedia() {
    DeleteCriticalSection(&appendLock);
}

bool LogMedia::Supports() { return true; }

void LogMedia::SetLevelEnabled(int level, bool value) {
    if (level >= 0 && level < 12) levelEnabled[level] = value ? 1 : 0;
}

bool LogMedia::AddRecord(LogBuffer* record) {
    if (!record) return true;
    record->AddRef();
    bool wasEmpty = false;
    EnterCriticalSection(&appendLock);
    if (index >= 0 && index < 9) record->next[index] = NULL;
    if (!tail) {
        head = record;
        wasEmpty = true;
    } else {
        if (index >= 0 && index < 9) tail->next[index] = record;
    }
    tail = record;
    LeaveCriticalSection(&appendLock);
    return wasEmpty;
}

void LogMedia::FlushQueue() {
    EnterCriticalSection(&appendLock);
    LogBuffer* list = head;
    head = tail = NULL;
    LeaveCriticalSection(&appendLock);
    if (!list) return;
    // WriteLog receives the whole detached chain and formats every record in it;
    // calling WriteLog once per record duplicates every line in the file.
    WriteLog(list);
    while (list) {
        LogBuffer* next = NULL;
        if (index >= 0 && index < 9) next = list->next[index];
        if (index >= 0 && index < 9) list->next[index] = NULL;
        list->Release();
        list = next;
    }
}

void LogMedia::Flush() { FlushQueue(); }

} // namespace blood
