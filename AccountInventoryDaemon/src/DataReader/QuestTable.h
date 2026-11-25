// datareader - specialized quest table (original: QuestTableImpl.cpp).
// Quest records are 0x7D00 bytes; the top-level <quest> elements are loaded through the shared
// DrTable implementation, with id/alias keys. Nested quest elements are parsed by the schema
// reader into the record's tail area but have no semantic accessors here (see IMPL note).
#pragma once
#include "AssociativeTable.h"

namespace achievement {
namespace datareader {

class QuestTable : public DrTable {
public:
    QuestTable();
};

} // namespace datareader
} // namespace achievement
