// BloodLib - deferred release queue (original: DelayedRelease.h).
#pragma once
#include "BloodLib.h"

namespace blood {

class DelayedRelease {
public:
    DelayedRelease();
    ~DelayedRelease();

    bool Enqueue(u64 deadline, RefCntObject* ref1, RefCntObject* ref2 = NULL);
    void Flush(u64 nowMilliseconds);
    void FlushAll();
    void SetThresholds(i32 throttleMilliseconds, i32 graceMilliseconds) {
        throttle = throttleMilliseconds;
        grace = graceMilliseconds;
    }

    CRITICAL_SECTION lock;   // +0x00 (0x28 bytes)
    void* head;              // +0x28
    void* tail;              // +0x30
    volatile LONG count;     // +0x38
    u64 lastFlush;           // +0x40
    i32 throttle;            // +0x48
    i32 grace;               // +0x4C

private:
    DelayedRelease(const DelayedRelease&);
    DelayedRelease& operator=(const DelayedRelease&);
};

DelayedRelease& TheDelayedRelease();

} // namespace blood
