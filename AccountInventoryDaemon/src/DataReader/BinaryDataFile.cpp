// datareader - binary cache stub (original: BinaryDataFile.cpp, sub_4A530 / sub_4A960).
#include "BinaryDataFile.h"

namespace achievement {
namespace datareader {

namespace {
const unsigned char kBinaryMagic[8] = {'T','A','D','B','O','S','L','B'};
const uint8_t kBinaryVersion = 5;
const size_t kBinaryHeaderSize = 0x77;
}

bool BinaryDataFile_LoadMemory(const void* data, size_t size, void*) {
    if (!data || size < kBinaryHeaderSize) return false;
    const unsigned char* p = (const unsigned char*)data;
    for (int i = 0; i < 8; ++i) if (p[i] != kBinaryMagic[i]) return false;
    if (p[8] != kBinaryVersion) return false;
    // Parsing of the remaining sections is deliberately not implemented: no caller needs it.
    return false;
}

bool BinaryDataFile_LoadFile(const wchar_t*, void*) { return false; }

} // namespace datareader
} // namespace achievement
