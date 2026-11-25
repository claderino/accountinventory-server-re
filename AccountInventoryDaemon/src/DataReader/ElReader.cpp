// datareader - schema-driven element reader (original: ElReaderImpl.cpp) and alias map
// (AliasImpl.cpp). See analysis/IMPL_datareader.md for the descriptor format.
#include "ElReader.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <cwctype>
#include <cstdio>
#include <new>

namespace achievement {
namespace datareader {

namespace {

void* SlotArray(size_t slots) {
    return new (std::nothrow) void*[slots]();
}

bool EqualsIgnoreCase(const wchar_t* a, const wchar_t* b) {
    if (!a || !b) return false;
    return _wcsicmp(a, b) == 0;
}

void UpperCopy(std::wstring* dst, const wchar_t* src) {
    dst->clear();
    if (!src) return;
    size_t n = wcslen(src);
    if (n > 199) n = 199;                 // original truncates at 199 wchar_t
    dst->resize(n);
    for (size_t i = 0; i < n; ++i) (*dst)[i] = (wchar_t)towupper((wint_t)src[i]);
}

bool ParseSigned(const wchar_t* text, int64_t minValue, int64_t maxValue, int64_t* out) {
    if (!text || !out) return false;
    const wchar_t* p = text;
    while (*p == L' ' || *p == L'\t') ++p;
    bool negative = false;
    if (*p == L'+' || *p == L'-') { negative = (*p == L'-'); ++p; }
    if (*p < L'0' || *p > L'9') return false;
    uint64_t value = 0;
    for (; *p >= L'0' && *p <= L'9'; ++p) {
        value = value * 10 + (uint64_t)(*p - L'0');
        if (value > 0x7fffffffffffffffULL) return false;
    }
    // Trailing whitespace is tolerated, anything else is a conversion failure.
    while (*p == L' ' || *p == L'\t') ++p;
    if (*p) return false;
    int64_t signedValue = negative ? -(int64_t)value : (int64_t)value;
    if (signedValue < minValue || signedValue > maxValue) return false;
    *out = signedValue;
    return true;
}

void WriteInt(void* dst, size_t size, int64_t value) {
    if (!dst) return;
    switch (size) {
    case 1: *(int8_t*)dst = (int8_t)value; break;
    case 2: *(int16_t*)dst = (int16_t)value; break;
    case 4: *(int32_t*)dst = (int32_t)value; break;
    case 8: *(int64_t*)dst = value; break;
    default: std::memset(dst, 0, size); std::memcpy(dst, &value, (size < 8) ? size : 8); break;
    }
}

bool AssignString(void* slot, const wchar_t* text, int maxChars) {
    if (!slot) return false;
    wchar_t* old = *(wchar_t**)slot;
    if (old) { delete[] old; *(wchar_t**)slot = NULL; }
    if (!text) return true;
    size_t len = wcslen(text);
    if (maxChars > 0 && len > (size_t)maxChars) len = (size_t)maxChars;
    wchar_t* copy = new (std::nothrow) wchar_t[len + 1];
    if (!copy) return false;
    std::memcpy(copy, text, len * sizeof(wchar_t));
    copy[len] = 0;
    *(wchar_t**)slot = copy;
    return true;
}

void AttributeName(wchar_t* buffer, size_t capacity, const ColumnDesc& column, size_t index) {
    if (column.aux && column.aux[index].name) {
        wcscpy_s(buffer, capacity, column.aux[index].name);
        return;
    }
    const wchar_t* base = column.name ? column.name : L"";
    swprintf_s(buffer, capacity, L"%s-%u", base, (unsigned)(index + 1));
}

} // namespace

// --------------------------------------------------------------------------- IAliasMap

namespace {
void** AliasVTable() {
    static void* slots[10] = {0};
    return slots;
}
bool VT_Alias_Find(void* self, const wchar_t* key, uint64_t* value) {
    return static_cast<IAliasMap*>(self)->Find(key, value);
}
bool VT_Alias_Add(void* self, const wchar_t* key, uint64_t value) {
    return static_cast<IAliasMap*>(self)->Add(key, value);
}
void VT_Alias_Clear(void* self) { static_cast<IAliasMap*>(self)->Clear(); }
void VT_Alias_RemoveKeys(void* self, const wchar_t* list, char flag) {
    static_cast<IAliasMap*>(self)->RemoveKeys(list, flag);
}
void VT_Alias_Commit(void* self) { static_cast<IAliasMap*>(self)->Commit(); }
bool VT_Alias_LoadBinary(void* self, const void* data, size_t size, size_t count, char flag) {
    return static_cast<IAliasMap*>(self)->LoadBinary(data, size, count, flag);
}
void* VT_Alias_Null(void*) { return NULL; }
} // namespace

IAliasMap::IAliasMap() : vt(AliasVTable()), committed_(false) {
    static bool installed = false;
    if (!installed) {
        void** s = AliasVTable();
        s[0x00 / 8] = (void*)&VT_Alias_Null;             // destructor (managed by C++ delete)
        s[0x08 / 8] = (void*)&VT_Alias_Find;
        s[0x10 / 8] = (void*)&VT_Alias_Add;
        s[0x18 / 8] = (void*)&VT_Alias_Clear;
        s[0x20 / 8] = (void*)&VT_Alias_RemoveKeys;
        s[0x28 / 8] = (void*)&VT_Alias_Commit;
        s[0x30 / 8] = (void*)&VT_Alias_LoadBinary;
        s[0x38 / 8] = (void*)&VT_Alias_Null;
        s[0x40 / 8] = (void*)&VT_Alias_Null;
        s[0x48 / 8] = (void*)&VT_Alias_Null;
        installed = true;
    }
}

IAliasMap* IAliasMap::Create() { return new (std::nothrow) IAliasMap(); }
void IAliasMap::Destroy(IAliasMap** map) {
    if (!map) return;
    delete *map;
    *map = NULL;
}

bool IAliasMap::Find(const wchar_t* key, uint64_t* value) const {
    if (!key || !value) return false;
    std::wstring normalized;
    UpperCopy(&normalized, key);
    std::unordered_map<std::wstring, uint64_t>::const_iterator it = index_.find(normalized);
    if (it == index_.end()) return false;
    *value = it->second;
    return true;
}

bool IAliasMap::Add(const wchar_t* key, uint64_t value) {
    if (!key || !*key) return false;
    std::wstring normalized;
    UpperCopy(&normalized, key);
    if (index_.find(normalized) != index_.end()) return false;   // first wins
    index_.insert(std::make_pair(normalized, value));
    Entry entry;
    entry.key = normalized;
    entry.value = value;
    entries_.push_back(entry);
    committed_ = false;
    return true;
}

void IAliasMap::Clear() {
    entries_.clear();
    index_.clear();
    committed_ = false;
}

void IAliasMap::RemoveKeys(const wchar_t* colonList, char flag) {
    if (!colonList) return;
    std::wstring list(colonList);
    size_t begin = 0;
    while (begin <= list.size()) {
        size_t end = list.find(L':', begin);
        if (end == std::wstring::npos) end = list.size();
        std::wstring part = list.substr(begin, end - begin);
        std::wstring normalized;
        UpperCopy(&normalized, part.c_str());
        for (size_t i = 0; i < entries_.size();) {
            const std::wstring& key = entries_[i].key;
            bool match = (flag != 0) ? (key.compare(0, normalized.size(), normalized) == 0)
                                     : (key == normalized);
            if (match) {
                index_.erase(entries_[i].key);
                entries_.erase(entries_.begin() + i);
            } else {
                ++i;
            }
        }
        if (end == list.size()) break;
        begin = end + 1;
    }
    committed_ = false;
}

void IAliasMap::Commit() {
    // Linearize the map into a sorted vector (binary-search table in the original).
    struct Compare {
        bool operator()(const Entry& a, const Entry& b) const { return a.key < b.key; }
    };
    std::sort(entries_.begin(), entries_.end(), Compare());
    committed_ = true;
}

bool IAliasMap::LoadBinary(const void*, size_t, size_t, char) {
    // Optional binary cache: never on the daemon's XML path.
    return false;
}

std::wstring MakeAliasKey(const wchar_t* schemaAlias, const wchar_t* alias) {
    std::wstring key;
    if (schemaAlias) key = schemaAlias;
    key += L":";
    if (alias) key += alias;
    return key;
}

// --------------------------------------------------------------------------- IElReader

namespace {
void** ElVTable() {
    static void* slots[10] = {0};
    return slots;
}
bool VT_Initialize(void* self, const TableSchemaDesc** s, int16_t n, void* d, bool f) {
    return static_cast<IElReader*>(self)->Initialize(s, n, d, f);
}
int8_t VT_FindElement(void* self, void* e, const TableSchemaDesc* s, void* a, void* r) {
    return static_cast<IElReader*>(self)->FindElement((blood::IXmlElement*)e, s,
                                                      (IAliasMap*)a, r);
}
int8_t VT_GetColumnType(void* self, void* e, const TableSchemaDesc* s, void* a, void* r) {
    return static_cast<IElReader*>(self)->GetColumnType((blood::IXmlElement*)e, s,
                                                        (IAliasMap*)a, r);
}
bool VT_ReadRecord(void* self, void* e, const TableSchemaDesc* s, void* r, size_t cap,
                   void* a, void* ctx) {
    return static_cast<IElReader*>(self)->ReadRecord((blood::IXmlElement*)e, s, r, cap,
                                                     (IAliasMap*)a, ctx);
}
bool VT_ReadRecordWithDefault(void* self, void* e, const TableSchemaDesc* s, void* r,
                              size_t cap, void* a, void* ctx) {
    return static_cast<IElReader*>(self)->ReadRecordWithDefault((blood::IXmlElement*)e, s,
                                                                r, cap, (IAliasMap*)a, ctx);
}
int VT_ElementSize(void* self, void* r, const TableSchemaDesc* s) {
    return static_cast<IElReader*>(self)->ElementSize(r, s);
}
bool VT_PackStrings(void* self, void* r, const TableSchemaDesc* s, unsigned char* b,
                    size_t* c, bool f) {
    return static_cast<IElReader*>(self)->PackStrings(r, s, b, c, f);
}
void VT_ClearOwnedStrings(void* self, void* r, const TableSchemaDesc* s) {
    static_cast<IElReader*>(self)->ClearOwnedStrings(r, s);
}
void VT_UnpackStrings(void* self, void* r, const TableSchemaDesc* s) {
    static_cast<IElReader*>(self)->UnpackStrings(r, s);
}
bool VT_RelocateStrings(void* self, void* r, const TableSchemaDesc* s, int64_t d) {
    return static_cast<IElReader*>(self)->RelocateStrings(r, s, d);
}
} // namespace

IElReader::IElReader() : vt(ElVTable()), schemas_(NULL), schemaCount_(0) {
    static bool installed = false;
    if (!installed) {
        void** s = ElVTable();
        s[0x00 / 8] = (void*)&VT_Initialize;
        s[0x08 / 8] = (void*)&VT_FindElement;
        s[0x10 / 8] = (void*)&VT_GetColumnType;
        s[0x18 / 8] = (void*)&VT_ReadRecord;
        s[0x20 / 8] = (void*)&VT_ReadRecordWithDefault;
        s[0x28 / 8] = (void*)&VT_ElementSize;
        s[0x30 / 8] = (void*)&VT_PackStrings;
        s[0x38 / 8] = (void*)&VT_ClearOwnedStrings;
        s[0x40 / 8] = (void*)&VT_UnpackStrings;
        s[0x48 / 8] = (void*)&VT_RelocateStrings;
        installed = true;
    }
}

IElReader::~IElReader() {}

IElReader* IElReader::Create() { return new (std::nothrow) IElReader(); }
void IElReader::Destroy(IElReader** reader) {
    if (!reader) return;
    delete *reader;
    *reader = NULL;
}

bool IElReader::Initialize(const TableSchemaDesc** schemas, int16_t count, void*, bool) {
    schemas_ = schemas;
    schemaCount_ = count;
    return true;
}

bool IElReader::ParseBool(const wchar_t* text, bool* out) {
    if (!text || !out) return false;
    wchar_t trimmed[32];
    TrimWide(trimmed, 32, text);
    if (EqualsIgnoreCase(trimmed, L"y") || EqualsIgnoreCase(trimmed, L"yes") ||
        EqualsIgnoreCase(trimmed, L"t") || EqualsIgnoreCase(trimmed, L"true") ||
        wcscmp(trimmed, L"1") == 0) { *out = true; return true; }
    if (EqualsIgnoreCase(trimmed, L"n") || EqualsIgnoreCase(trimmed, L"no") ||
        EqualsIgnoreCase(trimmed, L"f") || EqualsIgnoreCase(trimmed, L"false") ||
        wcscmp(trimmed, L"0") == 0) { *out = false; return true; }
    return false;
}

bool IElReader::LooksNumeric(const wchar_t* text) {
    if (!text) return false;
    bool dot = false;
    for (const wchar_t* p = text; *p; ++p) {
        if (*p == L'.') { if (dot) return false; dot = true; continue; }
        if (*p == L'+' || *p == L'-') continue;
        if (*p < L'0' || *p > L'9') return false;
    }
    return true;
}

void IElReader::TrimWide(wchar_t* dst, size_t capacity, const wchar_t* src) {
    if (!dst || capacity == 0) return;
    if (!src) { dst[0] = 0; return; }
    const wchar_t* begin = src;
    const wchar_t* end = src + wcslen(src);
    while (begin < end && iswspace(*begin)) ++begin;
    while (end > begin && iswspace(end[-1])) --end;
    size_t n = (size_t)(end - begin);
    if (n >= capacity) n = capacity - 1;
    std::memcpy(dst, begin, n * sizeof(wchar_t));
    dst[n] = 0;
}

bool IElReader::ParseVersion(const wchar_t* text, int16_t* major, int16_t* minor) {
    if (!text || !major || !minor) return false;
    const wchar_t* dot = wcschr(text, L'.');
    if (!dot) return false;
    wchar_t a[32], b[32];
    TrimWide(a, 32, text);
    TrimWide(b, 32, dot + 1);
    int64_t lo = 0, hi = 0;
    if (!ParseSigned(a, -32768, 32767, &lo)) return false;
    // b may contain trailing text after a second dot; parse the leading number only.
    if (!b[0]) return false;
    int64_t value = 0;
    for (const wchar_t* p = b; *p >= L'0' && *p <= L'9'; ++p)
        value = value * 10 + (*p - L'0');
    hi = value;
    if (hi < -32768 || hi > 32767) return false;
    *major = (int16_t)lo;
    *minor = (int16_t)hi;
    return true;
}

int8_t IElReader::FindElement(blood::IXmlElement* element, const TableSchemaDesc* schema,
                              IAliasMap*, void* record) {
    if (!element || !schema || !record) return -1;
    const wchar_t* name = element->GetName();
    for (int i = 0; i < schema->elementCount; ++i) {
        const ElementDesc& e = schema->elements[i];
        if (EqualsIgnoreCase(name, e.name)) {
            unsigned char* header = (unsigned char*)record;
            header[0] = e.kind;
            *(int16_t*)(header + 2) = -1;
            *(uint16_t*)(header + 4) = e.set ? e.set->dataSize : 0;
            return (int8_t)i;
        }
    }
    return -1;
}

int8_t IElReader::GetColumnType(blood::IXmlElement* element, const TableSchemaDesc* schema,
                                IAliasMap* aliases, void* record) {
    // Resolve a variant from the `type` attribute; returns the selected child index or -1.
    if (!element || !schema || !record) return -1;
    const wchar_t* type = element->GetAttribute(L"type");
    if (!type || !*type) return -1;
    int8_t elementIndex = FindElement(element, schema, aliases, record);
    if (elementIndex < 0) return -1;
    const ColumnSet& set = *schema->elements[elementIndex].set;
    for (uint16_t i = 0; i < set.childCount; ++i) {
        if (EqualsIgnoreCase(type, set.children[i].name)) {
            *(int16_t*)((unsigned char*)record + 2) = (int16_t)i;
            if (set.children[i].columns && set.children[i].columnCount)
                ReadColumns(element, set.children[i].columns, set.children[i].columnCount,
                            (unsigned char*)record, aliases);
            return (int8_t)i;
        }
    }
    return -1;
}

bool IElReader::ConvertValue(uint8_t type, void* dst, const wchar_t* text,
                             const ColumnDesc& column, size_t index, IAliasMap*) {
    if (!dst) return false;
    int64_t value = 0;
    switch (type) {
    case 6: { bool b = false; if (!ParseBool(text, &b)) return false; *(uint8_t*)dst = b ? 1 : 0; return true; }
    case 1:
    case 2:
    case 3:
    case 4:
    case 8:
    case 9:
    case 0x11:
    case 0x12:
    case 0x13:
    case 0x14:
    case 0x15:
    case 0x16:
    case 0x17:
    case 0x18:
    case 0x1C:
    case 0x1D:
    case 0x1E: {
        if (!text || !*text) return false;
        if (column.enumNames && column.enumCount) {
            // Enum name table (original ColumnDesc+0x38/+0x40): index is the stored value.
            if (!LooksNumeric(text)) {
                bool found = false;
                for (uint16_t i = 0; i < column.enumCount; ++i) {
                    if (EqualsIgnoreCase(text, column.enumNames[i])) {
                        value = i;
                        found = true;
                        break;
                    }
                }
                if (!found) return false;
            } else if (!ParseSigned(text, -0x7fffffffffffffffLL, 0x7fffffffffffffffLL, &value)) {
                return false;
            }
        } else if (!ParseSigned(text, column.minValue, column.maxValue, &value)) {
            return false;
        }
        WriteInt(dst, column.elementSize ? column.elementSize : 4, value);
        return true;
    }
    case 5: {
        if (!text || !*text) return false;
        wchar_t trimmed[64];
        TrimWide(trimmed, 64, text);
        *(float*)dst = (float)_wtof(trimmed);
        return true;
    }
    case 7:
    case 0x0A: {
        int maxChars = (column.maxValue > 0 && column.maxValue < 0x10000)
                       ? (int)column.maxValue : 0;
        return AssignString(dst, text, maxChars);
    }
    case 0x0B: {
        // 16-byte structured value (e.g. required-attraction): store the text bytes.
        if (!text) return false;
        size_t n = wcslen(text) * sizeof(wchar_t);
        if (n > 16) n = 16;
        std::memset(dst, 0, 16);
        std::memcpy(dst, text, n);
        return true;
    }
    default: {
        // Structured converters we do not interpret semantically: store an integer when the
        // attribute is numeric, otherwise copy text into the field. Optional columns tolerate
        // failure (handled by the caller).
        if (text && LooksNumeric(text)) {
            if (!ParseSigned(text, -0x7fffffffffffffffLL, 0x7fffffffffffffffLL, &value))
                return false;
            WriteInt(dst, column.elementSize ? column.elementSize : 4, value);
            return true;
        }
        return false;
    }
    }
}

bool IElReader::ReadColumns(blood::IXmlElement* element, const ColumnDesc* columns, int count,
                            unsigned char* record, IAliasMap*) {
    if (!element || !columns || !record) return false;
    for (int i = 0; i < count; ++i) {
        const ColumnDesc& column = columns[i];
        uint8_t n = column.count ? column.count : 1;
        for (uint8_t k = 0; k < n; ++k) {
            wchar_t name[128];
            AttributeName(name, 128, column, k);
            const wchar_t* text = element->GetAttribute(name);
            void* dst = record + column.offset + (size_t)k * column.elementSize;
            if (!text || !*text) {
                if (column.required) {
                    if (column.defaultValue && *column.defaultValue) {
                        if (!ConvertValue(column.type, dst, column.defaultValue, column, k, NULL))
                            return false;
                        continue;
                    }
                    return false;
                }
                if (k < column.minCount) break;
                if (column.defaultValue && *column.defaultValue) {
                    if (!ConvertValue(column.type, dst, column.defaultValue, column, k, NULL))
                        return false;
                }
                continue;
            }
            if (!ConvertValue(column.type, dst, text, column, k, NULL)) {
                if (column.required) return false;
                if (k < column.minCount) break;
            }
        }
    }
    return true;
}

bool IElReader::HandleAlias(blood::IXmlElement* element, const TableSchemaDesc* schema,
                            const ElementDesc& elementDesc, unsigned char* record,
                            IAliasMap* aliases) {
    if (!schema || !aliases || !elementDesc.set || !elementDesc.set->columns) return true;
    const ColumnDesc* aliasColumn = NULL;
    for (uint16_t i = 0; i < elementDesc.set->columnCount; ++i) {
        if (EqualsIgnoreCase(elementDesc.set->columns[i].name, L"alias")) {
            aliasColumn = &elementDesc.set->columns[i];
            break;
        }
    }
    if (!aliasColumn) return true;
    const wchar_t* alias = *(wchar_t**)(record + aliasColumn->offset);
    if (!alias || !*alias) return aliasColumn->required == 0;

    size_t valueOffset = elementDesc.set->keyOffset;
    if (EqualsIgnoreCase(schema->alias, L"achievement-register")) {
        // The daemon looks registers up by the alias string pointer (DataManager+0xE0, vt+0x70).
        valueOffset = aliasColumn->offset;
    }
    uint64_t value = 0;
    size_t capacity = schema->recordSize ? schema->recordSize : 0x100;
    if (valueOffset && valueOffset + 8 <= capacity)
        std::memcpy(&value, record + valueOffset, 8);
    std::wstring key = MakeAliasKey(schema->alias, alias);
    aliases->Add(key.c_str(), value);
    return true;
}

bool IElReader::ReadRecord(blood::IXmlElement* element, const TableSchemaDesc* schema,
                           void* record, size_t capacity, IAliasMap* aliases, void*) {
    if (!element || !schema || !record) return false;
    int8_t elemIndex = FindElement(element, schema, aliases, record);
    if (elemIndex < 0) return false;
    const ElementDesc& desc = schema->elements[elemIndex];
    if (!desc.set) return false;
    size_t total = schema->recordSize ? schema->recordSize : (6 + desc.set->dataSize);
    if (capacity < total) return false;
    unsigned char* bytes = (unsigned char*)record;
    std::memset(bytes + 6, 0, total - 6);
    bytes[0] = desc.kind;
    *(int16_t*)(bytes + 2) = -1;
    *(uint16_t*)(bytes + 4) = desc.set->dataSize;

    if (desc.set->columnCount &&
        !ReadColumns(element, desc.set->columns, desc.set->columnCount, bytes, aliases))
        return false;
    if (desc.set->childCount) {
        const wchar_t* type = element->GetAttribute(L"type");
        if (type && *type) {
            for (uint16_t i = 0; i < desc.set->childCount; ++i) {
                const ChildDesc& child = desc.set->children[i];
                if (EqualsIgnoreCase(type, child.name)) {
                    *(int16_t*)(bytes + 2) = (int16_t)i;
                    if (child.columns && child.columnCount &&
                        !ReadColumns(element, child.columns, child.columnCount, bytes, aliases))
                        return false;
                    break;
                }
            }
        }
    }
    HandleAlias(element, schema, desc, bytes, aliases);
    return true;
}

bool IElReader::ReadRecordWithDefault(blood::IXmlElement* element, const TableSchemaDesc* schema,
                                      void* record, size_t capacity, IAliasMap* aliases,
                                      void* context) {
    return ReadRecord(element, schema, record, capacity, aliases, context);
}

void IElReader::CollectStringOffsets(const TableSchemaDesc* schema, std::vector<uint16_t>* out) {
    if (!schema || !out) return;
    for (int i = 0; i < schema->elementCount; ++i) {
        const ColumnSet* set = schema->elements[i].set;
        if (!set || !set->columns) continue;
        for (uint16_t c = 0; c < set->columnCount; ++c) {
            const ColumnDesc& col = set->columns[c];
            if (col.type == 7 || col.type == 0x0A) {
                for (uint8_t k = 0; k < (col.count ? col.count : 1); ++k) {
                    uint16_t offset = (uint16_t)(col.offset + k * col.elementSize);
                    if (std::find(out->begin(), out->end(), offset) == out->end())
                        out->push_back(offset);
                }
            }
        }
        for (uint16_t ch = 0; ch < set->childCount; ++ch) {
            const ChildDesc& child = set->children[ch];
            if (!child.columns) continue;
            for (uint16_t c = 0; c < child.columnCount; ++c) {
                const ColumnDesc& col = child.columns[c];
                if (col.type == 7 || col.type == 0x0A) {
                    for (uint8_t k = 0; k < (col.count ? col.count : 1); ++k) {
                        uint16_t offset = (uint16_t)(col.offset + k * col.elementSize);
                        if (std::find(out->begin(), out->end(), offset) == out->end())
                            out->push_back(offset);
                    }
                }
            }
        }
    }
}

int IElReader::ElementSize(void* record, const TableSchemaDesc* schema) const {
    size_t total = schema && schema->recordSize ? schema->recordSize : 0;
    std::vector<uint16_t> offsets;
    CollectStringOffsets(schema, &offsets);
    unsigned char* bytes = (unsigned char*)record;
    for (size_t i = 0; i < offsets.size(); ++i) {
        wchar_t* s = *(wchar_t**)(bytes + offsets[i]);
        if (s) total += (wcslen(s) + 1) * sizeof(wchar_t);
    }
    return (int)total;
}

bool IElReader::PackStrings(void* record, const TableSchemaDesc* schema, unsigned char* buffer,
                            size_t* cursor, bool copyFlag) {
    if (!record || !schema || !buffer || !cursor) return false;
    std::vector<uint16_t> offsets;
    CollectStringOffsets(schema, &offsets);
    unsigned char* bytes = (unsigned char*)record;
    for (size_t i = 0; i < offsets.size(); ++i) {
        wchar_t* s = *(wchar_t**)(bytes + offsets[i]);
        if (!s) continue;
        size_t n = (wcslen(s) + 1) * sizeof(wchar_t);
        std::memcpy(buffer + *cursor, s, n);
        if (!copyFlag) { delete[] s; *(wchar_t**)(bytes + offsets[i]) = NULL; }
        *(uint64_t*)(bytes + offsets[i]) = (uint64_t)*cursor;
        *cursor += n;
    }
    return true;
}

void IElReader::ClearOwnedStrings(void* record, const TableSchemaDesc* schema) {
    if (!record || !schema) return;
    std::vector<uint16_t> offsets;
    CollectStringOffsets(schema, &offsets);
    unsigned char* bytes = (unsigned char*)record;
    for (size_t i = 0; i < offsets.size(); ++i) {
        wchar_t* s = *(wchar_t**)(bytes + offsets[i]);
        if (s) { delete[] s; *(wchar_t**)(bytes + offsets[i]) = NULL; }
    }
}

void IElReader::UnpackStrings(void*, const TableSchemaDesc*) {
    // Tables keep records unpacked; no-op here (binary cache path is unused).
}

bool IElReader::RelocateStrings(void* record, const TableSchemaDesc* schema, int64_t delta) {
    if (!record || !schema || !delta) return true;
    std::vector<uint16_t> offsets;
    CollectStringOffsets(schema, &offsets);
    unsigned char* bytes = (unsigned char*)record;
    for (size_t i = 0; i < offsets.size(); ++i) {
        wchar_t* s = *(wchar_t**)(bytes + offsets[i]);
        if (s) *(wchar_t**)(bytes + offsets[i]) = (wchar_t*)((char*)s + delta);
    }
    return true;
}

} // namespace datareader
} // namespace achievement
