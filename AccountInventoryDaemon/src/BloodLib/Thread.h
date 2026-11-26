// BloodLib - thread objects and the original inline-array ThreadManager.
#pragma once
#include "BloodLib.h"
#include <process.h>

namespace blood {

class ThreadManager;

// Original vtable slots: +0x00 deleting destructor, +0x08 Release, +0x10 Name,
// +0x18 Run, +0x20 Stop. Thread is not a RefCntObject in this build.
class ThreadObject {
public:
    ThreadObject() : threadId(0), cachedTime(0), field18(0), manager(NULL), running(false) {}
    virtual ~ThreadObject() {}
    virtual void Release() { delete this; }
    virtual const wchar_t* Name() const { return L""; }
    virtual bool Run() = 0;
    virtual void Stop() = 0;

    u64 threadId;
    u64 cachedTime;
    i32 field18;
    ThreadManager* manager;
    bool running;
};

class ThreadManager {
public:
    ThreadManager();
    ~ThreadManager();

    bool Init(u32 count);
    bool Start(ThreadObject* object, u32* threadId = NULL);
    void RefreshTimes();
    bool Stop(ThreadObject* object, u32 waitMs, bool terminate, bool remove);
    void StopAll(u32 waitMs);
    void Finalize();

    u32 capacity;
    volatile LONG used;
    ThreadObject** objects;
    HANDLE* handles;
    u64 ownerTime;

private:
    ThreadManager(const ThreadManager&);
    ThreadManager& operator=(const ThreadManager&);
    bool StopOne(u32 index, u32 waitMs, bool terminate, bool remove);
};

ThreadObject* Thread_Current();

// Event objects are reference-counted IOCP completion keys. The worker calls EventClass() to
// classify a key and HandleEvent() at vtable +0x28 in the original.
class EventObject : public RefCntObject {
public:
    virtual u32 EventClass() = 0;                              // +0x20
    virtual bool HandleEvent(OVERLAPPED* overlapped, u32 bytes) = 0; // +0x28
    virtual int GetType() { return 0; }                        // +0x30
    virtual bool OnFailedEvent(OVERLAPPED* overlapped) { return false; } // +0x38
};

class Timer;

} // namespace blood
