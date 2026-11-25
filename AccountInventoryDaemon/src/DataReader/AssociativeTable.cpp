// datareader - keyed tables, iterators and packed records (original: AssociativeTable.cpp,
// AutoKeyTable.cpp, QuestTable.cpp storage side).
#include "AssociativeTable.h"

#include <algorithm>
#include <cstring>
#include <new>

namespace achievement {
namespace datareader {

namespace {

void** TableVTable() {
    static void* slots[0xA0 / 8] = {0};
    return slots;
}

bool VT_LoadXml(void* self, void** docs, int count, const TableSchemaDesc* schema,
                void* elReader, void* aliases) {
    return static_cast<DrTable*>(self)->LoadXml((blood::IXmlDoc**)docs, count, schema,
                                                (IElReader*)elReader, (IAliasMap*)aliases);
}
bool VT_LoadBinary(void* self, const void* data, size_t size) {
    return static_cast<DrTable*>(self)->LoadBinary(data, size);
}
void* VT_GetInfo(void* self) {
    return (void*)static_cast<DrTable*>(self)->GetInfo();
}
void VT_SetKeyName(void* self, const TableSchemaDesc* schema) {
    static_cast<DrTable*>(self)->SetKeyName(schema);
}
void* VT_FindByAlias(void* self, const wchar_t* alias, void* aliases) {
    return static_cast<DrTable*>(self)->FindByAlias(alias, (IAliasMap*)aliases);
}
void* VT_FindByKey(void* self, uint64_t key) {
    return static_cast<DrTable*>(self)->FindByKey(key);
}
void VT_SetOwnStrings(void* self, uint8_t value) {
    static_cast<DrTable*>(self)->SetOwnStrings(value != 0);
}
void* VT_CreateIterator(void* self) {
    return static_cast<DrTable*>(self)->CreateIterator();
}
void VT_ReleaseIterator(void* self, void* it) {
    static_cast<DrTable*>(self)->ReleaseIterator((IIterator*)it);
}
void* VT_Null(void*) { return NULL; }
void VT_Void(void*) {}

struct StringOffset {
    uint16_t offset;
    uint8_t isArray;
    uint8_t count;
    uint8_t elementSize;
};

void CollectStringOffsets(const TableSchemaDesc* schema, std::vector<StringOffset>* out) {
    if (!schema) return;
    for (int i = 0; i < schema->elementCount; ++i) {
        const ColumnSet* set = schema->elements[i].set;
        if (!set) continue;
        std::vector<const ColumnDesc*> columns;
        for (uint16_t c = 0; c < set->columnCount && set->columns; ++c)
            columns.push_back(&set->columns[c]);
        for (uint16_t ch = 0; ch < set->childCount && set->children; ++ch) {
            const ChildDesc& child = set->children[ch];
            for (uint16_t c = 0; c < child.columnCount && child.columns; ++c)
                columns.push_back(&child.columns[c]);
        }
        for (size_t c = 0; c < columns.size(); ++c) {
            const ColumnDesc& col = *columns[c];
            if (col.type != 7 && col.type != 0x0A) continue;
            StringOffset so;
            so.offset = col.offset;
            so.isArray = (uint8_t)(col.type == 0x0A || col.count > 1);
            so.count = col.count ? col.count : 1;
            so.elementSize = (uint8_t)col.elementSize;
            bool present = false;
            for (size_t k = 0; k < out->size(); ++k) {
                if ((*out)[k].offset == so.offset) { present = true; break; }
            }
            if (!present) out->push_back(so);
        }
    }
}

void FreeStrings(unsigned char* record, const TableSchemaDesc* schema) {
    if (!record || !schema) return;
    std::vector<StringOffset> offsets;
    CollectStringOffsets(schema, &offsets);
    for (size_t i = 0; i < offsets.size(); ++i) {
        const StringOffset& so = offsets[i];
        if (!so.isArray) {
            wchar_t* s = *(wchar_t**)(record + so.offset);
            if (s) { delete[] s; *(wchar_t**)(record + so.offset) = NULL; }
        } else {
            for (uint8_t k = 0; k < so.count; ++k) {
                size_t at = so.offset + (size_t)k * (so.elementSize ? so.elementSize : 8);
                wchar_t* s = *(wchar_t**)(record + at);
                if (s) { delete[] s; *(wchar_t**)(record + at) = NULL; }
            }
        }
    }
}

void** IteratorVTable() {
    static void* slots[4] = {0};
    return slots;
}
void* VT_IterGet(void* self) { return static_cast<IIterator*>(self)->Get(); }
void VT_IterNext(void* self) { static_cast<IIterator*>(self)->Next(); }
bool VT_IterIsValid(void* self) { return static_cast<IIterator*>(self)->IsValid(); }
void VT_IterRelease(void* self) { static_cast<IIterator*>(self)->Release(); }

} // namespace

// --------------------------------------------------------------------------- IIterator

IIterator::IIterator(DrTable* table, size_t index, size_t count)
    : vt(IteratorVTable()), table_(table), index_(index), count_(count) {
    static bool installed = false;
    if (!installed) {
        void** s = IteratorVTable();
        s[0] = (void*)&VT_IterGet;
        s[1] = (void*)&VT_IterNext;
        s[2] = (void*)&VT_IterIsValid;
        s[3] = (void*)&VT_IterRelease;
        installed = true;
    }
}

void* IIterator::Get() const {
    return (table_ && index_ < count_) ? table_->At(index_) : NULL;
}
void IIterator::Next() { if (index_ < count_) ++index_; }
bool IIterator::IsValid() const { return table_ && index_ < count_ && table_->At(index_) != NULL; }
void IIterator::Release() { table_->ReleaseIterator(this); }

// --------------------------------------------------------------------------- DrTable

DrTable::DrTable(Kind kind)
    : vt(TableVTable()), kind_(kind), schema_(NULL), ownStrings_(true),
      secondaryKeyPrefix_(false) {
    static bool installed = false;
    if (!installed) {
        void** s = TableVTable();
        s[0x00 / 8] = (void*)&VT_Null;              // destructor (managed by C++ delete)
        s[0x08 / 8] = (void*)&VT_Null;
        s[0x10 / 8] = (void*)&VT_Null;
        s[0x18 / 8] = (void*)&VT_LoadXml;
        s[0x20 / 8] = (void*)&VT_LoadBinary;
        s[0x28 / 8] = (void*)&VT_GetInfo;
        s[0x30 / 8] = (void*)&VT_Null;
        s[0x38 / 8] = (void*)&VT_Null;
        s[0x40 / 8] = (void*)&VT_Null;
        s[0x48 / 8] = (void*)&VT_Null;
        s[0x50 / 8] = (void*)&VT_Null;
        s[0x58 / 8] = (void*)&VT_Null;
        s[0x60 / 8] = (void*)&VT_SetKeyName;
        s[0x68 / 8] = (void*)&VT_FindByAlias;
        s[0x70 / 8] = (void*)&VT_FindByKey;
        s[0x78 / 8] = (void*)&VT_SetOwnStrings;
        s[0x80 / 8] = (void*)&VT_Void;
        s[0x88 / 8] = (void*)&VT_CreateIterator;
        s[0x90 / 8] = (void*)&VT_ReleaseIterator;
        installed = true;
    }
}

DrTable::~DrTable() { Clear(); }

void DrTable::Clear() {
    for (size_t i = 0; i < records_.size(); ++i) {
        if (ownStrings_) FreeStrings(records_[i], schema_);
        delete[] records_[i];
    }
    records_.clear();
    index_.clear();
}

DrTable* DrTable::CreateAssociative() { return new (std::nothrow) DrTable(kAssociative); }
DrTable* DrTable::CreateAutoKey() { return new (std::nothrow) DrTable(kAutoKey); }
DrTable* DrTable::CreateQuest() { return new (std::nothrow) DrTable(kQuest); }

DrTable* DrTable::NullTable() {
    static DrTable nullTable(kNullTable);
    return &nullTable;
}

void DrTable::SetKeyName(const TableSchemaDesc* schema) { schema_ = schema; }

void DrTable::InsertRecord(unsigned char* record, uint64_t key1, uint64_t key2) {
    if (!record) return;
    const bool secondary = (key2 != 0 && key2 != key1);
    if (index_.find(key1) != index_.end() ||
        (secondary && index_.find(key2) != index_.end())) {
        // First duplicate wins: discard the incoming record and its temporary strings.
        FreeStrings(record, schema_);
        delete[] record;
        return;
    }
    records_.push_back(record);
    index_[key1] = record;
    if (secondary) index_[key2] = record;
}

bool DrTable::LoadXml(blood::IXmlDoc** docs, int count, const TableSchemaDesc* schema,
                      IElReader* elReader, IAliasMap* aliases) {
    if (!docs || !schema || !elReader || kind_ == kNullTable) return false;
    Clear();
    schema_ = schema;

    size_t recordSize = schema->recordSize ? schema->recordSize : 0x100;
    uint64_t autoKey = 1;
    bool loaded = false;

    for (int d = 0; d < count; ++d) {
        blood::IXmlDoc* doc = docs[d];
        if (!doc) continue;
        blood::IXmlElement* root = doc->GetRootElement();
        if (!root) continue;
        for (blood::IXmlElement* child = root->GetFirstChildElement(); child;
             child = child->GetNextSiblingElement()) {
            unsigned char probe[16];
            std::memset(probe, 0, sizeof(probe));
            int8_t elementIndex = elReader->FindElement(child, schema, aliases, probe);
            if (elementIndex < 0 || !schema->elements[elementIndex].set) continue;
            if (schema->elements[elementIndex].kind == 0) continue;  // <table> metadata

            unsigned char* record = new (std::nothrow) unsigned char[recordSize];
            if (!record) return false;
            std::memset(record, 0, recordSize);
            if (!elReader->ReadRecord(child, schema, record, recordSize, aliases, NULL)) {
                FreeStrings(record, schema);
                delete[] record;
                continue;
            }
            const ColumnSet& set = *schema->elements[elementIndex].set;
            uint64_t key1 = 0;
            if (set.keyOffset && set.keyOffset + 8 <= recordSize)
                std::memcpy(&key1, record + set.keyOffset, 8);
            uint64_t key2 = 0;
            for (uint16_t c = 0; c < set.columnCount && set.columns; ++c) {
                if (set.columns[c].type == 7 && set.columns[c].offset + 8 <= recordSize) {
                    std::memcpy(&key2, record + set.columns[c].offset, 8);
                    break;
                }
            }
            if (kind_ == kAutoKey) {
                key1 = autoKey++;
                key2 = 0;
            } else if (schema->alias &&
                       _wcsicmp(schema->alias, L"achievement-register") == 0) {
                // DataManager resolves register-ref-N by passing the wide-string pointer
                // directly to vt[0x70]. Keep the numeric key as the secondary index.
                uint64_t tmp = key1;
                key1 = key2;
                key2 = tmp;
            }
            InsertRecord(record, key1, key2);
            loaded = true;
        }
    }
    return loaded || records_.empty();
}

bool DrTable::LoadBinary(const void*, size_t) {
    // Optional binary cache (TADBOSLB v5) is never used by this daemon.
    return false;
}

void* DrTable::FindByKey(uint64_t key) const {
    if (kind_ == kNullTable) return NULL;
    if (kind_ == kAutoKey) {
        if (key == 0 || key > records_.size()) return NULL;
        return records_[(size_t)(key - 1)];
    }
    std::unordered_map<uint64_t, unsigned char*>::const_iterator it = index_.find(key);
    return (it == index_.end()) ? NULL : it->second;
}

void* DrTable::FindByAlias(const wchar_t* alias, IAliasMap* aliases) const {
    if (!alias || !aliases || !schema_) return NULL;
    uint64_t value = 0;
    if (!aliases->Find(MakeAliasKey(schema_->alias, alias).c_str(), &value)) return NULL;
    return FindByKey(value);
}

IIterator* DrTable::CreateIterator() const {
    if (kind_ == kNullTable) return NULL;
    return new (std::nothrow) IIterator(const_cast<DrTable*>(this), 0, records_.size());
}

void DrTable::ReleaseIterator(IIterator* it) const {
    delete it;
}

} // namespace datareader
} // namespace achievement
