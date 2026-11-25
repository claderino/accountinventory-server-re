// BloodLib - process memory managers and source-position allocation operators.
#include "MemoryManager.h"
#include "Memory.h"
#include "CrashHandler.h"
#include "Log.h"

#include <new>

namespace blood {
namespace {

u32 g_pageSize = 0;
u32 g_processorCount = 0;
u32 g_allocationGranularity = 0;
MemoryManager* g_staticManager = NULL;
MemoryManager* g_generalManager = NULL;

class HeapMemoryManager : public MemoryManager {
public:
    HeapMemoryManager() : heap_(NULL) {}
    ~HeapMemoryManager() override {
        if (heap_) HeapDestroy(heap_);
        heap_ = NULL;
    }

    bool Initialize(int section, void*) override {
        if (heap_) return false;
        heap_ = HeapCreate(0, 0, 0);
        if (!heap_) return false;
        if (section == 2) {
            ULONG enableTerminationOnCorruption = 2;
            HeapSetInformation(heap_, HeapEnableTerminationOnCorruption,
                               &enableTerminationOnCorruption, sizeof(enableTerminationOnCorruption));
        }
        return true;
    }
    void Reserved10() override {}
    void* Allocate(size_t size, const wchar_t*, int) override {
        return heap_ ? HeapAlloc(heap_, 0, size ? size : 1) : NULL;
    }
    void* AllocateZero(size_t size, const wchar_t*, int) override {
        return heap_ ? HeapAlloc(heap_, HEAP_ZERO_MEMORY, size ? size : 1) : NULL;
    }
    void* Reallocate(void* memory, size_t size, const wchar_t*, int) override {
        if (!heap_) return NULL;
        if (!memory) return HeapAlloc(heap_, 0, size ? size : 1);
        return HeapReAlloc(heap_, 0, memory, size ? size : 1);
    }
    void Reserved30() override {}
    void Free(void* memory) override {
        if (heap_ && memory) HeapFree(heap_, 0, memory);
    }
    void DeleteCriticalSection() override {}

private:
    HANDLE heap_;
};

} // namespace

MemoryManager* MemoryManager_General() { return g_generalManager; }
MemoryManager* MemoryManager_Static() { return g_staticManager; }

u32 Memory_PageSize() { return g_pageSize; }
u32 Memory_ProcessorCount() { return g_processorCount; }
u32 Memory_AllocationGranularity() { return g_allocationGranularity; }

MemoryManager* MemoryManager_Create(int section) {
    void* storage = malloc(sizeof(HeapMemoryManager));
    if (!storage) return NULL;
    MemoryManager* manager = new (storage) HeapMemoryManager();
    if (!manager->Initialize(section, NULL)) {
        manager->~MemoryManager();
        free(storage);
        return NULL;
    }
    return manager;
}

void* Memory_Allocate(size_t size, const wchar_t* file, int line) {
    MemoryManager* manager = g_generalManager;
    if (manager) return manager->Allocate(size, file, line);
    return malloc(size ? size : 1);
}

void* Memory_AllocateZero(size_t size, const wchar_t* file, int line) {
    MemoryManager* manager = g_generalManager;
    if (manager) return manager->AllocateZero(size, file, line);
    void* memory = calloc(1, size ? size : 1);
    return memory;
}

void* Memory_Reallocate(void* memory, size_t size, const wchar_t* file, int line) {
    MemoryManager* manager = g_generalManager;
    if (manager) return manager->Reallocate(memory, size, file, line);
    return realloc(memory, size ? size : 1);
}

void Memory_Free(void* memory) {
    if (!memory) return;
    MemoryManager* manager = g_generalManager;
    if (manager) manager->Free(memory);
    else free(memory);
}

bool Memory_Init() {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    g_pageSize = info.dwPageSize;
    g_processorCount = info.dwNumberOfProcessors;
    g_allocationGranularity = info.dwAllocationGranularity;

    CrashHandler_Init();
    if (!g_staticManager) g_staticManager = MemoryManager_CreateStaticPage();
    if (!g_generalManager) g_generalManager = MemoryManager_Create(2);
    return g_staticManager != NULL && g_generalManager != NULL;
}

void Memory_Shutdown() {
    if (g_generalManager) {
        g_generalManager->~MemoryManager();
        free(g_generalManager);
        g_generalManager = NULL;
    }
    if (g_staticManager) {
        g_staticManager->~MemoryManager();
        free(g_staticManager);
        g_staticManager = NULL;
    }
}

} // namespace blood

void* operator new(size_t size, const wchar_t* file, int line) {
    void* memory = blood::Memory_Allocate(size, file, line);
    if (!memory) throw std::bad_alloc();
    return memory;
}

void* operator new[](size_t size, const wchar_t* file, int line) {
    return ::operator new(size, file, line);
}
