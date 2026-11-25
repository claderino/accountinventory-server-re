// BloodLib - pooled thread-local log record (original: LogBuffer.cpp).
#include "Log.h"

#include <vector>

SOURCE_FILE("LogBuffer.cpp");

namespace blood {
namespace {

struct LogBufferObjectVtbl {
    void (*destroy)(LogBuffer*);
    void* reserved0;
    void* reserved1;
    void* reserved2;
    int (*addRef)(LogBuffer*);
    void* reserved3;
    void* reserved4;
    bool (*formatTimestamp)(LogBuffer*, wchar_t*, int);
};

struct LogBufferRefVtbl {
    int (*addRef)(LogBuffer*);
    void (*release)(LogBuffer*);
    int (*getLevel)(LogBuffer*);
    bool (*isEnabled)(LogBuffer*);
    bool (*formatTimestamp)(LogBuffer*, wchar_t*, int);
};

void DestroyThunk(LogBuffer* buffer);
int AddRefThunk(LogBuffer* buffer);
bool TimestampThunk(LogBuffer* buffer, wchar_t* output, int capacity);
int RefAddRefThunk(LogBuffer* buffer) { return AddRefThunk(buffer); }
void RefReleaseThunk(LogBuffer* buffer) { buffer->Release(); }
int RefGetLevelThunk(LogBuffer* buffer) { return buffer->level + 1; }
bool RefEnabledThunk(LogBuffer* buffer) { return buffer->enabled != 0; }

LogBufferObjectVtbl g_objectVtbl = {
    &DestroyThunk, NULL, NULL, NULL, &AddRefThunk, NULL, NULL, &TimestampThunk
};
LogBufferRefVtbl g_refVtbl = {
    &RefAddRefThunk, &RefReleaseThunk, &RefGetLevelThunk, &RefEnabledThunk, &TimestampThunk
};
// bsConsole_sv64.dll treats +0x18 as an ILogBufferRef object pointer: its first word
// must be the vtable pointer. All records share this one holder.
void* g_refVtableHolder[1] = { reinterpret_cast<void*>(&g_refVtbl) };

thread_local std::vector<void*> t_freeBlocks;
std::vector<void*> g_deadBlocks;
CRITICAL_SECTION g_deadLock;
bool g_deadLockReady = false;

void EnsureDeadLock() {
    if (!g_deadLockReady) {
        InitializeCriticalSectionAndSpinCount(&g_deadLock, 4000);
        g_deadLockReady = true;
    }
}

void* AllocBlock() {
    // The original allocates 16 x 0x1000-byte blocks in one committed reservation.
    return VirtualAlloc(NULL, 0x10000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void Recycle(void* block) {
    if (t_freeBlocks.size() <= 64) {
        t_freeBlocks.push_back(block);
        return;
    }
    EnsureDeadLock();
    EnterCriticalSection(&g_deadLock);
    for (size_t i = 0; i < 32 && !t_freeBlocks.empty(); ++i) {
        g_deadBlocks.push_back(t_freeBlocks.back());
        t_freeBlocks.pop_back();
    }
    LeaveCriticalSection(&g_deadLock);
}

void DestroyThunk(LogBuffer* buffer) {
    if (buffer) buffer->Release();
}

int AddRefThunk(LogBuffer* buffer) {
    buffer->AddRef();
    return buffer->refCount;
}

bool TimestampThunk(LogBuffer* buffer, wchar_t* output, int capacity) {
    return buffer->FormatTimestamp(output, capacity);
}

} // namespace

static_assert(offsetof(LogBuffer, sizeTag) == 0x08, "LogBuffer ABI");
static_assert(offsetof(LogBuffer, refCount) == 0x0C, "LogBuffer ABI");
static_assert(offsetof(LogBuffer, refVt) == 0x18, "LogBuffer ABI");
static_assert(offsetof(LogBuffer, next) == 0x20, "LogBuffer ABI");
static_assert(offsetof(LogBuffer, level) == 0x78, "LogBuffer ABI");
static_assert(offsetof(LogBuffer, file) == 0x80, "LogBuffer ABI");
static_assert(offsetof(LogBuffer, line) == 0x88, "LogBuffer ABI");
static_assert(offsetof(LogBuffer, enabled) == 0x8C, "LogBuffer ABI");
static_assert(offsetof(LogBuffer, message) == 0x8E, "LogBuffer ABI");
static_assert(sizeof(LogBuffer) == 0x1000, "LogBuffer ABI");

LogBuffer::LogBuffer()
    : vt(&g_objectVtbl), sizeTag(0x10A), refCount(1), refVt(&g_refVtableHolder), level(LogError),
      file(L""), line(0), enabled(true), reserved8D(0) {
    memset(reserved10, 0, sizeof(reserved10));
    memset(next, 0, sizeof(next));
    memset(reserved68, 0, sizeof(reserved68));
    memset(reserved70, 0, sizeof(reserved70));
    memset(message, 0, sizeof(message));
}

LogBuffer::~LogBuffer() {
    vt = NULL;
    refVt = NULL;
}

void LogBuffer_InitTLS() {
    // thread_local storage above is this build's TLS slot 3 (TEB array +0x18).
}

LogBuffer* LogBuffer_Acquire() {
    void* block = NULL;
    if (!t_freeBlocks.empty()) {
        block = t_freeBlocks.back();
        t_freeBlocks.pop_back();
    } else {
        EnsureDeadLock();
        EnterCriticalSection(&g_deadLock);
        if (!g_deadBlocks.empty()) {
            block = g_deadBlocks.back();
            g_deadBlocks.pop_back();
        }
        LeaveCriticalSection(&g_deadLock);
        if (!block) {
            u8* batch = static_cast<u8*>(AllocBlock());
            if (batch) {
                for (int i = 15; i >= 1; --i) t_freeBlocks.push_back(batch + i * 0x1000);
                block = batch;
            }
        }
    }
    if (!block) return NULL;
    LogBuffer* buffer = new (block) LogBuffer();
    for (int i = 0; i < 9; ++i) buffer->next[i] = NULL;
    return buffer;
}

void LogBuffer::AddRef() { InterlockedIncrement(&refCount); }

void LogBuffer::Release() {
    if (InterlockedDecrement(&refCount) != 0) return;
    void* block = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(this) & ~uintptr_t(0xFFF));
    this->~LogBuffer();
    Recycle(block);
}

void LogBuffer::SetHeader(int newLevel, const wchar_t* newFile, int newLine) {
    SYSTEMTIME stamp;
    GetLocalTime(&stamp);
    memcpy(reinterpret_cast<void*>(reinterpret_cast<u8*>(this) + 0x68), &stamp, sizeof(stamp));
    level = newLevel;
    file = newFile ? newFile : L"";
    line = newLine;
    enabled = true;
    message[0] = L'\0';
}

bool LogBuffer::FormatTimestamp(wchar_t* output, int capacity) const {
    const SYSTEMTIME* stamp = reinterpret_cast<const SYSTEMTIME*>(
        reinterpret_cast<const u8*>(this) + 0x68);
    return swprintf_s(output, capacity, L"%04d/%02d/%02d-%02d:%02d:%02d.%03d%s%s",
                      stamp->wYear, stamp->wMonth, stamp->wDay, stamp->wHour,
                      stamp->wMinute, stamp->wSecond, stamp->wMilliseconds,
                      L",", L"") >= 0;
}

} // namespace blood
