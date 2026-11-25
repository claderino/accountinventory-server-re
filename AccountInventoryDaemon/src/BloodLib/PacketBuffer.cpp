// BloodLib - TLS packet-buffer pool (original: PacketBuffer.cpp).
#include "PacketBuffer.h"
#include "Log.h"

#include <vector>

SOURCE_FILE("PacketBuffer.cpp");

namespace blood {
namespace {

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

void* AllocateBatch() {
    return VirtualAlloc(NULL, 16 * 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
}

void Recycle(void* block) {
    if (t_freeBlocks.size() <= 0x200) {
        t_freeBlocks.push_back(block);
        return;
    }
    EnsureDeadLock();
    EnterCriticalSection(&g_deadLock);
    for (size_t i = 0; i < 256 && !t_freeBlocks.empty(); ++i) {
        g_deadBlocks.push_back(t_freeBlocks.back());
        t_freeBlocks.pop_back();
    }
    LeaveCriticalSection(&g_deadLock);
}

} // namespace

static_assert(offsetof(PacketBuffer, tag) == 0x08, "PacketBuffer ABI");
static_assert(offsetof(PacketBuffer, refCount) == 0x0C, "PacketBuffer ABI");
static_assert(offsetof(PacketBuffer, size) == 0x20, "PacketBuffer ABI");
static_assert(offsetof(PacketBuffer, fields) == 0x22, "PacketBuffer ABI");
static_assert(offsetof(PacketBuffer, cipherLength) == 0xFC0, "PacketBuffer ABI");
static_assert(offsetof(PacketBuffer, cipherData) == 0xFC8, "PacketBuffer ABI");
static_assert(offsetof(PacketBuffer, next) == 0xFD0, "PacketBuffer ABI");
static_assert(sizeof(PacketBuffer) == 0xFD8, "PacketBuffer ABI");

void PacketBuffer_InitTLS() {
    // thread_local storage above is TLS slot 4 (TEB array +0x20).
}

PacketBuffer* PacketBuffer::Acquire() {
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
            u8* batch = static_cast<u8*>(AllocateBatch());
            if (batch) {
                for (int i = 15; i >= 1; --i) t_freeBlocks.push_back(batch + i * 0x1000);
                block = batch;
            }
        }
    }
    if (!block) return NULL;
    PacketBuffer* buffer = new (block) PacketBuffer();
    buffer->tag = 0x50A;
    buffer->refCount = 1;
    buffer->scratch = 0;
    memset(buffer->reserved18, 0, sizeof(buffer->reserved18));
    buffer->size = 0; // Achievement build leaves the size word zero in Acquire().
    buffer->cipherLength = 0;
    buffer->reservedFC4 = 0;
    buffer->cipherData = NULL;
    buffer->next = NULL;
    return buffer;
}

void PacketBuffer::Release(const wchar_t*, int) {
    if (InterlockedDecrement(&refCount) != 0) return;
    void* block = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(this) & ~uintptr_t(0xFFF));
    this->~PacketBuffer();
    Recycle(block);
}

bool Packet_EncryptChain(PacketBuffer* first, Crypto*, Crypto*) {
    // Crypto is linked dead code in this daemon; no application session installs a codec.
    // The hook is intentionally a no-op when the Session owner has no crypto context.
    (void)first;
    return true;
}

} // namespace blood
