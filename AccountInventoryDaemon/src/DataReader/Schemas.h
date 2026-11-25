// datareader - static table schema descriptors (generated from the unpacked image).
// Clean C++ equivalent of the original TableSchemaDesc / ElementDesc / ColumnSet / ColumnDesc:
// the same semantics (names, types, counts, ranges, byte offsets, variants) but a different
// in-memory field order. See analysis/IMPL_datareader.md for the mapping.
#pragma once
#include <cstdint>

namespace achievement {
namespace datareader {

// Element of an auxiliary name array (0x10 bytes in the original image): a hashed name id
// followed by the attribute/enum text. Only the text is used by the reimplementation.
struct EnumEntry {
    uint32_t id;
    const wchar_t* name;
};

// One XML attribute -> record field conversion.
struct ColumnDesc {
    const wchar_t* name;            // base name, e.g. L"register-ref" (the XML uses "-1".."-5")
    const EnumEntry* aux;           // indexed names / enum vocabulary, or nullptr
    uint8_t type;                   // conversion id (1..30; see ElReader.cpp)
    uint8_t required;
    uint8_t count;                  // array length (1 = scalar)
    uint8_t minCount;
    int64_t minValue;
    int64_t maxValue;
    const wchar_t* defaultValue;    // compiled default text, or nullptr
    const wchar_t* const* enumNames;// original ColumnDesc+0x38 name table (enum columns)
    uint16_t enumCount;             // original ColumnDesc+0x40
    uint16_t offset;                // byte offset in the record (header included)
    uint16_t elementSize;           // size of one element in bytes
};

// Variant layout selected by the XML `type=` attribute (56 register subtypes, quest cases...).
struct ChildDesc {
    const wchar_t* name;
    const ColumnDesc* columns;
    uint16_t columnCount;
    uint16_t recordSize;            // size of the variant's own data area
};

struct ColumnSet {
    const ColumnDesc* columns;
    const ChildDesc* children;      // may be nullptr
    uint16_t columnCount;
    uint16_t childCount;
    uint16_t dataSize;              // original ColumnSet+0x1C
    uint16_t keyOffset;             // original ColumnSet+0x1E
};

struct ElementDesc {
    const wchar_t* name;            // "table", "record", "quest", ...
    uint8_t kind;                   // original +8: 1 = a record element
    const ColumnSet* set;
};

struct TableSchemaDesc {
    const wchar_t* alias;           // "achievement", "achievement-register", ...
    uint32_t typeId;
    uint32_t version;               // 0xE = data version 0.14, etc.
    const ElementDesc* elements;
    uint8_t elementCount;
    uint16_t recordSize;            // packed block size (achievement 0x2AF8, quest 0x7D00)
    uint32_t flags;                 // nonzero = AutoKeyTable in the original
};

// 22 entries, indexed exactly like DataManager::tables (0 and 4 are unused).
extern const TableSchemaDesc* const kSchemas[22];

// Convenience: schema used by a table handle, or nullptr.
const TableSchemaDesc* SchemaForHandle(int index);

} // namespace datareader
} // namespace achievement
