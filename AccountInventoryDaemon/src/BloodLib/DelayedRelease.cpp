// BloodLib - deferred reference release queue (original: DelayedRelease.cpp).
#include "DelayedRelease.h"
#include "Daemon.h"

#include <new>

SOURCE_FILE("DelayedRelease.cpp");

namespace blood {

// Network/Channel owners provide these integration hooks.
void Channel_UnbindIfEqual(RefCntObject* ref1, RefCntObject* ref2);
HANDLE NetIocp();

namespace {

struct DelayedNode {
    DelayedNode* next;
    u64 deadline;
    void (*callback)(RefCntObject*, RefCntObject*);
    RefCntObject* ref1;
    RefCntObject* ref2;
    RefCntObject* ref3;
};

// Network/Channel owner supplies the fixed original callback (sub_25CC0).
void FixedCallback(RefCntObject* ref1, RefCntObject* ref2) {
    if (ref1) Channel_UnbindIfEqual(ref1, ref2);
}

void DrainNodes(DelayedNode* node) {
    while (node) {
        DelayedNode* next = node->next;
        if (node->callback) node->callback(node->ref1, node->ref2);
        if (node->ref1) RefCnt_Release(node->ref1, L"src\\DelayedRelease.cpp", 0x83);
        if (node->ref2) RefCnt_Release(node->ref2, L"src\\DelayedRelease.cpp", 0x84);
        if (node->ref3) RefCnt_Release(node->ref3, L"src\\DelayedRelease.cpp", 0x85);
        Memory_Free(node);
        node = next;
    }
}

class DelayedReleaseJob : public EventObject {
public:
    explicit DelayedReleaseJob(void* nodes) : nodes_(nodes) {
        tag = 0x0A;
        refCount = 1;
        history = NULL;
    }
    u32 EventClass() override { return 10; }
    bool HandleEvent(OVERLAPPED*, u32) override {
        // The IO worker releases the completion key after this callback (DispatchCompletion
        // / CompleteOperationRelease); do not release 'this' here or the job is freed twice.
        DrainNodes(static_cast<DelayedNode*>(nodes_));
        return true;
    }
    void* nodes() const { return nodes_; }
private:
    void* nodes_;
};

} // namespace

static_assert(offsetof(DelayedRelease, head) == 0x28, "DelayedRelease ABI");
static_assert(offsetof(DelayedRelease, tail) == 0x30, "DelayedRelease ABI");
static_assert(offsetof(DelayedRelease, count) == 0x38, "DelayedRelease ABI");
static_assert(offsetof(DelayedRelease, lastFlush) == 0x40, "DelayedRelease ABI");
static_assert(offsetof(DelayedRelease, throttle) == 0x48, "DelayedRelease ABI");
static_assert(offsetof(DelayedRelease, grace) == 0x4C, "DelayedRelease ABI");
static_assert(sizeof(DelayedRelease) == 0x50, "DelayedRelease ABI");
static_assert(sizeof(DelayedNode) == 0x30, "DelayedNode ABI");
static_assert(sizeof(DelayedReleaseJob) == 0x20, "DelayedReleaseJob ABI");

DelayedRelease::DelayedRelease()
    : head(NULL), tail(NULL), count(0), lastFlush(0), throttle(1000), grace(10000) {
    InitializeCriticalSectionAndSpinCount(&lock, 4000);
}

DelayedRelease::~DelayedRelease() {
    FlushAll();
    DeleteCriticalSection(&lock);
}

bool DelayedRelease::Enqueue(u64 deadline, RefCntObject* ref1, RefCntObject* ref2) {
    DelayedNode* node = static_cast<DelayedNode*>(Memory_Allocate(sizeof(DelayedNode),
                                                                   L"src\\DelayedRelease.cpp", 0x30));
    if (!node) {
        LogWrite(0, L"src\\DelayedRelease.cpp", 0x29,
                 L"[delayed release], allocation failed, callback:%p, ref1:%p, ref2:%p, ref3:%p",
                 reinterpret_cast<void*>(&FixedCallback), ref1, ref2, NULL);
        return false;
    }
    node->next = NULL;
    node->deadline = deadline;
    node->callback = &FixedCallback;
    node->ref1 = ref1;
    node->ref2 = ref2;
    node->ref3 = NULL;
    if (ref1) RefCnt_AddRef(ref1, L"src\\DelayedRelease.cpp", 0x33);
    if (ref2) RefCnt_AddRef(ref2, L"src\\DelayedRelease.cpp", 0x34);
    EnterCriticalSection(&lock);
    if (!head) head = node;
    if (tail) static_cast<DelayedNode*>(tail)->next = node;
    tail = node;
    InterlockedIncrement(&count);
    LeaveCriticalSection(&lock);
    return true;
}

void DelayedRelease::Flush(u64 nowMilliseconds) {
    DelayedNode* detachedHead = NULL;
    DelayedNode* detachedTail = NULL;
    EnterCriticalSection(&lock);
    if (lastFlush && static_cast<i64>(nowMilliseconds - lastFlush) < throttle) {
        LeaveCriticalSection(&lock);
        return;
    }
    int detached = 0;
    while (head && static_cast<i64>(nowMilliseconds - static_cast<DelayedNode*>(head)->deadline) > grace &&
           detached < 0x7FFFFFFF) {
        DelayedNode* node = static_cast<DelayedNode*>(head);
        head = node->next;
        node->next = NULL;
        if (detachedTail) detachedTail->next = node;
        else detachedHead = node;
        detachedTail = node;
        InterlockedDecrement(&count);
        ++detached;
    }
    if (!head) tail = NULL;
    lastFlush = nowMilliseconds;
    LeaveCriticalSection(&lock);
    if (!detachedHead) return;

    DelayedReleaseJob* storage = static_cast<DelayedReleaseJob*>(
        Memory_Allocate(sizeof(DelayedReleaseJob), L"src\\DelayedRelease.cpp", 0x6E));
    if (!storage) {
        DrainNodes(detachedHead);
        return;
    }
    DelayedReleaseJob* job = new (storage) DelayedReleaseJob(detachedHead);
    HANDLE iocp = NetIocp();
    if (!iocp || iocp == INVALID_HANDLE_VALUE ||
        !PostQueuedCompletionStatus(iocp, 0, reinterpret_cast<ULONG_PTR>(job), NULL)) {
        if (job) RefCnt_Release(job, L"src\\DelayedRelease.cpp", 0x73);
        DrainNodes(detachedHead);
    }
}

void DelayedRelease::FlushAll() {
    EnterCriticalSection(&lock);
    DelayedNode* list = static_cast<DelayedNode*>(head);
    head = tail = NULL;
    count = 0;
    LeaveCriticalSection(&lock);
    DrainNodes(list);
}

void RefCnt_ReleaseDelayedObject(RefCntObject* object, const wchar_t* file, int line) {
    // The original vtable slot +0x18 is destructor-then-free, not a deferred release.
    RefCnt_Release(object, file, line);
}

DelayedRelease& TheDelayedRelease() {
    static DelayedRelease queue;
    return queue;
}

} // namespace blood
