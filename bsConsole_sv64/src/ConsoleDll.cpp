// Reconstructed exports of bsConsole_sv64.dll.
// Only these three symbols are exported (undecorated, extern "C"), exactly as in the original.
#include "ConsoleWindow.h"

#include <new>

SOURCE_FILE("ConsoleDll.cpp");

namespace {

// The original allocates the wrapper and all inner objects from a private, growable heap created
// on the first CreateConsoleWindow call and destroyed by DestroyConsoleWindow.
HANDLE g_heap = NULL;

void* AllocFromHeap(size_t bytes) {
    if (!g_heap) g_heap = HeapCreate(0, 0, 0);
    if (!g_heap) return NULL;
    return HeapAlloc(g_heap, 0, bytes);
}

} // namespace

extern "C" __declspec(dllexport) const wchar_t* GetInterfaceVersion() {
    static const wchar_t kVersion[] = L"1";
    return kVersion;
}

extern "C" __declspec(dllexport) blood::IConsoleWindow* CreateConsoleWindow(HMODULE module) {
    void* mem = AllocFromHeap(sizeof(bsconsole::ConsoleWindow));
    if (!mem) return NULL;
    // No destructor call here: Destroy() released all resources, exactly like the original.
    return new (mem) bsconsole::ConsoleWindow(module);
}

extern "C" __declspec(dllexport) void DestroyConsoleWindow(blood::IConsoleWindow* window) {
    if (window) HeapFree(g_heap, 0, window);
    if (g_heap) {
        HeapDestroy(g_heap);
        g_heap = NULL;
    }
}
