// Private declaration of the IXmlConfigReader implementation.
#pragma once
#include "XmlReaderInternal.h"

namespace xmlimpl {

class XmlConfigReaderImpl : public blood::IXmlConfigReader {
public:
    explicit XmlConfigReaderImpl(IXmlReader* reader) : reader_(reader) { Reset(); }
    ~XmlConfigReaderImpl() { FreeArrays(); }

    bool SetGroupedDefinitions(const void* defs, int count, void* (*alloc)(size_t), size_t structSize) override;
    bool SetDefinitions(const blood::XmlConfigDef* defs, int count, void* (*alloc)(size_t), size_t structSize) override;
    void* GetReserved10() override;
    void* LoadFromMemory(size_t* outSize, const void* mem, size_t len, const wchar_t* fileName,
                         const wchar_t* module, unsigned major, unsigned minor, bool createDefault,
                         bool flags) override;
    void* Load(size_t* outSize, const wchar_t* fileName, const wchar_t* module, char flag,
               unsigned major, unsigned minor, char createDefault, char flags) override;
    void* LoadConfig(const wchar_t* fileName, const wchar_t* module, char flags) override;
    const wchar_t* GetError() override;
    void Destruct(bool freeObject) override;

private:
    void Reset();
    void FreeArrays();
    void SetError(const wchar_t* fmt, ...);
    bool InstallDefinitions(const void* defs, int count, void* (*alloc)(size_t), size_t structSize, bool grouped);
    int FindDefinition(const wchar_t* name) const;
    void ApplyDefault(int index);
    void ConvertValue(int index, const wchar_t* value);
    void* ValidateAndPack(const void* mem, size_t len, const wchar_t* fileName,
                          const wchar_t* module, unsigned major, size_t* outSize);

    IXmlReader* reader_;
    const blood::XmlConfigDef* defs_;
    const void* altDefs_;
    int count_;
    void** values_;
    unsigned char* seen_;
    std::wstring error_;
    void* (*alloc_)(size_t);
    size_t structSize_;
    bool grouped_;
    bool versionCheck_;
};

} // namespace xmlimpl
