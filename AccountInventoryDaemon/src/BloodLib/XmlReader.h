// BloodLib - interface of XmlReader_sv64.dll (original: XmlReader.h).
#pragma once
#include "BloodLib.h"

namespace blood {

class IXmlReader;
class IXmlDoc;
class IXmlElement;
class IXmlNode;
class IXmlFileIO;
class IXmlConfigReader;
class IXmlSaxBuilder;

class IXmlReader {
public:
    virtual bool Init(IXmlFileIO* io, void* reserved, char backend) = 0;
    virtual void* SetErrorSink(void* sink) = 0;
    virtual void* GetErrorSink() = 0;
    virtual void Close(bool destroyHeap) = 0;
    virtual IXmlFileIO* GetFileIO() = 0;
    virtual bool ParseWithBuilder(const wchar_t* path, IXmlSaxBuilder* sink) = 0;
    virtual IXmlDoc* ReadFile(const wchar_t* path) = 0;
    virtual IXmlDoc* ReadMemory(const void* buffer, size_t length, const wchar_t* name) = 0;
    virtual void DestroyDoc(IXmlDoc* doc) = 0;
    virtual IXmlDoc* NewDoc() = 0;
    virtual bool CheckHeader(void* header) = 0;
    virtual bool CheckHeaderSized(void* header, size_t size) = 0;
};

class IXmlDoc {
public:
    virtual bool IsValid() = 0;
    virtual void* GetInternal() = 0;
    virtual IXmlElement* GetRootElement() = 0;
    virtual size_t GetSerializedSize() = 0;
    virtual bool Serialize(void* buffer, size_t size) = 0;
    virtual bool Deserialize(const void* buffer, size_t size) = 0;
    virtual void Destroy() = 0;
    virtual void Delete() = 0;
};

class IXmlNode {
public:
    virtual int GetType() = 0;
    virtual bool IsValid() = 0;
};

class IXmlElement {
public:
    virtual int GetChildCount() = 0;
    virtual IXmlElement* GetFirstChildElement() = 0;
    virtual IXmlElement* GetNextSiblingElement() = 0;
    virtual const wchar_t* GetName() = 0;
    virtual int GetLine() = 0;
    virtual int GetAttributeCount() = 0;
    virtual const wchar_t* GetAttributeName(int index) = 0;
    virtual IXmlElement* GetChildAt(int index) = 0;
    virtual const wchar_t* GetAttribute(const wchar_t* name) = 0;
    virtual int GetAttributeIndex(const wchar_t* name) = 0;
    virtual const wchar_t* GetAttributeValue(int index) = 0;
    virtual IXmlNode* GetNode() = 0;
    virtual IXmlNode* GetNode2() = 0;
    virtual int GetReserved70() = 0;
};

class IXmlFileIO {
public:
    virtual int Init(const wchar_t* directory, const wchar_t* wildcard, int flags) = 0;
    virtual int Read(void* buffer, int* capacity) = 0;
    virtual int GetFileSize() = 0;
    virtual const wchar_t* GetCurrentFileName() = 0;
    virtual int OpenNext() = 0;
    virtual void Close() = 0;
    virtual void Destroy() = 0;
};

class IXmlSaxBuilder {
public:
    virtual void Begin() = 0;
    virtual void Unused08() = 0;
    virtual void Element(IXmlElement* element) = 0;
};

// Frozen 0x28-byte binary layout. The default slot is untyped in the original table: numeric
// defaults point at raw integers, boolean defaults may be the immediate value 1, and string
// defaults point at wide text. DaemonConfig converts these to text for the reconstructed DLL.
struct XmlConfigDef {
    int64_t offset;
    const wchar_t* name;
    int32_t type;
    int32_t flags;
    const void* defaultValue;
    const wchar_t* description;
};

class IXmlConfigReader {
public:
    virtual bool SetGroupedDefinitions(const void* defs, int count, void* (*alloc)(size_t),
                                       size_t structSize) = 0;
    virtual bool SetDefinitions(const XmlConfigDef* defs, int count, void* (*alloc)(size_t),
                                size_t structSize) = 0;
    virtual void* GetReserved10() = 0;
    virtual void* LoadFromMemory(size_t* outSize, const void* memory, size_t length,
                                 const wchar_t* fileName, const wchar_t* module,
                                 unsigned major, unsigned minor, bool createDefault,
                                 bool flags) = 0;
    virtual void* Load(size_t* outSize, const wchar_t* fileName, const wchar_t* module,
                       char flag, unsigned major, unsigned minor, char createDefault,
                       char flags) = 0;
    virtual void* LoadConfig(const wchar_t* fileName, const wchar_t* module, char flags) = 0;
    virtual const wchar_t* GetError() = 0;
    virtual void Destruct(bool freeObject) = 0;
};

const wchar_t* const kXmlReaderDllName = L"XmlReader_sv64.dll";
const wchar_t* const kXmlReaderRequiredVersion = L"13";

struct XmlReaderApi {
    HMODULE module;
    const wchar_t* (*GetInterfaceVersion)();
    IXmlReader* (*CreateXmlReader)();
    void (*DestroyXmlReader)(IXmlReader* reader);
    IXmlConfigReader* (*CreateXmlConfigReader)(IXmlReader* reader);
    void (*DestroyXmlConfigReader)(IXmlConfigReader* reader);
    void* (*EncodeToConfigPacket)(size_t*, const void*, int, const void*, size_t,
                                  const int*, void* (*)(size_t));
    void* (*DecodeFromConfigPacket)(size_t*, const void*, size_t, size_t, const int*,
                                    size_t, int, const void*, void* (*)(size_t));
    bool IsOpen() const { return module != NULL; }
};

bool XmlReader_Open(XmlReaderApi* api, const wchar_t* dllName = kXmlReaderDllName);
bool XmlReader_CheckVersion(const XmlReaderApi& api, const wchar_t** loadedVersion);
void XmlReader_Close(XmlReaderApi* api);

#pragma pack(push, 4)
struct DaemonConfigData {
    const wchar_t* rootPath;          // +0x00
    const wchar_t* configPath;        // +0x08
    const wchar_t* logPath;           // +0x10
    const wchar_t* dataPath;          // +0x18
    const char* cert;                 // +0x20
    const wchar_t* alertAddress;      // +0x28
    int32_t logCodePage;              // +0x30
    int32_t internalPort;             // +0x34
    int32_t managePort;               // +0x38
    int32_t workerThread;             // +0x3C
    int32_t dumpType;                 // +0x40
    int32_t checkPingTime;            // +0x44
    bool pingEnable;                  // +0x48
    bool disconnectWhenPingTimeout;   // +0x49
    int32_t eventThread;              // +0x4C
    int32_t maxPcCount;               // +0x50
    int32_t updateCycle;              // +0x54
    bool logRegisterChange;           // +0x58
    u8 reserved59[7];                  // +0x59..+0x5F
};
#pragma pack(pop)

} // namespace blood
