// datareader - library manager (original: DataReaderImpl.cpp, sub_3D100 / sub_3D7C0 / sub_3DCA0).
// Exposes the five C entry points and the IDataReader vtable used by DataManager.
#pragma once
#include "AssociativeTable.h"
#include "ElReader.h"
#include "Schemas.h"

namespace achievement {
namespace datareader {

// 0x60 bytes, index-aligned with DataManager::tables[22].
struct TableHandle {
    void* table;                       // +0x00 DrTable*
    const TableSchemaDesc* schema;     // +0x08
    const wchar_t* directory;          // +0x10
    const wchar_t* pattern;            // +0x18 (may be ';'-separated)
    uint8_t flags;                     // +0x20 file IO flags
    uint8_t reserved28[7];
    void* helper;                      // +0x28
    void* tableColumns;                // +0x30 (zero for all daemon schemas)
    blood::IXmlDoc** docs;             // +0x38
    int docCount;                      // +0x40
    int docCapacity;                   // +0x48
    int reserved48;                    // +0x48 (1 = docs owned by this library)
    int reserved4C;                    // +0x4C
    void* reserved50;                  // +0x50
    uint8_t finalized;                 // +0x58
    uint8_t reserved59[7];
};
static_assert(sizeof(TableHandle) == 0x60, "TableHandle must be 0x60 bytes");

// IDataReader vtable slots: +0x00 CreateElReader, +0x08 DestroyElReader,
// +0x10 CreateAliasMap, +0x18 DestroyAliasMap, +0x20 CreateQuestTable,
// +0x28 DestroyQuestTable, +0x30 LoadXml, +0x38 LoadTables, +0x40 FinalizeTables,
// +0x48/+0x50/+0x58 binary cache (unused), +0x60 return 0.
class IDataReader {
public:
    IDataReader();
    ~IDataReader();

    IElReader* CreateElReader();
    void DestroyElReader(IElReader** reader);
    IAliasMap* CreateAliasMap();
    void DestroyAliasMap(IAliasMap** map);
    DrTable* CreateQuestTable();
    void DestroyQuestTable(DrTable** table);

    bool LoadXml(blood::IXmlReader* xml, IElReader* elReader, TableHandle* tables,
                 int count, IAliasMap* aliases, bool finish, void* reserved);
    bool LoadTables(blood::IXmlReader* xml, IElReader* elReader, TableHandle* tables,
                    int count, IAliasMap* aliases, bool finish);
    bool FinalizeTables(blood::IXmlReader* xml, IElReader* elReader, TableHandle* tables,
                        int count, IAliasMap* aliases);
    bool LoadBinaryPairA(void*, void*, TableHandle*, int, IAliasMap*);
    bool LoadBinaryPairB(void*, void*, TableHandle*, int, IAliasMap*);
    void LoadBinaryForward(void*, void*, TableHandle*, int, IAliasMap*);

    static IDataReader* Create();
    static void Destroy(IDataReader** reader);

    void** vt;
};

// Five original exports plus the three in-library factories.
extern "C" {
const wchar_t* DataReader_GetInterfaceVersion();
bool DataReader_Setup(int appVersion, void* loggerVtable, void* heap0, void* heap1);
void DataReader_Cleanup();
IDataReader* DataReader_CreateDataReader();
void DataReader_DestroyDataReader(IDataReader** reader);
IElReader* DataReader_CreateElReader();
IAliasMap* DataReader_CreateAliasMap();
DrTable* DataReader_CreateQuestTable();
}

} // namespace datareader
} // namespace achievement
