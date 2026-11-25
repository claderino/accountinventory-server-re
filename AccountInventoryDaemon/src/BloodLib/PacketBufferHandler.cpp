// BloodLib - trusted receive framing validator (original: PacketBufferHandler.cpp).
#include "PacketBufferHandler.h"

SOURCE_FILE("PacketBufferHandler.cpp");

namespace blood {

// Session owner provides this accessor; kept as an extern so Network/Session headers remain owned
// by the network agent.
u64 Session_GetId(const Session* session);

int TrustedPacketBufferHandler::Validate(Session* session, const u16* header, int available,
                                         PacketBuffer** output) {
    if (available <= 1) return 0;
    i16 size = static_cast<i16>(*header);
    if (size < 2 || size > kMaxPacketSize) {
        u64 id = session ? Session_GetId(session) : 0;
        const wchar_t* message =
            L"== CRASH == [session], TrustedPacketBufferHandler; invalid packet size, id:%I64X, header->size:%d";
        LogWrite(0, L"src\\PacketBufferHandler.cpp", 0x3F, message, id,
                 static_cast<int>(size));
        Assert_FailW(L"src\\PacketBufferHandler.cpp", 0x3F, message, id,
                     static_cast<int>(size));
        return -1;
    }
    if (size > available) return 0;
    if (output) *output = reinterpret_cast<PacketBuffer*>(const_cast<u16*>(header));
    return size;
}

} // namespace blood
