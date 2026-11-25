// BloodLib - timing wheel plus min-heap (original: Timer.h / Timer.cpp).
#pragma once
#include "Thread.h"

namespace blood {

struct TimerBucket {
    u32 index;
    Timer* head;
    Timer* tail;
};

class Timer : public EventObject {
public:
    Timer();
    virtual ~Timer();

    u32 EventClass() override { return 5; }
    bool HandleEvent(OVERLAPPED* overlapped, u32 bytes) override;
    virtual void OnTimer(); // +0x40: empty stub in every Achievement timer vtable

    u32 startTick;          // +0x18
    Timer* next;            // +0x20 toward bucket tail
    Timer* previous;        // +0x28 toward bucket head
    union {
        u32 dueTick;        // +0x30 when in heap
        i32 index;          // +0x30 wheel bucket, or -1
    };
    bool inHeap;            // +0x34

    u32 GetTime() const { return dueTick; }
    void SetTime(u32 value) { dueTick = value; }
};

class TimerQueue {
public:
    TimerQueue();
    virtual ~TimerQueue();

    bool Initialize(u32 resolutionMs, u32 bucketCount);
    // Detaches the expired bucket and returns its head. The caller transfers each timer to the
    // IOCP; the queue's reference becomes the completion key's reference.
    Timer* Update();
    bool Add(u32 delayMs, Timer* timer);
    bool Remove(Timer* timer);
    void Finalize();

    u32 resolutionMs;       // +0x08
    CRITICAL_SECTION lock;  // +0x10
    u32 bucketCount;        // +0x38
    TimerBucket* wheel;     // +0x40
    u32 cursor;             // +0x48
    Timer** heapBegin;      // +0x50
    Timer** heapEnd;        // +0x58
    Timer** heapCapacity;   // +0x60
    u8 reserved68[0x10];

private:
    TimerQueue(const TimerQueue&);
    TimerQueue& operator=(const TimerQueue&);
    void AppendToBucket(u32 index, Timer* timer);
    void RemoveFromBucket(u32 index, Timer* timer);
    void HeapSiftDown();
    void HeapClear();
    void FireBucket(TimerBucket* bucket);
};

TimerQueue* DaemonTimers();

} // namespace blood
