// BloodLib - timing wheel plus min-heap (original: Timer.cpp).
#include "Daemon.h"
#include "Timer.h"

#include <string.h>

SOURCE_FILE("Timer.cpp");

namespace blood {
namespace {

u32 CurrentTick(const TimerQueue* queue) {
    Daemon* daemon = Daemon::Instance();
    if (daemon) return static_cast<u32>(daemon->LoopTick());
    return static_cast<u32>(DaemonMillis() / (queue->resolutionMs ? queue->resolutionMs : 1));
}

} // namespace

static_assert(offsetof(Timer, startTick) == 0x18, "Timer ABI");
static_assert(offsetof(Timer, next) == 0x20, "Timer ABI");
static_assert(offsetof(Timer, previous) == 0x28, "Timer ABI");
static_assert(offsetof(Timer, index) == 0x30, "Timer ABI");
static_assert(offsetof(Timer, inHeap) == 0x34, "Timer ABI");
static_assert(sizeof(Timer) == 0x38, "Timer ABI");
static_assert(offsetof(TimerQueue, resolutionMs) == 0x08, "TimerQueue ABI");
static_assert(offsetof(TimerQueue, lock) == 0x10, "TimerQueue ABI");
static_assert(offsetof(TimerQueue, bucketCount) == 0x38, "TimerQueue ABI");
static_assert(offsetof(TimerQueue, wheel) == 0x40, "TimerQueue ABI");
static_assert(offsetof(TimerQueue, cursor) == 0x48, "TimerQueue ABI");
static_assert(offsetof(TimerQueue, heapBegin) == 0x50, "TimerQueue ABI");
static_assert(offsetof(TimerQueue, heapEnd) == 0x58, "TimerQueue ABI");
static_assert(offsetof(TimerQueue, heapCapacity) == 0x60, "TimerQueue ABI");
static_assert(sizeof(TimerQueue) == 0x78, "TimerQueue ABI");
static_assert(sizeof(TimerBucket) == 0x18, "TimerBucket ABI");

Timer::Timer()
    : startTick(0), next(NULL), previous(NULL), inHeap(false) {
    index = -1;
}

Timer::~Timer() {}

bool Timer::HandleEvent(OVERLAPPED*, u32) {
    OnTimer();
    return true;
}

void Timer::OnTimer() {}

TimerQueue::TimerQueue()
    : resolutionMs(0), bucketCount(0), wheel(NULL), cursor(0), heapBegin(NULL),
      heapEnd(NULL), heapCapacity(NULL) {
    memset(reserved68, 0, sizeof(reserved68));
    InitializeCriticalSectionAndSpinCount(&lock, 4000);
}

TimerQueue::~TimerQueue() {
    Finalize();
    if (wheel) Memory_Free(wheel);
    if (heapBegin) Memory_Free(heapBegin);
    wheel = NULL;
    heapBegin = heapEnd = heapCapacity = NULL;
    DeleteCriticalSection(&lock);
}

bool TimerQueue::Initialize(u32 newResolutionMs, u32 newBucketCount) {
    if (wheel) {
        CRASH(0x29, L"== CRASH == ASSERT CRASH");
        return false;
    }
    resolutionMs = newResolutionMs ? newResolutionMs : 1;
    bucketCount = newBucketCount ? newBucketCount : 1;
    wheel = static_cast<TimerBucket*>(Memory_AllocateZero(sizeof(TimerBucket) * bucketCount,
                                                          L"src\\Timer.cpp", 0x2E));
    if (!wheel) return false;
    for (u32 i = 0; i < bucketCount; ++i) {
        wheel[i].index = i;
        wheel[i].head = wheel[i].tail = NULL;
    }
    cursor = 0;
    return true;
}

void TimerQueue::AppendToBucket(u32 index, Timer* timer) {
    TimerBucket& bucket = wheel[index];
    timer->next = NULL;
    timer->previous = bucket.tail;
    if (bucket.tail) bucket.tail->next = timer;
    else bucket.head = timer;
    bucket.tail = timer;
}

void TimerQueue::RemoveFromBucket(u32 index, Timer* timer) {
    TimerBucket& bucket = wheel[index];
    if (timer->next) timer->next->previous = timer->previous;
    else bucket.tail = timer->previous;
    if (timer->previous) timer->previous->next = timer->next;
    else bucket.head = timer->next;
    timer->next = timer->previous = NULL;
    timer->index = -1;
    RefCnt_Release(timer, L"src\\Timer.cpp", 0x130);
}

void TimerQueue::FireBucket(TimerBucket* bucket) {
    while (bucket->head) {
        Timer* timer = bucket->head;
        bucket->head = timer->next;
        if (!bucket->head) bucket->tail = NULL;
        timer->next = timer->previous = NULL;
        timer->index = -1;
        timer->OnTimer();
        RefCnt_Release(timer, L"src\\Timer.cpp", 0x138);
    }
}

void TimerQueue::HeapSiftDown() {
    if (heapBegin == heapEnd) return;
    Timer* last = *--heapEnd;
    if (heapBegin == heapEnd) return;
    size_t index = 0;
    while (true) {
        size_t child = index * 2 + 1;
        if (child >= static_cast<size_t>(heapEnd - heapBegin)) break;
        if (child + 1 < static_cast<size_t>(heapEnd - heapBegin) &&
            static_cast<i32>(heapBegin[child + 1]->dueTick) < static_cast<i32>(heapBegin[child]->dueTick))
            ++child;
        if (static_cast<i32>(last->dueTick) <= static_cast<i32>(heapBegin[child]->dueTick)) break;
        heapBegin[index] = heapBegin[child];
        index = child;
    }
    heapBegin[index] = last;
}

void TimerQueue::HeapClear() {
    for (Timer** entry = heapBegin; entry != heapEnd; ++entry) {
        (*entry)->inHeap = false;
        (*entry)->index = -1;
        RefCnt_Release(*entry, L"src\\Timer.cpp", 0x153);
    }
    heapEnd = heapBegin;
}

bool TimerQueue::Add(u32 delayMs, Timer* timer) {
    if (!timer || timer->index >= 0) {
        CRASH(0x9B, L"== CRASH == ASSERT CRASH");
        return false;
    }
    EnterCriticalSection(&lock);
    u32 ticks = delayMs / resolutionMs;
    if (ticks < bucketCount) {
        u32 index = (cursor + ticks) % bucketCount;
        AppendToBucket(index, timer);
        timer->startTick = CurrentTick(this);
        timer->index = static_cast<i32>(index);
        timer->inHeap = false;
        RefCnt_AddRef(timer, L"src\\Timer.cpp", 0x120);
    } else {
        // HeapPush sets start/due relative to the current daemon tick.
        u32 tick = CurrentTick(this);
        timer->startTick = tick;
        timer->dueTick = tick + ticks;
        timer->inHeap = true;
        RefCnt_AddRef(timer, L"src\\Timer.cpp", 0x146);
        if (heapEnd == heapCapacity) {
            size_t count = static_cast<size_t>(heapEnd - heapBegin);
            size_t capacity = count ? count * 2 : 16;
            Timer** grown = static_cast<Timer**>(Memory_Reallocate(heapBegin, capacity * sizeof(Timer*),
                                                                   L"<server>\\bloodlib\\include\\StlMemory.h", 0x40));
            if (!grown) { LeaveCriticalSection(&lock); return false; }
            heapBegin = grown;
            heapEnd = heapBegin + count;
            heapCapacity = heapBegin + capacity;
        }
        size_t index = static_cast<size_t>(heapEnd - heapBegin);
        *heapEnd++ = timer;
        while (index > 0) {
            size_t parent = (index - 1) / 2;
            if (static_cast<i32>(heapBegin[parent]->dueTick) <= static_cast<i32>(timer->dueTick)) break;
            heapBegin[index] = heapBegin[parent];
            index = parent;
        }
        heapBegin[index] = timer;
    }
    LeaveCriticalSection(&lock);
    return true;
}

bool TimerQueue::Remove(Timer* timer) {
    if (!timer) {
        CRASH(0xB7, L"== CRASH == ASSERT CRASH");
        return false;
    }
    EnterCriticalSection(&lock);
    i32 index = timer->index;
    if (index < 0) {
        LeaveCriticalSection(&lock);
        LOG(1, 0xC4, L"TimerQueue::Remove() Error, timer index = %d", index);
        return false;
    }
    if (static_cast<u32>(index) < bucketCount) {
        RemoveFromBucket(static_cast<u32>(index), timer);
    } else {
        // Heap entries are lazily removed; Update releases the stale entry when it reaches the root.
        timer->inHeap = false;
    }
    LeaveCriticalSection(&lock);
    return true;
}

Timer* TimerQueue::Update() {
    EnterCriticalSection(&lock);
    cursor = (cursor + 1) % bucketCount;
    TimerBucket& bucket = wheel[cursor];
    Timer* fired = bucket.head;
    for (Timer* timer = fired; timer; timer = timer->next) timer->index = -1;
    bucket.head = bucket.tail = NULL;

    u32 tick = CurrentTick(this);
    while (heapBegin != heapEnd &&
           static_cast<i32>(heapBegin[0]->dueTick) < static_cast<i32>(tick + bucketCount)) {
        Timer* timer = heapBegin[0];
        HeapSiftDown();
        if (timer->inHeap) {
            u32 index = (bucketCount + cursor - 1) % bucketCount;
            timer->inHeap = false;
            AppendToBucket(index, timer);
            timer->index = static_cast<i32>(index);
            RefCnt_AddRef(timer, L"src\\Timer.cpp", 0x120);
        }
        // Consume the heap reference. If inHeap was false this was a lazily removed stale entry.
        RefCnt_Release(timer, L"src\\Timer.cpp", 0x72);
    }
    LeaveCriticalSection(&lock);
    return fired;
}

void TimerQueue::Finalize() {
    EnterCriticalSection(&lock);
    HeapClear();
    for (u32 i = 0; i < bucketCount; ++i) FireBucket(&wheel[i]);
    LeaveCriticalSection(&lock);
}

} // namespace blood
