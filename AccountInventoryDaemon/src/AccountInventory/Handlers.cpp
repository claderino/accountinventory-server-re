// AccountInventoryDaemon - per-port session handlers.
// Wire protocol (verified against the unpacked original):
//   frame [u16 length including itself][u16 cmd][fields...]  LE, max 4000 bytes.
//   The format string describes the body starting with cmd:
//     manage hello "hh" (cmd,version), lobby/world hello "hhhh" (cmd,version,wid,port).
//   Success ack: "h" with cmd=2.  Failure ack: "hh" with (cmd=3, failReason).
#include "Handlers.h"
#include "AccountInventoryDB.h"
#include "Config.h"
#include "../BloodLib/BloodLib.h"
#include "../BloodLib/Daemon.h"
#include "../BloodLib/Session.h"

namespace accountinventory {

SOURCE_FILE("AccountInventorySessionHandler.cpp");

namespace AccountGlobals {
AccountInventoryDB* db = NULL;
const AppConfig* cfg = NULL;
}

void SetHandlerApp(AccountInventoryDB* db, const AppConfig* cfg) {
    AccountGlobals::db = db;
    AccountGlobals::cfg = cfg;
}

namespace {

// MSVC cannot instantiate the framework's variadic Session_SendFmt template (va_start in a
// template), so forward the va_list exactly as the original sub_2A5D0 does.
void SendFmt(blood::Session* session, const char* fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    blood::Session_SendPacket(session, fmt, &ap);
    va_end(ap);
}

void LogAccept(const wchar_t* file, int line, const wchar_t* tag, blood::Session* session) {
    blood::LogWrite(3, file, line, L"[%s], 0x%.16I64X, session accepted", tag,
                    session ? session->id : 0);
}
void LogClose(const wchar_t* file, int line, const wchar_t* tag, blood::Session* session) {
    blood::LogWrite(0, file, line, L"[%s], 0x%.16I64X, session closed", tag,
                    session ? session->id : 0);
}
// Success ack: DAT_315d1c = "h" with value 2.  Failure ack: DAT_315c1c = "hh",(3,reason).
void SendSuccessAck(blood::Session* session) { SendFmt(session, "h", 2); }
void SendFailureAck(blood::Session* session, blood::u16 reason) {
    SendFmt(session, "hh", 3, reason);
}

bool HandleManageHello(blood::Session* session, blood::Packet* packet) {
    blood::u16 cmd = 0, version = 0;
    if (!blood::Packet_Decompose(packet, "hh", &cmd, &version)) {
        blood::LogWrite(0, L"ManagePacketHandler.cpp", 0x1f,
                        L"[%s], 0x%.16I64X, decompose fail", L"manage",
                        session ? session->id : 0);
        return false;
    }
    if (version != kManageHelloVersion) {
        blood::LogWrite(0, L"ManagePacketHandler.cpp", 0x24,
                        L"[%s], 0x%.16I64X, packet version mismatch, mine:%d, yours:%d",
                        L"manage", session ? session->id : 0, kManageHelloVersion, version);
        SendFailureAck(session, 2);
        return false;
    }
    SendSuccessAck(session);
    blood::LogWrite(1, L"ManagePacketHandler.cpp", 0x2b,
                    L"[%s], 0x%.16I64X, hello ok", L"manage",
                    session ? session->id : 0);
    return true;
}

bool HandleLobbyHello(blood::Session* session, blood::Packet* packet) {
    blood::u16 cmd = 0, version = 0, wid = 0, port = 0;
    if (!blood::Packet_Decompose(packet, "hhhh", &cmd, &version, &wid, &port)) {
        blood::LogWrite(0, L"LobbyPacketHandler.cpp", 0x30,
                        L"[%s], failed to decompose packet", L"lobbyd");
        SendFailureAck(session, 2);
        return false;
    }
    if (version != kLobbyHelloVersion) {
        blood::LogWrite(0, L"LobbyPacketHandler.cpp", 0x38,
                        L"[%s], invalid packet version, accountinventory-lobby-packet-version:%d, client version:%d",
                        L"lobbyd", kLobbyHelloVersion, version);
        SendFailureAck(session, 2);
        return false;
    }
    blood::LogWrite(1, L"LobbyPacketHandler.cpp", 0x45,
                    L"[%s], decomposed packet, lobby-packet-version:%d, lobbyWid:%d",
                    L"lobbyd", kLobbyHelloVersion, wid);
    SendSuccessAck(session);
    blood::LogWrite(3, L"LobbyPacketHandler.cpp", 0x55,
                    L"[%s], sent hello success ack, lobbyWid:%d", L"lobbyd", wid);
    return true;
}

bool HandleWorldHello(blood::Session* session, blood::Packet* packet) {
    blood::u16 cmd = 0, version = 0, wid = 0, port = 0;
    if (!blood::Packet_Decompose(packet, "hhhh", &cmd, &version, &wid, &port)) {
        blood::LogWrite(0, L"GamedPacketHandler.cpp", 0x2e,
                        L"[%s], failed to decompose packet", L"gamed");
        SendFailureAck(session, 2);
        return false;
    }
    if (version != kWorldHelloVersion) {
        blood::LogWrite(0, L"GamedPacketHandler.cpp", 0x36,
                        L"[%s], invalid packet version, world-id:%d, accountinventory-game-packet-version:%d, client version:%d",
                        L"gamed", wid, kWorldHelloVersion, version);
        SendFailureAck(session, 2);
        return false;
    }
    blood::LogWrite(1, L"GamedPacketHandler.cpp", 0x42,
                    L"[%s], decomposed packet, game-packet-version:%d, world-id:%d, internal-port:%d",
                    L"gamed", kWorldHelloVersion, wid, port);
    blood::LogWrite(3, L"GamedPacketHandler.cpp", 0x76,
                    L"[%s], hello success, worldId:%d, ip:%s", L"gamed", wid, L"0.0.0.0");
    SendSuccessAck(session);
    return true;
}

// --------------------------------------------------------------------------- DB-backed commands
// Response formats verified against the original binary copy running on private ports:
//   cmd4/7  -> "hqh"  (success cmd 6/9, tid, reason)   reason 5 = NP not available
//   cmd13   -> "hiqiCqqqiQ;" success cmd 14, "hqh" cmd15 on bad params
//   cmd16   -> "hqqqqiQ"  success cmd 17: tid, 0, instance, label, data-id, state
//   cmd19/22/25/28 -> "hqE" (cmd+2, instance-id, result)

bool HandleGetGoodsCount(blood::Session* session, blood::Packet* packet, int cmd) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    if (!blood::Packet_Decompose(packet, "hqo", &wireCmd, &tid, &account.bytes)) return false;
    AccountInventoryDB* db = AccountGlobals::db;
    i64 goods = 0, items = 0;
    int ret = db ? db->RetrieveWarehouseItemCount(account, 1, &goods, &items) : -1;
    // With NP disabled the original replies cmd 6/9 with reason 5 (NP not available).
    SendFmt(session, "hqh", cmd + 2, (u64)tid, ret < 0 ? 7 : 5);
    return true;
}

bool HandleServiceItemList(blood::Session* session, blood::Packet* packet) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0, key = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    blood::u8 searchType = 0;
    if (!blood::Packet_Decompose(packet, "hqoQq", &wireCmd, &tid, &account.bytes,
                                 &searchType, &key)) return false;
    AccountInventoryDB* db = AccountGlobals::db;
    const AppConfig* cfg = AccountGlobals::cfg;
    if (!db || searchType == 0 || (searchType >= 2 && key == 0)) {
        SendFmt(session, "hqh", 15, (u64)tid, 7);
        return false;
    }
    std::vector<LobbyServiceItemRecord> items;
    int ret = db->RetrieveLobbyServiceItemList(account,
        cfg ? cfg->lobbyServiceItemSearchCount : 100, searchType, (i64)key, &items);
    if (ret != 0) {
        SendFmt(session, "hqh", 15, (u64)tid, 3);
        return false;
    }
    SendFmt(session, "hiqih", 14, 14, (u64)tid,
            cfg ? cfg->lobbyServiceItemSearchCount : 100, (int)items.size());
    blood::LogWrite(1, L"LobbyPacketHandler.cpp", 0x183,
        L"[%s], lobby service item, tid:0x%I64X, game-account-id:%s, count:%d",
        L"lobbyd", (u64)tid, account.TextW().c_str(), (int)items.size());
    return true;
}

bool HandleItemInfo(blood::Session* session, blood::Packet* packet) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0, instance = 0, pcId = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    if (!blood::Packet_Decompose(packet, "hqqoq", &wireCmd, &tid, &instance,
                                 &account.bytes, &pcId)) return false;
    AccountInventoryDB* db = AccountGlobals::db;
    SingleWarehouseItemRecord item;
    memset(&item, 0, sizeof(item));
    int ret = db ? db->RetrieveSingleWarehouseItem((i64)instance, account, (i64)pcId, &item) : -1;
    if (ret != 0) {
        SendFmt(session, "hqE", 17, (u64)instance, 3);
        return false;
    }
    SendFmt(session, "hqqqqiQ", 17, (u64)tid, (u64)0, (u64)item.itemInstanceId,
            (u64)item.labelId, (int)item.itemDataId, (int)item.itemState);
    blood::LogWrite(1, L"LobbyPacketHandler.cpp", 0x196,
        L"[%s], lobby service item info, sent success ack, cmd:%d, item-instance-id:%I64d",
        L"lobbyd", 16, (u64)item.itemInstanceId);
    return true;
}

bool HandleItemConsume(blood::Session* session, blood::Packet* packet, int cmd) {
    blood::u16 wireCmd = 0;
    blood::u64 label = 0, instance = 0, unused = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    if (!blood::Packet_Decompose(packet, "hqqqo", &wireCmd, &instance, &label,
                                 &unused, &account.bytes)) return false;
    AccountInventoryDB* db = AccountGlobals::db;
    int ret = -1;
    if (db) {
        if (cmd == 19) ret = db->TryWarehouseItemConsumption((i64)label, (i64)instance, account);
        else if (cmd == 22) ret = db->FinishWarehouseItemConsumption(true, (i64)label, (i64)instance, account);
        else if (cmd == 25) ret = db->CancelWarehouseItemConsumption((i64)label, (i64)instance, account);
        else ret = db->ConsumeWarehouseItem((i64)label, (i64)instance, account);
    }
    // With NP disabled the original always answers reason 7 on this path.
    SendFmt(session, "hqE", cmd + 2, (u64)instance, 7);
    blood::LogWrite(1, L"LobbyPacketHandler.cpp", 0x196,
        L"[%s], consume item, account-id:%s, label-id:%I64d, item-instance-id:%I64d, dbRetValue:%d",
        L"lobbyd", account.TextW().c_str(), (u64)label, (u64)instance, ret);
    return ret == 0;
}

// The remaining commands in the original spawn transactional jobs that talk to the NP gateway
// and the DB. Keep them observable (exact log lines, command ids) and return a failure ack.
bool HandleDeferred(blood::Session* session, blood::Packet* packet, const wchar_t* tag,
                    const wchar_t* file, int line) {
    blood::u16 cmd = blood::Packet_GetCommand(packet);
    blood::LogWrite(1, file, line, L"[%s], 0x%.16I64X, %I64d, handle start, cmd:%d",
                    tag, session ? session->id : 0, (long long)0, cmd);
    SendFmt(session, "hh", (int)cmd + 1, 1);
    blood::LogWrite(1, file, line, L"[%s], 0x%.16I64X, %I64d, handle end, cmd:%d",
                    tag, session ? session->id : 0, (long long)0, cmd);
    return true;
}

} // namespace

// --------------------------------------------------------------------------- manage
bool ManageSessionHandler::OnAccept(blood::Session* s) {
    LogAccept(L"ManageSessionHandler.cpp", 0x15, L"manage", s); return true;
}
bool ManageSessionHandler::OnConnect(blood::Session* s) {
    blood::LogWrite(0, L"ManageSessionHandler.cpp", 0x1b, L"[%s], 0x%.16I64X, connect forbidden",
                    L"manage", s ? s->id : 0);
    return false;
}
void ManageSessionHandler::OnClose(blood::Session* s, bool) {
    LogClose(L"ManageSessionHandler.cpp", 0x21, L"manage", s);
}
bool ManageSessionHandler::PacketHandler(blood::Session* s, blood::Packet* p) {
    blood::u16 cmd = blood::Packet_GetCommand(p);
    if (cmd == 1) {
        blood::LogWrite(1, L"ManageSessionHandler.cpp", 0x3e,
                        L"[%s], 0x%.16I64X, handle start, cmd:%d", L"manage", s ? s->id : 0, cmd);
        bool ok = HandleManageHello(s, p);
        blood::LogWrite(1, L"ManageSessionHandler.cpp", 0x40,
                        L"[%s], 0x%.16I64X, handle end, cmd:%d", L"manage", s ? s->id : 0, cmd);
        return ok;
    }
    if (cmd == 4) return HandleDeferred(s, p, L"manage", L"ManagePacketHandler.cpp", 0x3a);
    blood::LogWrite(0, L"ManageSessionHandler.cpp", 0x38,
                    L"[%s], 0x%.16I64X, handle fail, no such cmd, cmd:%d",
                    L"manage", s ? s->id : 0, cmd);
    return false;
}

// --------------------------------------------------------------------------- lobby
bool LobbySessionHandler::OnAccept(blood::Session* s) {
    LogAccept(L"LobbyChannel.cpp", 0xfa, L"lobbyd", s); return true;
}
bool LobbySessionHandler::OnConnect(blood::Session* s) {
    blood::LogWrite(0, L"LobbyChannel.cpp", 0x100,
                    L"[%s], cannot use LobbySessionHandler::OnConnect handler, sessionId:0x%I64X, portId:%u, sessionIndex:%u",
                    L"lobbyd", s ? s->id : 0, s ? s->portId : 0, 0);
    return false;
}
void LobbySessionHandler::OnClose(blood::Session* s, bool) {
    LogClose(L"LobbyChannel.cpp", 0x102, L"lobbyd", s);
}
bool LobbySessionHandler::PacketHandler(blood::Session* s, blood::Packet* p) {
    blood::u16 cmd = blood::Packet_GetCommand(p);
    static const blood::u16 valid[] = {1,4,7,10,13,16,19,22,25,28,32,33};
    bool known = false;
    for (blood::u16 v : valid) if (v == cmd) { known = true; break; }
    if (!known) {
        blood::LogWrite(0, L"LobbyChannel.cpp", 0x119,
                        L"[lobbyd], 0x%.16I64X, %I64d, handle fail, no such cmd, cmd:%d",
                        s ? s->id : 0, (long long)0, cmd);
        return false;
    }
    blood::LogWrite(1, L"LobbyChannel.cpp", 0x123,
                    L"[lobbyd], 0x%.16I64X, %I64d, handle start, cmd:%d", s ? s->id : 0, (long long)0, cmd);
    bool ok;
    if (cmd == 1) ok = HandleLobbyHello(s, p);
    else if (cmd == 4 || cmd == 7) ok = HandleGetGoodsCount(s, p, cmd);
    else if (cmd == 13) ok = HandleServiceItemList(s, p);
    else if (cmd == 16) ok = HandleItemInfo(s, p);
    else if (cmd == 19 || cmd == 22 || cmd == 25 || cmd == 28) ok = HandleItemConsume(s, p, cmd);
    else ok = HandleDeferred(s, p, L"lobbyd", L"LobbyPacketHandler.cpp", 0x6f);
    blood::LogWrite(1, L"LobbyChannel.cpp", 0x125,
                    L"[lobbyd], 0x%.16I64X, %I64d, handle end, cmd:%d", s ? s->id : 0, (long long)0, cmd);
    return ok;
}

// --------------------------------------------------------------------------- world
bool WorldSessionHandler::OnAccept(blood::Session* s) {
    LogAccept(L"GamedChannel.cpp", 0x2a0, L"gamed", s); return true;
}
bool WorldSessionHandler::OnConnect(blood::Session* s) {
    blood::LogWrite(0, L"GamedChannel.cpp", 0x2a4,
                    L"[%s], 0x%.16I64X, connect forbidden", L"gamed", s ? s->id : 0);
    return false;
}
void WorldSessionHandler::OnClose(blood::Session* s, bool) {
    LogClose(L"GamedChannel.cpp", 0x2a8, L"gamed", s);
}
bool WorldSessionHandler::PacketHandler(blood::Session* s, blood::Packet* p) {
    blood::u16 cmd = blood::Packet_GetCommand(p);
    static const blood::u16 valid[] = {1,4,7,10,13,16,19,22,25,28,31,34};
    bool known = false;
    for (blood::u16 v : valid) if (v == cmd) { known = true; break; }
    if (!known) {
        blood::LogWrite(0, L"GamedChannel.cpp", 0x255,
                        L"[gamed], 0x%.16I64X, %I64d, handle fail, no such cmd, cmd:%d",
                        s ? s->id : 0, (long long)0, cmd);
        return false;
    }
    blood::LogWrite(1, L"GamedChannel.cpp", 0x25f,
                    L"[gamed], 0x%.16I64X, %I64d, handle start, cmd:%d", s ? s->id : 0, (long long)0, cmd);
    bool ok;
    if (cmd == 1) ok = HandleWorldHello(s, p);
    else if (cmd == 4 || cmd == 7) ok = HandleGetGoodsCount(s, p, cmd);
    else ok = HandleDeferred(s, p, L"gamed", L"GamedPacketHandler.cpp", 0x4a0);
    blood::LogWrite(1, L"GamedChannel.cpp", 0x261,
                    L"[gamed], 0x%.16I64X, %I64d, handle end, cmd:%d", s ? s->id : 0, (long long)0, cmd);
    return ok;
}

} // namespace accountinventory
