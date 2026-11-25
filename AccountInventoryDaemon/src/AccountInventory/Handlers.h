// AccountInventoryDaemon - per-port session handlers and wire handlers.
#pragma once
#include "../BloodLib/BloodLib.h"
#include "../BloodLib/Channel.h"
#include "../BloodLib/Packet.h"

namespace accountinventory {

// Command tables recovered from the original binary:
//   manage  : 1, 4                       (table @ 0xCC1D0)
//   lobby   : 1,4,7,10,13,16,19,22,25,28,32,33  (table @ 0xCC210)
//   world   : 1,4,7,10,13,16,19,22,25,28,31,34  (table @ 0xCC320, cmd-offset 0)
class ManageSessionHandler : public blood::SessionHandler {
public:
    bool OnAccept(blood::Session* session) override;
    bool OnConnect(blood::Session* session) override;
    void OnClose(blood::Session* session, bool closeAll = false) override;
    bool PacketHandler(blood::Session* session, blood::Packet* packet) override;
};

class LobbySessionHandler : public blood::SessionHandler {
public:
    bool OnAccept(blood::Session* session) override;
    bool OnConnect(blood::Session* session) override;
    void OnClose(blood::Session* session, bool closeAll = false) override;
    bool PacketHandler(blood::Session* session, blood::Packet* packet) override;
};

class WorldSessionHandler : public blood::SessionHandler {
public:
    bool OnAccept(blood::Session* session) override;
    bool OnConnect(blood::Session* session) override;
    void OnClose(blood::Session* session, bool closeAll = false) override;
    bool PacketHandler(blood::Session* session, blood::Packet* packet) override;
};

// Hello packet versions verified against the original handlers:
//   manage cmd 1 -> version 1,  lobby cmd 1 -> version 7,  world cmd 1 -> version 8
const blood::u16 kManageHelloVersion = 1;
const blood::u16 kLobbyHelloVersion = 7;
const blood::u16 kWorldHelloVersion = 8;

} // namespace accountinventory
