// datareader - optional binary table cache (UNUSED by AchievementDaemon).
// Original format: magic "TADBOSLB", version byte 5, 0x77-byte header, 11-byte table frames.
// No .bin/.dat files exist in the daemon's data tree, so the implementation is a stub that
// rejects every load. Kept as a separate unit for source-level compatibility.
#pragma once
#include <cstddef>
#include <cstdint>

namespace achievement {
namespace datareader {

struct BinaryDataFileHeader {
    unsigned char bytes[0x77];
};

bool BinaryDataFile_LoadMemory(const void* data, size_t size, void* aliasMap);
bool BinaryDataFile_LoadFile(const wchar_t* path, void* aliasMap);

} // namespace datareader
} // namespace achievement
