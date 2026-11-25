// BloodLib - interface of bsConsole_sv64.dll (NCSoft server console window).
//
// The daemon does not link the DLL: in window mode it LoadLibrary()s "bsConsole_sv64.dll",
// resolves GetInterfaceVersion / CreateConsoleWindow / DestroyConsoleWindow and talks to the
// returned object through the 17-slot vtable below. THIS build fetches GetInterfaceVersion but
// never calls or compares it (see REPORT_framework_net.md §4.3).
#pragma once
#include "BloodLib.h"

namespace blood {

class LogBuffer;

class IConsoleWindow {
public:
    // No virtual destructor: slot 0 is Create. Dispose through Destroy()/DestroyConsoleWindow().
    virtual bool Create(HINSTANCE hInstance, const wchar_t* title, const wchar_t* daemonName,
                        void* callback1, void (*exitCallback)(void), bool* reloadEnabled,
                        bool* shutdownEnabled, void* param9, void* hook) = 0;   // +0x00
    virtual void Destroy() = 0;                                                // +0x08
    virtual void ShowRun() = 0;                                                // +0x10
    virtual HWND GetHwnd() = 0;                                                // +0x18
    virtual void SetTitle(const wchar_t* title) = 0;                           // +0x20
    virtual void SetConnected(bool connected) = 0;                             // +0x28
    virtual void EnableConnect(bool enable) = 0;                               // +0x30
    virtual void EnableReload(bool enable) = 0;                                // +0x38
    virtual void EnableShutdown(bool enable) = 0;                              // +0x40
    virtual void Connect() = 0;                                                // +0x48
    virtual void Disconnect() = 0;                                             // +0x50
    virtual void SetStateText(const wchar_t* text) = 0;                        // +0x58
    virtual void SetBusyText(const wchar_t* text, UINT timeoutMs) = 0;         // +0x60
    virtual void ShowMessage(const wchar_t* text, UINT timeoutMs) = 0;         // +0x68
    virtual void EnqueueLogRecord(LogBuffer* record) = 0;                     // +0x70
    virtual void SetCounters(int a, int b, int c, int d, int e, int f) = 0;    // +0x78
    virtual IConsoleWindow* ResetAndDestroy() = 0;                            // +0x80

protected:
    IConsoleWindow() {}
    ~IConsoleWindow() {}
};

const wchar_t* const kConsoleDllName = L"bsConsole_sv64.dll";

struct ConsoleDll {
    HMODULE module;
    const wchar_t* (*GetInterfaceVersion)();
    IConsoleWindow* (*CreateConsoleWindow)(HMODULE module);
    void (*DestroyConsoleWindow)(IConsoleWindow* window);

    bool IsOpen() const { return module != NULL; }
};

// LoadLibrary + GetProcAddress of the three exports; false (and `dll` zeroed) on any failure.
bool ConsoleDll_Open(ConsoleDll* dll, const wchar_t* dllName = kConsoleDllName);
void ConsoleDll_Close(ConsoleDll* dll);

} // namespace blood
