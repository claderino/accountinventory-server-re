// BloodLib - IOCP worker thread (original: source\IoWorkerThread.cpp).
#include "NetworkInternal.h"
#include "Log.h"
#include <time.h>

SOURCE_FILE("IoWorkerThread.cpp");

namespace blood {

static __declspec(thread) u32 t_workerId = 0;

IoWorkerThread::IoWorkerThread() : id_(0), failedHandler_(NULL) {}
IoWorkerThread::~IoWorkerThread() {}

bool IoWorkerThread::Init(u32 id, FailedEventObjectHandler failedHandler) {
    if (id < 1) {
        CRASH(40, L"== CRASH == ASSERT CRASH");
        return false;
    }
    id_ = id;
    failedHandler_ = failedHandler;
    return true;
}

void IoWorkerThread::Stop() {
    running = false;
    HANDLE iocp = NetIocp();
    if (iocp != NULL && iocp != INVALID_HANDLE_VALUE)
        PostQueuedCompletionStatus(iocp, 0, 0, NULL);   // wake the 1-s GQCS wait
}

// Dispatch one completion. The worker's temporary AddRef/Release pairs with the AddRef done here;
// the per-operation reference added by the poster is consumed by CompleteOperationRelease.
static void DispatchCompletion(EventObject* key, OVERLAPPED* ov, u32 bytes, bool failed,
                               FailedEventObjectHandler failedHandler) {
    if (key == NULL) return;
    RefCnt_AddRef(key, kSourceFile, 0);
    bool ok = failed ? key->OnFailedEvent(ov) : key->HandleEvent(ov, bytes);
    if (!ok) {
        // Hard-coded in this build's worker: Listener class 2 and Session class 3.
        if (key->EventClass() == kListenerEventClass) key->OnFailedEvent(ov);
        if (key->EventClass() == kSessionEventClass) key->OnFailedEvent(ov);
        if (failedHandler != NULL) failedHandler(key, ov);
    }
    RefCnt_Release(key, kSourceFile, 0);
    CompleteOperationRelease(key);   // drops the reference taken before posting the I/O
}

bool IoWorkerThread::Run() {
    t_workerId = id_;
    srand(id_ + (unsigned)time(NULL));
    HANDLE iocp = NetIocp();
    for (;;) {
        if (!running) {
            LOG(1, 89, L"[worker], run flag has off; stop, id: %d", (int)id_);
            LOG(1, 121, L"[worker], stopped, id: %d", (int)id_);
            return true;
        }
        DWORD bytes = 0;
        ULONG_PTR keyValue = 0;
        OVERLAPPED* ov = NULL;
        BOOL ok = GetQueuedCompletionStatus(iocp, &bytes, &keyValue, &ov, 1000);
        EventObject* key = reinterpret_cast<EventObject*>(keyValue);
        if (!ok) {
            if (key == NULL || ov == NULL) {
                DWORD err = GetLastError();
                if (err != WAIT_TIMEOUT)
                    LOG(1, 130, L"[worker], IOCP error, error: %d", (int)err);
            } else {
                DispatchCompletion(key, ov, bytes, true, failedHandler_);
            }
            continue;
        }
        if (key == NULL) continue;      // wake-up posted by Stop()
        if (key->EventClass() == 0 && ov == NULL) continue;  // unused termination key
        DispatchCompletion(key, ov, bytes, false, failedHandler_);
    }
}

} // namespace blood
