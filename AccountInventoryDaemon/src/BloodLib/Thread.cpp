// BloodLib - ThreadManager (original: src\Thread.cpp).
#include "Thread.h"

SOURCE_FILE("Thread.cpp");

namespace blood {
namespace {
thread_local ThreadObject* t_currentThread = NULL;

unsigned __stdcall ThreadProc(void* argument) {
    ThreadObject* object = static_cast<ThreadObject*>(argument);
    if (!object) return 0;
    t_currentThread = object; // original TLS slot 5 (TEB array +0x28)
    object->threadId = GetCurrentThreadId();
    object->running = true;
    object->cachedTime = object->manager ? object->manager->ownerTime : 0;
    object->Run();
    object->running = false;
    t_currentThread = NULL;
    return 0;
}
} // namespace

static_assert(offsetof(ThreadObject, threadId) == 0x08, "ThreadObject ABI");
static_assert(offsetof(ThreadObject, cachedTime) == 0x10, "ThreadObject ABI");
static_assert(offsetof(ThreadObject, manager) == 0x20, "ThreadObject ABI");
static_assert(offsetof(ThreadObject, running) == 0x28, "ThreadObject ABI");
static_assert(sizeof(ThreadObject) == 0x30, "ThreadObject ABI");
static_assert(offsetof(ThreadManager, objects) == 0x08, "ThreadManager ABI");
static_assert(offsetof(ThreadManager, handles) == 0x10, "ThreadManager ABI");

ThreadManager::ThreadManager()
    : capacity(0), used(0), objects(NULL), handles(NULL), ownerTime(0) {}

ThreadManager::~ThreadManager() { Finalize(); }

bool ThreadManager::Init(u32 count) {
    if (capacity != 0) return false;
    if (count == 0) return false;
    objects = static_cast<ThreadObject**>(Memory_AllocateZero(sizeof(ThreadObject*) * count,
                                                              L"src\\Thread.cpp", 0x2E));
    handles = static_cast<HANDLE*>(Memory_AllocateZero(sizeof(HANDLE) * count,
                                                       L"src\\Thread.cpp", 0x33));
    if (!objects || !handles) {
        if (objects) Memory_Free(objects);
        if (handles) Memory_Free(handles);
        objects = NULL;
        handles = NULL;
        return false;
    }
    capacity = count;
    used = 0;
    return true;
}

bool ThreadManager::Start(ThreadObject* object, u32* threadId) {
    if (!object || !objects || used >= static_cast<LONG>(capacity)) return false;
    u32 slot = capacity;
    for (u32 i = 0; i < capacity; ++i) {
        if (objects[i] == NULL) { slot = i; break; }
    }
    if (slot == capacity) return false;
    object->manager = this;
    objects[slot] = object;
    unsigned tid = 0;
    HANDLE handle = reinterpret_cast<HANDLE>(_beginthreadex(NULL, 0, ThreadProc, object, 0, &tid));
    if (!handle) {
        objects[slot] = NULL;
        object->manager = NULL;
        return false;
    }
    handles[slot] = handle;
    InterlockedIncrement(&used);
    if (threadId) *threadId = tid;
    return true;
}

void ThreadManager::RefreshTimes() {
    u64 now = static_cast<u64>(GetTickCount());
    ownerTime = now;
    for (u32 i = 0; i < capacity; ++i) {
        if (objects[i]) objects[i]->cachedTime = now;
    }
}

bool ThreadManager::StopOne(u32 index, u32 waitMs, bool terminate, bool remove) {
    if (index >= capacity || !objects[index] || !handles[index]) return false;
    ThreadObject* object = objects[index];
    HANDLE handle = handles[index];
    object->Stop();
    DWORD result = WaitForSingleObject(handle, waitMs);
    if (result != WAIT_OBJECT_0 && terminate) TerminateThread(handle, 0);
    if (remove) {
        objects[index] = NULL;
        InterlockedDecrement(&used);
        // The original leaves the HANDLE in the array; Finalize releases the arrays.
    }
    return result == WAIT_OBJECT_0;
}

bool ThreadManager::Stop(ThreadObject* object, u32 waitMs, bool terminate, bool remove) {
    if (!object) return false;
    for (u32 i = 0; i < capacity; ++i) {
        if (objects[i] == object) return StopOne(i, waitMs, terminate, remove);
    }
    return false;
}

void ThreadManager::StopAll(u32 waitMs) {
    for (u32 i = 0; i < capacity; ++i)
        if (objects[i]) objects[i]->Stop();
    for (u32 i = 0; i < capacity; ++i) {
        if (!objects[i]) continue;
        ThreadObject* object = objects[i];
        object->Stop();
        HANDLE handle = handles[i];
        DWORD result = WaitForSingleObject(handle, waitMs);
        if (result != WAIT_OBJECT_0) TerminateThread(handle, 0);
        objects[i] = NULL;
        InterlockedDecrement(&used);
    }
}

void ThreadManager::Finalize() {
    StopAll(2000);
    if (objects) Memory_Free(objects);
    if (handles) Memory_Free(handles);
    objects = NULL;
    handles = NULL;
    capacity = 0;
    used = 0;
}

ThreadObject* Thread_Current() { return t_currentThread; }

} // namespace blood
