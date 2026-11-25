// BloodLib - page-pool and static-page memory managers (original: MemoryManager.cpp).
#include "MemoryManager.h"
#include "Memory.h"
#include "Log.h"

#include <new>

SOURCE_FILE("MemoryManager.cpp");

namespace blood {

PagePoolManager::PagePoolManager()
    : blockSize_(0), chunkSize_(0), current_(NULL), end_(NULL) {
    InitializeCriticalSectionAndSpinCount(&lock_, 4000);
}

PagePoolManager::~PagePoolManager() { DeleteCriticalSection(&lock_); }

bool PagePoolManager::Initialize(size_t blockSize, size_t maximumChunkSize) {
    if (blockSize_ != 0) {
        LOG(0, 0x10a, L"== CRASH == [memory], Page Pool Memory Manager already initialized");
        Assert_FailW(kSourceFile, 0x10a,
                     L"== CRASH == [memory], Page Pool Memory Manager already initialized");
        return false;
    }
    size_t power = 0x400;
    if (blockSize < power || maximumChunkSize < blockSize) {
        LOG(0, 0x10f,
            L"== CRASH == [memory], Page Pool Memory Manager initialization failed, size: %d",
            static_cast<int>(blockSize));
        Assert_FailW(kSourceFile, 0x10f,
                     L"== CRASH == [memory], Page Pool Memory Manager initialization failed, size: %d",
                     static_cast<int>(blockSize));
        return false;
    }
    while (power < maximumChunkSize && power < blockSize) power <<= 1;
    if (power != blockSize) {
        LOG(0, 0x117,
            L"== CRASH == [memory], Page Pool Memory Manager allocation size is invalid, size: %d",
            static_cast<int>(blockSize));
        Assert_FailW(kSourceFile, 0x117,
                     L"== CRASH == [memory], Page Pool Memory Manager allocation size is invalid, size: %d",
                     static_cast<int>(blockSize));
        return false;
    }
    blockSize_ = blockSize;
    chunkSize_ = power;
    return true;
}

void* PagePoolManager::Allocate() {
    EnterCriticalSection(&lock_);
    if (end_ - current_ < static_cast<ptrdiff_t>(blockSize_)) {
        u8* chunk = static_cast<u8*>(VirtualAlloc(NULL, chunkSize_,
                                                  MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE));
        current_ = chunk;
        end_ = chunk ? chunk + chunkSize_ : current_;
    }
    void* result = current_;
    if (result) current_ += blockSize_;
    LeaveCriticalSection(&lock_);
    return result;
}

namespace {

class StaticPageMemoryManager : public MemoryManager {
public:
    StaticPageMemoryManager() : page_(NULL), current_(NULL), end_(NULL) {}
    ~StaticPageMemoryManager() override {
        if (page_) VirtualFree(page_, 0, MEM_RELEASE);
    }

    bool Initialize(int section, void*) override {
        (void)section;
        if (page_) return false;
        page_ = static_cast<u8*>(VirtualAlloc(NULL, 0x800, MEM_COMMIT | MEM_RESERVE,
                                              PAGE_READWRITE));
        if (!page_) return false;
        current_ = page_ + 0x250;
        end_ = page_ + 0x800;
        return true;
    }
    void Reserved10() override {}
    void* Allocate(size_t size, const wchar_t* file, int line) override {
        if (!current_) {
            LOG(0, 0x160, L"[memory], static page memory not initialized");
            return NULL;
        }
        size_t aligned = (size + 15) & ~size_t(15);
        if (aligned <= static_cast<size_t>(end_ - current_)) {
            void* result = current_;
            current_ += aligned;
            return result;
        }
        MemoryManager* owner = MemoryManager_General();
        if (!owner) {
            LOG(0, 0x16a, L"[memory], static page memory full, free size: %d, size: %d",
                static_cast<int>(end_ - current_), static_cast<int>(aligned));
            return NULL;
        }
        return owner->Allocate(aligned, file, line);
    }
    void* AllocateZero(size_t size, const wchar_t* file, int line) override {
        void* result = Allocate(size, file, line);
        if (result) memset(result, 0, size);
        return result;
    }
    void* Reallocate(void* memory, size_t size, const wchar_t* file, int line) override {
        MemoryManager* owner = MemoryManager_General();
        return owner ? owner->Reallocate(memory, size, file, line) : NULL;
    }
    void Reserved30() override {}
    void Free(void* memory) override {
        u8* address = static_cast<u8*>(memory);
        if (address < page_ || address >= end_) {
            MemoryManager* owner = MemoryManager_General();
            if (owner) owner->Free(memory);
        }
    }
    void DeleteCriticalSection() override {}

private:
    u8* page_;
    u8* current_;
    u8* end_;
};

} // namespace

MemoryManager* MemoryManager_CreateStaticPage() {
    void* storage = malloc(sizeof(StaticPageMemoryManager));
    if (!storage) return NULL;
    MemoryManager* manager = new (storage) StaticPageMemoryManager();
    if (!manager->Initialize(0, NULL)) {
        manager->~MemoryManager();
        free(storage);
        return NULL;
    }
    return manager;
}

} // namespace blood
