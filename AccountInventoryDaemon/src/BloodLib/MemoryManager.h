// BloodLib - memory-manager interface (original: MemoryManager.h / Memory.cpp).
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <windows.h>
#include <stddef.h>
#include <stdint.h>

namespace blood {

class MemoryManager {
public:
    virtual ~MemoryManager() {}

    // Vtable layout used by the original: +0x08 Initialize, +0x18 Allocate,
    // +0x20 AllocateZero, +0x28 Reallocate, +0x38 Free.
    virtual bool Initialize(int section, void* parameter) = 0;
    virtual void Reserved10() = 0;
    virtual void* Allocate(size_t size, const wchar_t* file, int line) = 0;
    virtual void* AllocateZero(size_t size, const wchar_t* file, int line) = 0;
    virtual void* Reallocate(void* memory, size_t size, const wchar_t* file, int line) = 0;
    virtual void Reserved30() = 0;
    virtual void Free(void* memory) = 0;
    virtual void DeleteCriticalSection() = 0;
};

class PagePoolManager {
public:
    PagePoolManager();
    ~PagePoolManager();

    bool Initialize(size_t blockSize, size_t maximumChunkSize);
    void* Allocate();

private:
    PagePoolManager(const PagePoolManager&);
    PagePoolManager& operator=(const PagePoolManager&);
    CRITICAL_SECTION lock_;
    size_t blockSize_;
    size_t chunkSize_;
    unsigned char* current_;
    unsigned char* end_;
};

MemoryManager* MemoryManager_Create(int section);
MemoryManager* MemoryManager_CreateStaticPage();
MemoryManager* MemoryManager_General();
MemoryManager* MemoryManager_Static();

void* Memory_Allocate(size_t size, const wchar_t* file, int line);
void* Memory_AllocateZero(size_t size, const wchar_t* file, int line);
void* Memory_Reallocate(void* memory, size_t size, const wchar_t* file, int line);
void Memory_Free(void* memory);

bool Memory_Init();
void Memory_Shutdown();

} // namespace blood

// Original framework allocation syntax: new (size, file, line) T.
void* operator new(size_t size, const wchar_t* file, int line);
void* operator new[](size_t size, const wchar_t* file, int line);
