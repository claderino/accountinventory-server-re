// BloodLib - listening port and its session pool (original: source\Port.cpp).
#include "NetworkInternal.h"
#include <vector>
#include <new>

SOURCE_FILE("Port.cpp");

namespace blood {

// SessionPool (original: 0xD0-byte object, vtable 94268). The original preallocates 100% of the
// 10000 sessions from 64-KiB VirtualAlloc chunks (element size 0x2000) and never VirtualFrees the
// chunks. We keep the element size / preallocation and free the arenas explicitly (SAFETY).
class SessionPool {
public:
    explicit SessionPool(u32 portId)
        : portId_(portId), initialized_(false), protectedDeque_(false),
          elementSize_(0x2000), chunkSize_(0), arena_(NULL), arenaEnd_(NULL), nextSlot_(0) {
        InitializeCriticalSectionAndSpinCount(&freeLock_, 4000);
        InitializeCriticalSectionAndSpinCount(&arenaLock_, 4000);
    }
    ~SessionPool() {
        // recycled_ entries are raw arena slots: Free() already destroyed any Session
        // that was actually constructed, and untouched slots have no vptr at all.
        // Explicitly calling ~Session() here dereferences a null vtable on shutdown.
        for (size_t i = 0; i < arenas_.size(); ++i) VirtualFree(arenas_[i], 0, MEM_RELEASE);
        arenas_.clear();
        DeleteCriticalSection(&arenaLock_);
        DeleteCriticalSection(&freeLock_);
    }

    bool Initialize(u32 max, u32 percent) {
        if (initialized_) {
            LOG(0, 0x36, L"== CRASH == [port], session pool is already initialized, id: %d", (int)portId_);
            return false;
        }
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        chunkSize_ = si.dwAllocationGranularity;   // 64 KiB on the target
        if (chunkSize_ < elementSize_) chunkSize_ = elementSize_;
        u32 total = max * percent / 100;
        for (u32 i = 0; i < total; ++i) {
            void* mem = AllocateElement();
            if (mem == NULL) {
                LOG(0, 0x59, L"[port], cannot make session, id: %d", (int)portId_);
                return false;
            }
            recycled_.push_back(static_cast<Session*>(mem));   // not constructed yet
        }
        initialized_ = true;
        return true;
    }

    Session* Alloc(Port* port, PacketBufferHandler* prototype) {
        EnterCriticalSection(&freeLock_);
        Session* s = NULL;
        if (!recycled_.empty()) {
            s = recycled_.back();
            recycled_.pop_back();
            new (s) Session(port, portId_, prototype);
        }
        LeaveCriticalSection(&freeLock_);
        if (s == NULL) {
            void* mem = AllocateElement();
            if (mem) s = new (mem) Session(port, portId_, prototype);
        }
        return s;
    }

    void Free(Session* s) {
        if (s == NULL) return;
        s->~Session();
        EnterCriticalSection(&freeLock_);
        recycled_.push_back(s);
        LeaveCriticalSection(&freeLock_);
    }

    bool initialized() const { return initialized_; }

private:
    SessionPool(const SessionPool&);
    SessionPool& operator=(const SessionPool&);

    void* AllocateElement() {
        EnterCriticalSection(&arenaLock_);
        if (arena_ == NULL || (size_t)(arenaEnd_ - arena_) < elementSize_) {
            void* chunk = VirtualAlloc(NULL, chunkSize_, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
            if (chunk == NULL) {
                LeaveCriticalSection(&arenaLock_);
                return NULL;
            }
            arenas_.push_back(static_cast<unsigned char*>(chunk));
            arena_ = static_cast<unsigned char*>(chunk);
            arenaEnd_ = arena_ + chunkSize_;
        }
        void* result = arena_;
        arena_ += elementSize_;
        LeaveCriticalSection(&arenaLock_);
        return result;
    }

    u32 portId_;
    bool initialized_;
    bool protectedDeque_;
    u32 elementSize_;
    u32 chunkSize_;
    unsigned char* arena_;
    unsigned char* arenaEnd_;
    u32 nextSlot_;
    CRITICAL_SECTION freeLock_;
    CRITICAL_SECTION arenaLock_;
    std::vector<unsigned char*> arenas_;
    std::vector<Session*> recycled_;
};

Port::Port()
    : id_(0), listenPort_(0), iocp_(INVALID_HANDLE_VALUE), implicitRefs(0), opened(false),
      initialized(false), noDelayDisabled(false), activeFlag(0), capacity_(0),
      poolInitialized(false), slots(NULL), pool(NULL), listener_(NULL),
      handler_(NULL), prototype_(NULL), nextSlot_(0), liveCount_(0) {
    InitializeCriticalSectionAndSpinCount(&lock, 4000);
}

Port::~Port() {
    Clear();
    DeleteCriticalSection(&lock);
}

bool Port::Initialize(u32 id, u16 listenPort, u32 capacity, HANDLE iocp,
                      SessionHandler* mgr, PacketBufferHandler* prototype) {
    if (initialized || capacity_ != 0) {
        CRASH(0x36, L"== CRASH == ASSERT CRASH", (int)id_);
    }
    if (capacity == 0 || prototype == NULL) return false;
    id_ = id;
    listenPort_ = listenPort;
    iocp_ = iocp;
    capacity_ = capacity;
    handler_ = mgr;             // embedded, NOT AddRef'd in this build
    prototype_ = prototype;
    noDelayDisabled = true;    // TCP_NODELAY is never enabled in this image
    pool = new SessionPool(id);
    slots = new Session*[capacity_];
    memset(slots, 0, sizeof(Session*) * capacity_);
    if (!pool->Initialize(capacity_, 100)) return false;
    poolInitialized = true;
    if (listenPort != 0) {
        Listener* l = new Listener(this);
        if (!l->Initialize(iocp, listenPort)) {
            delete l;
            return false;
        }
        listener_ = l;         // the Listener holds its own socket-lifetime reference
    }
    initialized = true;
    activeFlag = 1;
    return true;
}

void Port::Clear() {
    opened = false;
    if (listener_ != NULL) {
        listener_->CloseListenSocket();
        listener_->Release();
        listener_ = NULL;
    }
    if (slots != NULL) {
        for (u32 i = 0; i < capacity_; ++i) {
            Session* s = slots[i];
            if (s == NULL) continue;
            slots[i] = NULL;
            s->Release();      // last reference may recycle the session into the pool
        }
    }
    if (pool != NULL) {
        delete pool;           // destroys any still-recycled sessions and frees the arenas
        pool = NULL;
        poolInitialized = false;
    }
    delete[] slots;
    slots = NULL;
    liveCount_ = 0;
    nextSlot_ = 0;
    capacity_ = 0;
    initialized = false;
    activeFlag = 0;
    // The original calls the prototype destructor but only nulls the manager pointer.
    prototype_ = NULL;
}

bool Port::Start() { opened = true; return true; }
bool Port::Stop() { opened = false; return true; }

Session* Port::AllocSlot(u64* outId) {
    if (liveCount_ >= capacity_) {
        LOG(0, 0x259, L"Session List if full, %d", (int)capacity_);
        return NULL;
    }
    Session* s = pool != NULL ? pool->Alloc(this, prototype_) : NULL;
    if (s == NULL) {
        LOG(0, 0x59, L"[port], cannot make session, id: %d", (int)id_);
        return NULL;
    }
    u64 id = 0;
    bool found = false;
    EnterCriticalSection(&lock);
    for (u32 n = 0; n < capacity_; ++n) {
        u32 slot = nextSlot_;
        nextSlot_ = (slot + 1) % capacity_;
        if (slots[slot] == NULL) {
            slots[slot] = s;
            ++liveCount_;
            id = ((u64)id_ << 32) | slot;   // slot 0 is a valid slot
            found = true;
            break;
        }
    }
    LeaveCriticalSection(&lock);
    if (!found) {
        // Only reached when the wrap-around found no slot; treat as failure (slot 0 is valid only
        // when it was actually populated, which the loop above guarantees with the id check).
        s->Release();
        LOG(0, 0x283, L"[port], Cannot find empty slot in session list, id: %d", (int)id_);
        return NULL;
    }
    if (outId) *outId = id;
    LOG(1, 0x288, L"[session], created, port-id:%u, session-id:%I64x, session:%p", id_, id, s);
    return s;
}

Session* Port::CreateSession(u64* id, SOCKET accepted, in_addr ip, u16 port, u32 option) {
    u64 sessionId = 0;
    Session* s = AllocSlot(&sessionId);
    if (s == NULL) return NULL;
    s->AddRef();                                   // temporary for the caller
    if (accepted != INVALID_SOCKET) s->SetSocket(accepted);
    u64 acceptedU64 = (u64)(ULONG_PTR)accepted;
    if (!s->Initialize(this, sessionId, option) ||
        !s->OnAccept(this, &acceptedU64, sessionId, ip, port, option)) {
        s->Release();
        RemoveBySession(s);
        return NULL;
    }
    if (id) *id = sessionId;
    return s;                                      // caller (Listener) releases this temporary
}

bool Port::RemoveBySession(Session* s) {
    if (s == NULL) {
        CRASH(0x1CC, L"== CRASH == ASSERT CRASH");
        return false;
    }
    u32 slot = (u32)s->id;
    if (slot >= capacity_) {
        CRASH(0x1CF, L"== CRASH == ASSERT CRASH");
        return false;
    }
    bool removed = false;
    EnterCriticalSection(&lock);
    if (slots[slot] == s) {
        slots[slot] = NULL;
        if (liveCount_ > 0) --liveCount_;
        s->Release();                              // drop the slot reference
        removed = true;
    }
    LeaveCriticalSection(&lock);
    if (removed) {
        LOG(1, 0x1DC, L"[session], removed, port-id:%u, session-id:%I64x, session:%p",
            id_, s->id, s);
    }
    return removed;
}

void Port::RemoveSession(Session* s) { RemoveBySession(s); }

void Port::RecycleSession(Session* s) {
    if (pool != NULL) pool->Free(s);
}

void Port::ReleaseSessionPool() {
    if (pool == NULL) return;
    poolInitialized = false;
    delete pool;
    pool = NULL;
}

void Port::BeginTeardownSessions() {
    if (slots == NULL) return;
    for (u32 i = 0; i < capacity_; ++i) {
        if (slots[i] != NULL) slots[i]->BeginTeardown();
    }
}

} // namespace blood
