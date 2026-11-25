// datareader - sequential-key table (original: AutoKeyTable.cpp, sub_4D7F0).
#include "AutoKeyTable.h"

namespace achievement {
namespace datareader {

AssociativeTable::AssociativeTable() : DrTable(kAssociative) {}
AutoKeyTable::AutoKeyTable() : DrTable(kAutoKey) {}

} // namespace datareader
} // namespace achievement
