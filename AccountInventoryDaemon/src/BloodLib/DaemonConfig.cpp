// BloodLib - AchievementDaemon configuration loader and printer (original: DaemonConfig.cpp).
#include "Daemon.h"

#include <stdio.h>
#include <stddef.h>

SOURCE_FILE("DaemonConfig.cpp");

namespace blood {
namespace {

void* ConfigAlloc(size_t bytes) { return malloc(bytes); }

bool IsXmlFile(const wchar_t* path) {
    if (!path) return false;
    size_t length = wcslen(path);
    return length >= 4 && _wcsicmp(path + length - 4, L".xml") == 0;
}

} // namespace

static_assert(sizeof(DaemonConfigData) == 0x60, "packed config size");
static_assert(offsetof(DaemonConfigData, rootPath) == 0x00, "packed config offset");
static_assert(offsetof(DaemonConfigData, cert) == 0x20, "packed config offset");
static_assert(offsetof(DaemonConfigData, alertAddress) == 0x28, "packed config offset");
static_assert(offsetof(DaemonConfigData, logCodePage) == 0x30, "packed config offset");
static_assert(offsetof(DaemonConfigData, dumpType) == 0x40, "packed config offset");
static_assert(offsetof(DaemonConfigData, pingEnable) == 0x48, "packed config offset");
static_assert(offsetof(DaemonConfigData, maxPcCount) == 0x50, "packed config offset");
static_assert(offsetof(DaemonConfigData, logRegisterChange) == 0x58, "packed config offset");

DaemonConfig::DaemonConfig() : buffer_(NULL), data_(NULL) {}
DaemonConfig::~DaemonConfig() {
    free(buffer_);
    buffer_ = NULL;
    data_ = NULL;
}

bool DaemonConfig::Load(const wchar_t* configPathOrFile, XmlReaderApi* xml) {
    wchar_t file[MAX_PATH];
    if (!configPathOrFile || !*configPathOrFile) {
        wcscpy_s(file, MAX_PATH, L"achievementd.config.xml");
    } else if (IsXmlFile(configPathOrFile)) {
        wcscpy_s(file, MAX_PATH, configPathOrFile);
    } else {
        swprintf_s(file, MAX_PATH, L"%s\\achievementd.config.xml", configPathOrFile);
    }

    XmlReaderApi local;
    memset(&local, 0, sizeof(local));
    if (!xml || !xml->IsOpen()) {
        if (!XmlReader_Open(&local)) {
            LogWrite(0, L"src\\DaemonConfig.cpp", 0x26,
                     L"[config], cannot load xml reader library, file: %s", file);
            Sleep(5000);
            return false;
        }
        xml = &local;
    }
    const wchar_t* loaded = L"";
    if (!XmlReader_CheckVersion(*xml, &loaded)) {
        LogWrite(0, L"src\\DaemonConfig.cpp", 0x2A,
                 L"[config], xml reader check version failed, file: %s, required: %s, loaded: %s, platform binary:%d",
                 file, kXmlReaderRequiredVersion, loaded, 0);
        Sleep(5000);
        if (local.IsOpen()) XmlReader_Close(&local);
        return false;
    }
    IXmlReader* reader = xml->CreateXmlReader();
    if (!reader) {
        LogWrite(0, L"src\\DaemonConfig.cpp", 0x30,
                 L"[config], cannot load xml reader library, file: %s", file);
        if (local.IsOpen()) XmlReader_Close(&local);
        return false;
    }
    if (!reader->Init(NULL, NULL, 0)) {
        LogWrite(0, L"src\\DaemonConfig.cpp", 0x33,
                 L"[config], xml reader library initialize failed.");
        xml->DestroyXmlReader(reader);
        if (local.IsOpen()) XmlReader_Close(&local);
        Sleep(5000);
        return false;
    }
    IXmlConfigReader* configReader = xml->CreateXmlConfigReader(reader);
    if (!configReader) {
        LogWrite(0, L"src\\DaemonConfig.cpp", 0x3A,
                 L"[config], cannot create xml config, file: %s", file);
        xml->DestroyXmlReader(reader);
        if (local.IsOpen()) XmlReader_Close(&local);
        return false;
    }

    // The copied XmlReader DLL expects textual defaults. The original image's table stores raw
    // numeric defaults; convert only the temporary table handed to the DLL.
    XmlConfigDef effective[kDaemonConfigDefCount];
    wchar_t defaultText[kDaemonConfigDefCount][64];
    memcpy(effective, kDaemonConfigDefs, sizeof(effective));
    for (int i = 0; i < kDaemonConfigDefCount; ++i) {
        defaultText[i][0] = L'\0';
        effective[i].defaultValue = NULL;
        const void* value = kDaemonConfigDefs[i].defaultValue;
        if (!value || kDaemonConfigDefs[i].type == 0) continue;
        uintptr_t numeric = reinterpret_cast<uintptr_t>(value);
        if (kDaemonConfigDefs[i].type == 1 || kDaemonConfigDefs[i].type == 4) {
            i32 integer = numeric <= 0x10000 ? static_cast<i32>(numeric)
                                            : *static_cast<const i32*>(value);
            swprintf_s(defaultText[i], 64, L"%d", integer);
            effective[i].defaultValue = defaultText[i];
        } else if (kDaemonConfigDefs[i].type == 6 || kDaemonConfigDefs[i].type == 7 ||
                   kDaemonConfigDefs[i].type == 8) {
            effective[i].defaultValue = value;
        }
    }
    if (!configReader->SetDefinitions(effective, kDaemonConfigDefCount, ConfigAlloc,
                                      kDaemonConfigSize)) {
        const wchar_t* error = configReader->GetError();
        LogWrite(0, L"src\\DaemonConfig.cpp", 0x40,
                 L"[config], xml config library initialize failed, file: %s, error: %s",
                 file, error ? error : L"");
        xml->DestroyXmlConfigReader(configReader);
        xml->DestroyXmlReader(reader);
        if (local.IsOpen()) XmlReader_Close(&local);
        Sleep(5000);
        return false;
    }
    Daemon* daemon = Daemon::Instance();
    const wchar_t* module = daemon ? daemon->GetLogTag() : L"achievement-daemon";
    void* packed = configReader->LoadConfig(file, module, 0);
    if (!packed) {
        const wchar_t* error = configReader->GetError();
        LogWrite(0, L"src\\DaemonConfig.cpp", 0x49,
                 L"[config], cannot load config, file: %s, error: %s",
                 file, error ? error : L"");
        xml->DestroyXmlConfigReader(configReader);
        xml->DestroyXmlReader(reader);
        if (local.IsOpen()) XmlReader_Close(&local);
        Sleep(5000);
        return false;
    }
    free(buffer_);
    buffer_ = packed;
    data_ = static_cast<DaemonConfigData*>(packed);
    PrintAll();
    xml->DestroyXmlConfigReader(configReader);
    xml->DestroyXmlReader(reader);
    if (local.IsOpen()) XmlReader_Close(&local);
    return true;
}

void DaemonConfig::PrintAll() {
    if (!data_) return;
    const u8* base = reinterpret_cast<const u8*>(data_);
    for (int i = 0; i < kDaemonConfigDefCount; ++i) {
        const XmlConfigDef& definition = kDaemonConfigDefs[i];
        if (definition.offset < 0) {
            LogWrite(1, L"src\\DaemonConfig.cpp", 0x84, L"[config], ==== %s ====",
                     definition.description ? definition.description : L"");
            continue;
        }
        const void* value = base + definition.offset;
        switch (definition.type) {
        case 1:
            LogWrite(1, L"src\\DaemonConfig.cpp", 0x8D, L"[config], %s : %d",
                     definition.name, *static_cast<const i32*>(value));
            break;
        case 2:
            LogWrite(1, L"src\\DaemonConfig.cpp", 0x8E, L"[config], %s : %I64d",
                     definition.name, *static_cast<const i64*>(value));
            break;
        case 3:
            LogWrite(1, L"src\\DaemonConfig.cpp", 0x8F, L"[config], %s : %f",
                     definition.name, static_cast<double>(*static_cast<const float*>(value)));
            break;
        case 4:
            LogWrite(1, L"src\\DaemonConfig.cpp", 0x90, L"[config], %s : %s",
                     definition.name, *static_cast<const u8*>(value) ? L"yes" : L"no");
            break;
        case 5:
            if (*static_cast<const u32*>(value) == 0)
                LogWrite(1, L"src\\DaemonConfig.cpp", 0x94, L"[config], %s : %s",
                         definition.name, L"N/A");
            else
                LogWrite(1, L"src\\DaemonConfig.cpp", 0x94, L"[config], %s : %s",
                         definition.name, IpToWideString(*static_cast<const u32*>(value)));
            break;
        case 6:
        case 7: {
            const wchar_t* text = *static_cast<const wchar_t* const*>(value);
            LogWrite(1, L"src\\DaemonConfig.cpp", 0x9C, L"[config], %s : %s",
                     definition.name, text ? text : L"N/A");
            break;
        }
        case 8: {
            const char* text = *static_cast<const char* const*>(value);
            wchar_t converted[1024] = L"N/A";
            if (text && *text) MultiByteToWideChar(CP_ACP, 0, text, -1, converted, 1024);
            LogWrite(1, L"src\\DaemonConfig.cpp", 0xA4, L"[config], %s : %s",
                     definition.name, converted);
            break;
        }
        default:
            break;
        }
    }
}

const wchar_t* DaemonConfig::RootPath() const { return data_ ? data_->rootPath : NULL; }
const wchar_t* DaemonConfig::ConfigPath() const { return data_ ? data_->configPath : NULL; }
const wchar_t* DaemonConfig::LogPath() const { return data_ ? data_->logPath : NULL; }
const wchar_t* DaemonConfig::DataPath() const { return data_ ? data_->dataPath : NULL; }
const char* DaemonConfig::CertFile() const { return data_ ? data_->cert : NULL; }
const wchar_t* DaemonConfig::AlertAddress() const { return data_ ? data_->alertAddress : NULL; }
int DaemonConfig::LogCodePage() const { return data_ ? data_->logCodePage : 0; }
int DaemonConfig::InternalPort() const { return data_ ? data_->internalPort : 22200; }
int DaemonConfig::ManagePort() const { return data_ ? data_->managePort : 32200; }
int DaemonConfig::WorkerThread() const { return data_ ? data_->workerThread : 0; }
int DaemonConfig::DumpType() const { return data_ ? data_->dumpType : 2; }
int DaemonConfig::CheckPingTime() const { return data_ ? data_->checkPingTime : 30000; }
bool DaemonConfig::PingEnable() const { return data_ ? data_->pingEnable : true; }
bool DaemonConfig::DisconnectWhenPingTimeout() const {
    return data_ ? data_->disconnectWhenPingTimeout : true;
}
int DaemonConfig::EventThread() const { return data_ ? data_->eventThread : 0; }
int DaemonConfig::MaxPcCount() const { return data_ ? data_->maxPcCount : 400000; }
int DaemonConfig::UpdateCycle() const { return data_ ? data_->updateCycle : 3000; }
bool DaemonConfig::LogRegisterChange() const { return data_ ? data_->logRegisterChange : false; }

} // namespace blood
