// BloodLib - reference counting, history records, and fatal assertion entry points.
#include "BloodLib.h"
#include "Daemon.h"
#include "DelayedRelease.h"

#include <time.h>

SOURCE_FILE("RefCntObject.cpp");

namespace blood {
namespace {

struct HistoryRecord {
    i64 time;
    const wchar_t* file;
    i32 newRef;
    DWORD threadId;
    u16 line;
    u8 opcode;
    u8 reserved[3];
};
static_assert(sizeof(HistoryRecord) == 0x20, "refcnt history record ABI");

struct HistoryHeader {
    volatile LONG dumping;
    volatile LONG count;
    LONG maximum;
    HistoryRecord* records;
};
static_assert(offsetof(HistoryHeader, records) == 0x10, "refcnt history ABI");

CRITICAL_SECTION g_historyLock;
bool g_historyLockReady = false;

void EnsureHistoryLock() {
    if (!g_historyLockReady) {
        InitializeCriticalSectionAndSpinCount(&g_historyLock, 4000);
        g_historyLockReady = true;
    }
}

} // namespace

RefCntObject::RefCntObject() : tag(0), refCount(1), history(NULL) {
    tag |= 0xF0;
}

void RefCntObject::Release(const wchar_t*, int) { delete this; }
void RefCntObject::Destroy() { delete this; }
void RefCntObject::ReleaseDelayed(const wchar_t* file, int line) {
    RefCnt_ReleaseDelayedObject(this, file, line);
}

void RefCntObject::operator delete(void* memory) noexcept {
    Memory_Free(memory);
}

int RefCnt_AddRef(RefCntObject* object, const wchar_t* file, int line) {
    if (!object) return 0;
    if (object->refCount <= 0) {
        LogWrite(0, L"<server>\\BloodLib\\include\\RefCntObject.h", 0x92,
                 L"== CRASH == [ref obj], cannot add reference count, this:0x%p, ref:%d, tag:0x%08x, file:%s, line:%d",
                 object, object->refCount, object->tag, file, line);
        Assert_FailW(L"<server>\\BloodLib\\include\\RefCntObject.h", 0x92,
                     L"== CRASH == [ref obj], cannot add reference count, this:0x%p, ref:%d, tag:0x%08x, file:%s, line:%d",
                     object, object->refCount, object->tag, file, line);
        return 0;
    }
    LONG result = InterlockedIncrement(&object->refCount);
    if (object->history)
        RefCntHistory_Add(object->history, 1, result, file, line);
    return result;
}

int RefCnt_Release(RefCntObject* object, const wchar_t* file, int line) {
    if (!object) return 0;
    if (object->refCount <= 0) {
        LogWrite(0, L"<server>\\bloodlib\\include\\RefCntObject.h", 0xA7,
                 L"== CRASH == [ref obj], cannot remove reference count, this:0x%p, ref:%d, tag:0x%08x, file:%s, line:%d",
                 object, object->refCount, object->tag, file, line);
        Assert_FailW(L"<server>\\bloodlib\\include\\RefCntObject.h", 0xA7,
                     L"== CRASH == [ref obj], cannot remove reference count, this:0x%p, ref:%d, tag:0x%08x, file:%s, line:%d",
                     object, object->refCount, object->tag, file, line);
        return 0;
    }
    LONG result = InterlockedDecrement(&object->refCount);
    if (object->history)
        RefCntHistory_Add(object->history, 2, result, file, line);
    if (result == 0)
        object->Release(file, line);
    return result;
}

void RefCntHistory_Add(void* history, u8 opcode, i32 newRef, const wchar_t* file, int line) {
    if (!history) return;
    EnsureHistoryLock();
    HistoryHeader* header = static_cast<HistoryHeader*>(history);
    if (header->dumping || !header->records) return;
    LONG slot = InterlockedIncrement(&header->count) - 1;
    if (slot < 0 || slot >= header->maximum) return;
    HistoryRecord& record = header->records[slot];
    record.time = static_cast<i64>(_time64(NULL));
    record.file = file;
    record.newRef = newRef;
    record.threadId = GetCurrentThreadId();
    record.line = static_cast<u16>(line);
    record.opcode = opcode;
    record.reserved[0] = record.reserved[1] = record.reserved[2] = 0;
}

void RefCntHistory_Dump(void* history, RefCntObject* object) {
    if (!history || !object) return;
    EnsureHistoryLock();
    HistoryHeader* header = static_cast<HistoryHeader*>(history);
    if (InterlockedCompareExchange(&header->dumping, 1, 0) != 0) {
        LogWrite(0, L"source\\RefCntObject.cpp", 0x12E,
                 L"[refcnt-history], already dumped, ptr:0x%p, tag:0x%08x",
                 object, object->tag);
        return;
    }
    LogWrite(1, L"source\\RefCntObject.cpp", 0x132,
             L"[refcnt-history], history dump, ptr:0x%p, tag:0x%08x, history-count:%d, history-max:%d",
             object, object->tag, header->count, header->maximum);
    int calculated = 1;
    for (LONG i = 0; i < header->count; ++i) {
        const HistoryRecord& record = header->records[i];
        if (!record.time && !record.file && !record.newRef && !record.threadId &&
            !record.line && !record.opcode)
            continue;
        const wchar_t* name = L"unknown_file";
        if (record.file) {
            const wchar_t* slash = wcsrchr(record.file, L'\\');
            name = slash ? slash + 1 : record.file;
        }
        struct tm local;
        _localtime64_s(&local, reinterpret_cast<const __time64_t*>(&record.time));
        wchar_t timestamp[128];
        swprintf_s(timestamp, 128, L"%04d/%02d/%02d-%02d:%02d:%02d%s%s",
                   local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
                   local.tm_hour, local.tm_min, local.tm_sec, L",", L"");
        const wchar_t* operation = L"none";
        if (record.opcode == 1) { operation = L"addref"; ++calculated; }
        else if (record.opcode == 2) { operation = L"delref"; --calculated; }
        LogWrite(1, L"source\\RefCntObject.cpp", 0x15A,
                 L"[refcnt-history], ptr:0x%p, tag:0x%08x, time:%s, thread-id:%d, seq:%d, op:%s, ref:%d, calc-ref:%d, file:%s, line:%d",
                 object, object->tag, timestamp, record.threadId, i + 1, operation,
                 record.newRef, calculated, name, record.line);
    }
    InterlockedExchange(&header->dumping, 0);
}

static AssertFailHandler g_assertHandler = NULL;
void SetAssertFailHandler(AssertFailHandler handler) { g_assertHandler = handler; }

void Assert_FailW(const wchar_t* file, int line, const wchar_t* fmt, ...) {
    if (!g_assertHandler) return;
    va_list ap;
    va_start(ap, fmt);
    g_assertHandler(file, line, fmt, ap);
    va_end(ap);
}

void Assert_FailA(const wchar_t* file, int line, const wchar_t* fmt, ...) {
    void* storage = HeapAlloc(GetProcessHeap(), 0, 0x2010);
    if (!storage) {
        RaiseException(kAssertExceptionCode, 0, 0, NULL);
        return;
    }
    wchar_t* message = reinterpret_cast<wchar_t*>(static_cast<u8*>(storage) + 0x0C);
    va_list ap;
    va_start(ap, fmt);
    vswprintf_s(message, 0x1000, fmt ? fmt : L"", ap);
    va_end(ap);
    memcpy(storage, &file, sizeof(file));
    memcpy(static_cast<u8*>(storage) + 8, &line, sizeof(line));
    ULONG_PTR info[3] = { reinterpret_cast<ULONG_PTR>(storage),
                          static_cast<ULONG_PTR>(line),
                          reinterpret_cast<ULONG_PTR>(message) };
    // The original passes one pointer whose target is {file,line,message}. Keep that shape.
    info[0] = reinterpret_cast<ULONG_PTR>(storage);
    info[1] = 0;
    info[2] = 0;
    RaiseException(kAssertExceptionCode, 1, 1, info);
    Daemon* daemon = Daemon::Instance();
    if (daemon && daemon->StopEvent()) SetEvent(daemon->StopEvent());
    Sleep(1000);
}

} // namespace blood
