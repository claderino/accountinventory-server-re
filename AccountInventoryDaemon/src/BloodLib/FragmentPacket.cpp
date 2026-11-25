// BloodLib - send-side packet fragmentation (original: ..\Packet\FragmentPacket.cpp).
#include "FragmentPacket.h"

namespace blood {
namespace {

const wchar_t* const kFile = L"..\\Packet\\FragmentPacket.cpp";

void FragmentFailure(int line, const wchar_t* message) {
    LogWrite(0, kFile, line, message);
    Assert_FailW(kFile, line, message);
}

} // namespace

static_assert(offsetof(FragmentPacketSplitter, command_) == 0x08, "FragmentPacketSplitter ABI");
static_assert(offsetof(FragmentPacketSplitter, first_) == 0x10, "FragmentPacketSplitter ABI");
static_assert(offsetof(FragmentPacketSplitter, last_) == 0x18, "FragmentPacketSplitter ABI");
static_assert(sizeof(FragmentPacketSplitter) == 0x28, "FragmentPacketSplitter ABI");

FragmentPacketSplitter::FragmentPacketSplitter()
    : command_(0), first_(NULL), last_(NULL), unused_(0) {}

bool FragmentPacketSplitter::AddBuffer(u16* previousHeader, u16* newHeader) {
    if (!command_) {
        FragmentFailure(0x39,
            L"== CRASH == [fragment-packet-splitter], add buffer; command is null");
        return false;
    }
    if (!newHeader) {
        FragmentFailure(0x3F,
            L"== CRASH == [fragment-packet-splitter], add buffer; newHeader is null");
        return false;
    }
    last_ = reinterpret_cast<u32*>(reinterpret_cast<u8*>(newHeader) + *newHeader + 2);
    *reinterpret_cast<u16*>(reinterpret_cast<u8*>(newHeader) + *newHeader) = command_;
    *last_ = 0xFFFFFFFFu;
    *newHeader = static_cast<u16>(*newHeader + 6);
    if (!first_) {
        first_ = last_;
        *last_ = 0;
        return true;
    }
    if (!previousHeader) {
        FragmentFailure(0x51,
            L"== CRASH == [fragment-packet-splitter], add buffer; prev header is null");
        return false;
    }
    *first_ += static_cast<u32>(static_cast<i16>(*previousHeader) - 8);
    return true;
}

bool FragmentPacketSplitter::Finalize(u16* lastHeader) {
    if (!command_ || !first_ || !lastHeader) {
        FragmentFailure(0x5F,
            L"== CRASH == [fragment-packet-splitter], finalize; command or total size is 0");
        return false;
    }
    *first_ += static_cast<u32>(static_cast<i16>(*lastHeader) - 8);
    if (last_ != first_) *last_ = 0;
    first_ = NULL;
    return true;
}

void FragmentPacketSplitter::Clear() {
    first_ = NULL;
    last_ = NULL;
}

} // namespace blood
