// BloodLib - host side of XmlReader_sv64.dll.
#include "XmlReader.h"

namespace blood {

bool XmlReader_Open(XmlReaderApi* api, const wchar_t* dllName) {
    if (!api) return false;
    memset(api, 0, sizeof(*api));
    HMODULE module = LoadLibraryW(dllName ? dllName : kXmlReaderDllName);
    if (!module) return false;
    api->module = module;
    api->GetInterfaceVersion = reinterpret_cast<const wchar_t* (*)()>(GetProcAddress(module, "GetInterfaceVersion"));
    api->CreateXmlReader = reinterpret_cast<IXmlReader* (*)()>(GetProcAddress(module, "CreateXmlReader"));
    api->DestroyXmlReader = reinterpret_cast<void (*)(IXmlReader*)>(GetProcAddress(module, "DestroyXmlReader"));
    api->CreateXmlConfigReader = reinterpret_cast<IXmlConfigReader* (*)(IXmlReader*)>(GetProcAddress(module, "CreateXmlConfigReader"));
    api->DestroyXmlConfigReader = reinterpret_cast<void (*)(IXmlConfigReader*)>(GetProcAddress(module, "DestroyXmlConfigReader"));
    api->EncodeToConfigPacket = reinterpret_cast<void* (*)(size_t*, const void*, int, const void*,
        size_t, const int*, void* (*)(size_t))>(GetProcAddress(module, "EncodeToConfigPacket"));
    api->DecodeFromConfigPacket = reinterpret_cast<void* (*)(size_t*, const void*, size_t, size_t,
        const int*, size_t, int, const void*, void* (*)(size_t))>(GetProcAddress(module, "DecodeFromConfigPacket"));
    if (!api->GetInterfaceVersion || !api->CreateXmlReader || !api->DestroyXmlReader ||
        !api->CreateXmlConfigReader || !api->DestroyXmlConfigReader ||
        !api->EncodeToConfigPacket || !api->DecodeFromConfigPacket) {
        FreeLibrary(module);
        memset(api, 0, sizeof(*api));
        return false;
    }
    return true;
}

bool XmlReader_CheckVersion(const XmlReaderApi& api, const wchar_t** loadedVersion) {
    if (!api.GetInterfaceVersion) return false;
    const wchar_t* loaded = api.GetInterfaceVersion();
    if (loadedVersion) *loadedVersion = loaded ? loaded : L"";
    return loaded && _wcsicmp(loaded, kXmlReaderRequiredVersion) == 0;
}

void XmlReader_Close(XmlReaderApi* api) {
    if (!api) return;
    if (api->module) FreeLibrary(api->module);
    memset(api, 0, sizeof(*api));
}

} // namespace blood
