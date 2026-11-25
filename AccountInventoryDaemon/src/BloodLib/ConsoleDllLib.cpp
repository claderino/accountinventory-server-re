// BloodLib - host side of bsConsole_sv64.dll: load the library and bind its three exports.
// (Original: WindowConsole.cpp sub_2D170. The original retrieves GetInterfaceVersion but never
//  calls it, so there is no version check and no text-console fallback in this build.)
#include "BsConsole.h"

namespace blood {

bool ConsoleDll_Open(ConsoleDll* dll, const wchar_t* dllName) {
    memset(dll, 0, sizeof(*dll));
    HMODULE module = LoadLibraryW(dllName ? dllName : kConsoleDllName);
    if (!module) return false;

    dll->module = module;
    dll->GetInterfaceVersion =
        reinterpret_cast<const wchar_t* (*)()>(GetProcAddress(module, "GetInterfaceVersion"));
    dll->CreateConsoleWindow =
        reinterpret_cast<IConsoleWindow* (*)(HMODULE)>(GetProcAddress(module, "CreateConsoleWindow"));
    dll->DestroyConsoleWindow =
        reinterpret_cast<void (*)(IConsoleWindow*)>(GetProcAddress(module, "DestroyConsoleWindow"));

    if (!dll->GetInterfaceVersion || !dll->CreateConsoleWindow || !dll->DestroyConsoleWindow) {
        FreeLibrary(module);
        memset(dll, 0, sizeof(*dll));
        return false;
    }
    return true;
}

void ConsoleDll_Close(ConsoleDll* dll) {
    if (dll->module) FreeLibrary(dll->module);
    memset(dll, 0, sizeof(*dll));
}

} // namespace blood
