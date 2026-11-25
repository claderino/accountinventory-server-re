// datareader - packed records, keyed tables and iterators (original: AssociativeTable.cpp).
// The C++ record structs have the exact application-visible byte offsets decoded from the
// static schema descriptors; the remaining bytes of the 0x2AF8/0x44C/0x7D00 blocks are tail
// storage for fields the daemon does not read directly.
#pragma once
#include "ElReader.h"
#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

namespace achievement {
namespace datareader {

class DrTable;
class IIterator;

#pragma pack(push, 1)   // original packed blocks are not multiples of 8 (register = 0x44C)

// 0x2AF8 bytes. The first 6 bytes are the private record header written by ElReader.
struct AchievementRecord {
    unsigned char header[8];
    uint16_t id;                              // +0x08
    uint16_t step;                            // +0x0A
    uint8_t job;                              // +0x0C
    uint8_t reserved0[3];
    wchar_t* alias;                           // +0x10
    uint8_t deprecated;                       // +0x18
    uint8_t completeFromClient;               // +0x19
    uint8_t reserved1[6];
    wchar_t* registerRef[5];                  // +0x20
    uint8_t registerType[5];                  // +0x48
    uint8_t reserved2[3];
    int32_t registerValue[5];                 // +0x50
    uint8_t progressShow;                     // +0x64
    uint8_t progressStepRegisterClear[5];     // +0x65
    uint8_t reserved3[6];
    wchar_t* stepCompleteRewardItem[2];       // +0x70
    int16_t stepCompleteRewardItemCount[2];   // +0x80
    uint8_t reserved4[4];
    int64_t stepCompleteRewardGameCash;       // +0x88
    uint8_t stepCompleteRewardType;           // +0x90
    uint8_t reserved5[7];
    wchar_t* stepCompleteRewardFinalItem[2];  // +0x98
    int16_t stepCompleteRewardFinalItemCount[2]; // +0xA8
    int16_t currentStepScore;                 // +0xAC
    int16_t scoreSum;                         // +0xAE
    unsigned char tail[0x2AF8 - 0xB0];
};

struct KillPcVariant {
    wchar_t* faction;                         // +0x68
    uint8_t race;                             // +0x70
    uint8_t targetJob;                        // +0x71
    int8_t aboveLevel;                        // +0x72
    int8_t belowLevel;                        // +0x73
    int32_t zone[4];                          // +0x74
};

// 0x44C bytes. `type=` selects one of 56 variant layouts at +0x68; the subtype index is also
// stored in the private header at +2, as the daemon reads it there.
struct AchievementRegisterRecord {
    unsigned char header[8];
    uint16_t id;                              // +0x08
    uint8_t job;                              // +0x0A
    uint8_t reserved0[5];
    wchar_t* alias;                           // +0x10
    uint8_t reserved1[4];                     // +0x14..0x17
    uint16_t version;                         // +0x18
    uint8_t reserved2[2];
    int32_t maxValue;                         // +0x1C
    uint8_t forNewbieCare;                    // +0x20
    uint8_t persistant;                       // +0x21
    uint8_t incrementFromClient;              // +0x22
    uint8_t progressShow;                     // +0x23
    uint64_t achievement[8];                  // +0x28, filled by DataManager (not XML)
    union {
        unsigned char variant[0x44C - 0x68];  // +0x68
        KillPcVariant killPc;
    };
};

struct QuestRecord {
    unsigned char header[8];
    uint16_t id;                              // +0x08
    uint8_t reserved0[6];
    wchar_t* alias;                           // +0x10
    uint8_t category;                         // +0x18
    uint8_t resetType;                        // +0x19
    uint8_t resetByAcquireTime;               // +0x1A
    uint8_t reserved2[5];
    unsigned char tail[0x7D00 - 0x20];
};

#pragma pack(pop)

static_assert(sizeof(AchievementRecord) == 0x2AF8, "achievement record size");
static_assert(sizeof(AchievementRegisterRecord) == 0x44C, "register record size");
static_assert(sizeof(QuestRecord) == 0x7D00, "quest record size");
static_assert(offsetof(AchievementRecord, id) == 0x08, "achievement id offset");
static_assert(offsetof(AchievementRecord, scoreSum) == 0xAE, "achievement scoreSum offset");
static_assert(offsetof(AchievementRegisterRecord, alias) == 0x10, "register alias offset");
static_assert(offsetof(AchievementRegisterRecord, achievement) == 0x28, "register back-pointer offset");
static_assert(offsetof(QuestRecord, id) == 0x08, "quest id offset");

// Iterator interface (vt slots: +0 Get, +8 Next, +0x10 IsValid, +0x18 Release).
class IIterator {
public:
    void* Get() const;
    void Next();
    bool IsValid() const;
    void Release();
    void** vt;
private:
    friend class DrTable;
    IIterator(DrTable* table, size_t index, size_t count);
    DrTable* table_;
    size_t index_;
    size_t count_;
};

// Shared table implementation. `vt` is an explicit slot array so both typed C++ calls and the
// original `table->vt[0x70](table, key)` calling convention work. Slots used by the daemon:
// +0x18 LoadXml, +0x20 LoadBinary, +0x28 GetInfo, +0x60 SetKeyName, +0x68 FindByAlias,
// +0x70 FindByKey, +0x78 SetOwnStrings, +0x88 CreateIterator, +0x90 ReleaseIterator.
class DrTable {
public:
    enum Kind { kNullTable = 0, kAssociative = 1, kAutoKey = 2, kQuest = 3 };

    explicit DrTable(Kind kind = kAssociative);
    ~DrTable();

    bool LoadXml(blood::IXmlDoc** docs, int count, const TableSchemaDesc* schema,
                 IElReader* elReader, IAliasMap* aliases);
    bool LoadBinary(const void* data, size_t size);
    const TableSchemaDesc* GetInfo() const { return schema_; }
    void SetKeyName(const TableSchemaDesc* schema);
    void* FindByAlias(const wchar_t* alias, IAliasMap* aliases) const;
    void* FindByKey(uint64_t key) const;
    void SetOwnStrings(bool value) { ownStrings_ = value; }
    void SetSecondaryKey(bool value) { secondaryKeyPrefix_ = value; }
    IIterator* CreateIterator() const;
    void ReleaseIterator(IIterator* it) const;

    size_t size() const { return records_.size(); }
    void* At(size_t index) const {
        return index < records_.size() ? records_[index] : NULL;
    }
    void Destroy() { delete this; }

    static DrTable* CreateAssociative();
    static DrTable* CreateAutoKey();
    static DrTable* CreateQuest();
    static DrTable* NullTable();

    void** vt;
    Kind kind() const { return kind_; }

private:
    DrTable(const DrTable&);
    DrTable& operator=(const DrTable&);
    void Clear();
    void InsertRecord(unsigned char* record, uint64_t key1, uint64_t key2);

    Kind kind_;
    const TableSchemaDesc* schema_;
    bool ownStrings_;
    bool secondaryKeyPrefix_;
    std::vector<unsigned char*> records_;
    std::unordered_map<uint64_t, unsigned char*> index_;
};

// Thin named subclasses live in AutoKeyTable.h / QuestTable.h; a source-compatible name for
// the associative table is defined below and in AssociativeTable.cpp's factory.
class AssociativeTable : public DrTable {
public:
    AssociativeTable();
};

// Alias for callers that used ranking's shorter names.
typedef AssociativeTable Table;

} // namespace datareader
} // namespace achievement
