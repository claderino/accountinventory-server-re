// datareader - schema-driven XML element reader and alias map
// (original: ElReaderImpl.cpp, AliasImpl.cpp).
//
// The XmlReader DLL is the frozen ranking ABI; its header is included when the BloodLib port is
// present. The fallback declarations below let this library be compile-checked stand-alone and
// have exactly the same slot layout, so compiled callers are interchangeable.
#pragma once
#if !defined(DATAREADER_STANDALONE_XML) && __has_include("../BloodLib/XmlReader.h")
#include "../BloodLib/XmlReader.h"
#else
namespace blood {
class IXmlElement;
class IXmlDoc { public: virtual ~IXmlDoc() {} virtual bool IsValid() = 0;
    virtual void* GetInternal() = 0; virtual IXmlElement* GetRootElement() = 0;
    virtual size_t GetSerializedSize() = 0; virtual bool Serialize(void*, size_t) = 0;
    virtual bool Deserialize(const void*, size_t) = 0; virtual void Destroy() = 0;
    virtual void Delete() = 0; };
class IXmlElement { public: virtual ~IXmlElement() {}
    virtual int GetChildCount() = 0; virtual IXmlElement* GetFirstChildElement() = 0;
    virtual IXmlElement* GetNextSiblingElement() = 0; virtual const wchar_t* GetName() = 0;
    virtual int GetLine() = 0; virtual int GetAttributeCount() = 0;
    virtual const wchar_t* GetAttributeName(int) = 0; virtual IXmlElement* GetChildAt(int) = 0;
    virtual const wchar_t* GetAttribute(const wchar_t*) = 0;
    virtual int GetAttributeIndex(const wchar_t*) = 0;
    virtual const wchar_t* GetAttributeValue(int) = 0;
    virtual void* GetNode() = 0; virtual void* GetNode2() = 0; virtual int GetReserved70() = 0; };
class IXmlFileIO { public: virtual ~IXmlFileIO() {}
    virtual int Init(const wchar_t*, const wchar_t*, int) = 0;
    virtual int Read(void*, int*) = 0; virtual int GetFileSize() = 0;
    virtual const wchar_t* GetCurrentFileName() = 0; virtual int OpenNext() = 0;
    virtual void Close() = 0; virtual void Destroy() = 0; };
class IXmlReader { public: virtual ~IXmlReader() {}
    virtual bool Init(IXmlFileIO*, void*, char) = 0; virtual void* SetErrorSink(void*) = 0;
    virtual void* GetErrorSink() = 0; virtual void Close(bool) = 0;
    virtual IXmlFileIO* GetFileIO() = 0;
    virtual bool ParseWithBuilder(const wchar_t*, void*) = 0;
    virtual IXmlDoc* ReadFile(const wchar_t*) = 0;
    virtual IXmlDoc* ReadMemory(const void*, size_t, const wchar_t*) = 0;
    virtual void DestroyDoc(IXmlDoc*) = 0; virtual IXmlDoc* NewDoc() = 0;
    virtual bool CheckHeader(void*) = 0; virtual bool CheckHeaderSized(void*, size_t) = 0; };
} // namespace blood
#endif

#include "Schemas.h"
#include <cstddef>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace achievement {
namespace datareader {

// Case-insensitive wide-string -> opaque u64 map. First insertion wins.
// vt slots: +0x00 dtor, +0x08 Find, +0x10 Add, +0x18 Clear, +0x20 RemoveKeys,
//           +0x28 Commit, +0x30 LoadBinary, +0x38 return 0, +0x40 validity, +0x48 deleting dtor.
class IAliasMap {
public:
    IAliasMap();
    bool Find(const wchar_t* key, uint64_t* value) const;
    bool Add(const wchar_t* key, uint64_t value);
    void Clear();
    void RemoveKeys(const wchar_t* colonList, char flag);
    void Commit();
    bool LoadBinary(const void* data, size_t size, size_t count, char replace);

    void** vt;                       // explicit slot array, mirrors the original vtable
    static IAliasMap* Create();
    static void Destroy(IAliasMap** map);

private:
    IAliasMap(const IAliasMap&);
    IAliasMap& operator=(const IAliasMap&);
    struct Entry { std::wstring key; uint64_t value; };
    std::vector<Entry> entries_;     // insertion order until Commit, sorted afterwards
    std::unordered_map<std::wstring, uint64_t> index_;
    bool committed_;
};

// Schema-driven converter. vt slots follow the original IElReader vtable:
// +0x00 Initialize, +0x08 FindElement, +0x10 GetColumnType, +0x18 ReadRecord,
// +0x20 ReadRecordWithDefault, +0x28 ElementSize, +0x30 PackStrings,
// +0x38 ClearOwnedStrings, +0x40 UnpackStrings, +0x48 RelocateStrings.
class IElReader {
public:
    IElReader();
    ~IElReader();

    bool Initialize(const TableSchemaDesc** schemas, int16_t count, void* defaults, bool flag);
    int8_t FindElement(blood::IXmlElement* element, const TableSchemaDesc* schema,
                       IAliasMap* aliases, void* record);
    int8_t GetColumnType(blood::IXmlElement* element, const TableSchemaDesc* schema,
                         IAliasMap* aliases, void* record);
    bool ReadRecord(blood::IXmlElement* element, const TableSchemaDesc* schema,
                    void* record, size_t capacity, IAliasMap* aliases, void* context);
    bool ReadRecordWithDefault(blood::IXmlElement* element, const TableSchemaDesc* schema,
                               void* record, size_t capacity, IAliasMap* aliases, void* context);
    int ElementSize(void* record, const TableSchemaDesc* schema) const;
    bool PackStrings(void* record, const TableSchemaDesc* schema, unsigned char* buffer,
                     size_t* cursor, bool copyFlag);
    void ClearOwnedStrings(void* record, const TableSchemaDesc* schema);
    void UnpackStrings(void* record, const TableSchemaDesc* schema);
    bool RelocateStrings(void* record, const TableSchemaDesc* schema, int64_t delta);

    static IElReader* Create();
    static void Destroy(IElReader** reader);

    // Convenience parsers (static, original sub_3E620 / sub_3ECE0 / helpers).
    static bool ParseBool(const wchar_t* text, bool* out);
    static bool ParseVersion(const wchar_t* text, int16_t* major, int16_t* minor);
    static void TrimWide(wchar_t* dst, size_t capacity, const wchar_t* src);
    static bool LooksNumeric(const wchar_t* text);

    void** vt;
    const TableSchemaDesc** schemas_;
    int16_t schemaCount_;

private:
    IElReader(const IElReader&);
    IElReader& operator=(const IElReader&);
    bool ReadColumns(blood::IXmlElement* element, const ColumnDesc* columns, int count,
                     unsigned char* record, IAliasMap* aliases);
    bool HandleAlias(blood::IXmlElement* element, const TableSchemaDesc* schema,
                     const ElementDesc& elementDesc, unsigned char* record, IAliasMap* aliases);
    static bool ConvertValue(uint8_t type, void* dst, const wchar_t* text,
                             const ColumnDesc& column, size_t index, IAliasMap* aliases);
    static void CollectStringOffsets(const TableSchemaDesc* schema, std::vector<uint16_t>* out);
};

// Format a schema alias + alias text into the canonical alias-map key.
std::wstring MakeAliasKey(const wchar_t* schemaAlias, const wchar_t* alias);

} // namespace datareader
} // namespace achievement
