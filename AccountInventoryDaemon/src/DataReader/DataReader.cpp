// datareader - table loading / finalization (original: DataReaderImpl.cpp, sub_3D100..0x3DCA0).
#include "DataReader.h"

#include <cstdlib>
#include <cstring>
#include <new>
#include <vector>

namespace achievement {
namespace datareader {

namespace {

void** ManagerVTable() {
    static void* slots[0x68 / 8] = {0};
    return slots;
}

IElReader* VT_CreateElReader(void*) { return IElReader::Create(); }
void VT_DestroyElReader(void*, void** r) { IElReader::Destroy((IElReader**)r); }
IAliasMap* VT_CreateAliasMap(void*) { return IAliasMap::Create(); }
void VT_DestroyAliasMap(void*, void** m) { IAliasMap::Destroy((IAliasMap**)m); }
DrTable* VT_CreateQuestTable(void*) { return DrTable::CreateQuest(); }
void VT_DestroyQuestTable(void*, void** t) { DrTable* table = (DrTable*)*t; if (table) table->Destroy(); *t = NULL; }
bool VT_LoadXml(void* self, void* xml, void* el, void* tables, int count, void* aliases,
                bool finish, void* reserved) {
    return static_cast<IDataReader*>(self)->LoadXml((blood::IXmlReader*)xml, (IElReader*)el,
        (TableHandle*)tables, count, (IAliasMap*)aliases, finish, reserved);
}
bool VT_LoadTables(void* self, void* xml, void* el, void* tables, int count, void* aliases,
                   bool finish) {
    return static_cast<IDataReader*>(self)->LoadTables((blood::IXmlReader*)xml, (IElReader*)el,
        (TableHandle*)tables, count, (IAliasMap*)aliases, finish);
}
bool VT_Finalize(void* self, void* xml, void* el, void* tables, int count, void* aliases) {
    return static_cast<IDataReader*>(self)->FinalizeTables((blood::IXmlReader*)xml,
        (IElReader*)el, (TableHandle*)tables, count, (IAliasMap*)aliases);
}
bool VT_False(void*, void*, void*, void*, int, void*) { return false; }
void VT_Void(void*, void*, void*, void*, int, void*) {}
void* VT_Null(void*) { return NULL; }

// Setup state (original globals at 0xB6D90..): the logger vtable is stored but not called;
// diagnostics are emitted by the daemon's DataLogger wrapper when it chooses to install one.
struct SetupState {
    int appVersion;
    void* logger;
    void* heap0;
    void* heap1;
    bool initialized;
};
SetupState g_setup = {0, NULL, NULL, NULL, false};

// Releases any DOM vector a previous load left in the handle.
void ReleaseDocs(TableHandle& handle, blood::IXmlReader* xml) {
    if (handle.docs && handle.reserved48 && xml) {
        for (int i = 0; i < handle.docCount; ++i) {
            if (handle.docs[i]) xml->DestroyDoc(handle.docs[i]);
        }
    }
    delete[] handle.docs;
    handle.docs = NULL;
    handle.docCount = 0;
    handle.docCapacity = 0;
    handle.reserved48 = 0;
}

bool IsPatternEmpty(const wchar_t* pattern) { return !pattern || !*pattern; }

} // namespace

IDataReader::IDataReader() : vt(ManagerVTable()) {
    static bool installed = false;
    if (!installed) {
        void** s = ManagerVTable();
        s[0x00 / 8] = (void*)&VT_CreateElReader;
        s[0x08 / 8] = (void*)&VT_DestroyElReader;
        s[0x10 / 8] = (void*)&VT_CreateAliasMap;
        s[0x18 / 8] = (void*)&VT_DestroyAliasMap;
        s[0x20 / 8] = (void*)&VT_CreateQuestTable;
        s[0x28 / 8] = (void*)&VT_DestroyQuestTable;
        s[0x30 / 8] = (void*)&VT_LoadXml;
        s[0x38 / 8] = (void*)&VT_LoadTables;
        s[0x40 / 8] = (void*)&VT_Finalize;
        s[0x48 / 8] = (void*)&VT_False;
        s[0x50 / 8] = (void*)&VT_False;
        s[0x58 / 8] = (void*)&VT_Void;
        s[0x60 / 8] = (void*)&VT_Null;
        installed = true;
    }
}

IDataReader::~IDataReader() {}

IDataReader* IDataReader::Create() { return new (std::nothrow) IDataReader(); }
void IDataReader::Destroy(IDataReader** reader) {
    if (!reader) return;
    delete *reader;
    *reader = NULL;
}

IElReader* IDataReader::CreateElReader() { return IElReader::Create(); }
void IDataReader::DestroyElReader(IElReader** reader) { IElReader::Destroy(reader); }
IAliasMap* IDataReader::CreateAliasMap() { return IAliasMap::Create(); }
void IDataReader::DestroyAliasMap(IAliasMap** map) { IAliasMap::Destroy(map); }
DrTable* IDataReader::CreateQuestTable() { return DrTable::CreateQuest(); }
void IDataReader::DestroyQuestTable(DrTable** table) {
    if (!table) return;
    if (*table) (*table)->Destroy();
    *table = NULL;
}

bool IDataReader::LoadXml(blood::IXmlReader* xml, IElReader* elReader, TableHandle* tables,
                          int count, IAliasMap* aliases, bool finish, void*) {
    if (!tables || !elReader) return false;
    if (!LoadTables(xml, elReader, tables, count, aliases, finish)) return false;

    // Table-level columns (release-module, type, version) are zero in all 21 loaded schemas;
    // the loop is kept for binary compatibility but does no work for this daemon.
    for (int i = 0; i < count; ++i) {
        TableHandle& handle = tables[i];
        if (!handle.tableColumns) continue;
        const ColumnDesc* columns = (const ColumnDesc*)handle.tableColumns;
        // Without a live column count the original loop reads a zero-terminated descriptor list.
        for (int c = 0; columns[c].name; ++c) {
            const wchar_t* value = NULL;
            if (xml) {
                blood::IXmlDoc* doc = handle.docs && handle.docCount ? handle.docs[0] : NULL;
                blood::IXmlElement* root = doc ? doc->GetRootElement() : NULL;
                if (root) value = root->GetAttribute(columns[c].name);
            }
            (void)value;
        }
    }
    if (finish && aliases) aliases->Commit();
    return true;
}

bool IDataReader::LoadTables(blood::IXmlReader* xml, IElReader*, TableHandle* tables,
                             int count, IAliasMap*, bool) {
    if (!tables) return false;
    std::vector<unsigned char> scratch;
    scratch.resize(0x1000);

    for (int i = 0; i < count; ++i) {
        TableHandle& handle = tables[i];
        if (!handle.schema) continue;
        if (!handle.table) {
            const TableSchemaDesc* schema = handle.schema;
            uint32_t flags = schema->flags & 0xFF;
            handle.table = flags
                ? (void*)DrTable::CreateAutoKey()
                : (void*)DrTable::CreateAssociative();
            if (handle.table) {
                ((DrTable*)handle.table)->SetKeyName(schema);
                ((DrTable*)handle.table)->SetOwnStrings(true);
            }
        }
        if (handle.docCount > 0 && handle.docs) continue;   // caller supplied parsed DOMs
        if (!xml || IsPatternEmpty(handle.pattern)) continue;

        blood::IXmlFileIO* fileIO = xml->GetFileIO();
        if (!fileIO) continue;
        ReleaseDocs(handle, xml);
        if (fileIO->Init(handle.directory, handle.pattern, (int)handle.flags) != 0) continue;

        std::vector<blood::IXmlDoc*> docs;
        do {
            int size = fileIO->GetFileSize();
            if (size <= 0) break;
            if ((size_t)size > scratch.size()) scratch.resize((size_t)size);
            int capacity = (int)scratch.size();
            if (fileIO->Read(scratch.data(), &capacity) != 0) break;
            const wchar_t* name = fileIO->GetCurrentFileName();
            blood::IXmlDoc* doc = xml->ReadMemory(scratch.data(), (size_t)capacity, name);
            if (doc) docs.push_back(doc);
        } while (fileIO->OpenNext() == 0);
        fileIO->Close();

        if (!docs.empty()) {
            handle.docs = new (std::nothrow) blood::IXmlDoc*[docs.size()];
            if (handle.docs) {
                for (size_t d = 0; d < docs.size(); ++d) handle.docs[d] = docs[d];
                handle.docCount = (int)docs.size();
                handle.docCapacity = (int)docs.size();
                handle.reserved48 = 1;                 // this library owns the DOMs
            } else {
                for (size_t d = 0; d < docs.size(); ++d) xml->DestroyDoc(docs[d]);
            }
        }
    }
    return true;
}

bool IDataReader::FinalizeTables(blood::IXmlReader* xml, IElReader* elReader,
                                 TableHandle* tables, int count, IAliasMap* aliases) {
    if (!tables) return false;
    for (int i = 0; i < count; ++i) {
        TableHandle& handle = tables[i];
        if (!handle.schema || !handle.table || !handle.docs || handle.docCount <= 0) continue;
        if (!handle.finalized) {
            DrTable* table = (DrTable*)handle.table;
            table->LoadXml(handle.docs, handle.docCount, handle.schema, elReader, aliases);
            handle.finalized = 1;
        }
        ReleaseDocs(handle, xml);
    }
    return true;
}

bool IDataReader::LoadBinaryPairA(void*, void*, TableHandle*, int, IAliasMap*) { return false; }
bool IDataReader::LoadBinaryPairB(void*, void*, TableHandle*, int, IAliasMap*) { return false; }
void IDataReader::LoadBinaryForward(void*, void*, TableHandle*, int, IAliasMap*) {}

const TableSchemaDesc* SchemaForHandle(int index) {
    if (index < 0 || index >= 22) return NULL;
    return kSchemas[index];
}

// --------------------------------------------------------------------------- C exports

extern "C" {

const wchar_t* DataReader_GetInterfaceVersion() { return L"32"; }

bool DataReader_Setup(int appVersion, void* loggerVtable, void* heap0, void* heap1) {
    g_setup.appVersion = appVersion;
    g_setup.logger = loggerVtable;
    g_setup.heap0 = heap0;
    g_setup.heap1 = heap1;
    g_setup.initialized = true;
    return true;
}

void DataReader_Cleanup() {
    g_setup.logger = NULL;
    g_setup.heap0 = NULL;
    g_setup.heap1 = NULL;
    g_setup.initialized = false;
}

IDataReader* DataReader_CreateDataReader() { return IDataReader::Create(); }
void DataReader_DestroyDataReader(IDataReader** reader) { IDataReader::Destroy(reader); }
IElReader* DataReader_CreateElReader() { return IElReader::Create(); }
IAliasMap* DataReader_CreateAliasMap() { return IAliasMap::Create(); }
DrTable* DataReader_CreateQuestTable() { return DrTable::CreateQuest(); }

} // extern "C"

} // namespace datareader
} // namespace achievement
