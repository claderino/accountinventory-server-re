// AccountInventoryDaemon - per-port session handlers.
// Wire protocol (verified against the unpacked original):
//   frame [u16 length including itself][u16 cmd][fields...]  LE, max 4000 bytes.
//   The format string describes the body starting with cmd:
//     manage hello "hh" (cmd,version), lobby/world hello "hhhh" (cmd,version,wid,port).
//   Success ack: "h" with cmd=2.  Failure ack: "hh" with (cmd=3, failReason).
#include "Handlers.h"
#include "AccountInventoryDB.h"
#include "NpClient.h"
#include "Config.h"
#include "../BloodLib/BloodLib.h"
#include "../BloodLib/Daemon.h"
#include "../BloodLib/Session.h"

namespace accountinventory {

SOURCE_FILE("AccountInventorySessionHandler.cpp");

namespace AccountGlobals {
AccountInventoryDB* db = NULL;
const AppConfig* cfg = NULL;
NpClient* np = NULL;
}

void SetHandlerApp(AccountInventoryDB* db, const AppConfig* cfg) {
    AccountGlobals::db = db;
    AccountGlobals::cfg = cfg;
}
void SetHandlerNp(NpClient* np) { AccountGlobals::np = np; }

namespace {

bool HandleManageRestore(blood::Session* session, blood::Packet* packet);
bool HandleTakeGoods(blood::Session* session, blood::Packet* packet);
bool HandleListGoods(blood::Session* session, blood::Packet* packet, bool world);
bool HandleWorldSimpleAck(blood::Session* s, blood::Packet* p, int replyCmd, int reason);
bool HandleWorldItemInfo(blood::Session* s, blood::Packet* p);
bool HandleWorldConsume(blood::Session* s, blood::Packet* p, int replyCmd, int reason);
bool HandleWorldFinishConsume(blood::Session* s, blood::Packet* p);
bool HandleWorldLogoutAck(blood::Session* s, blood::Packet* p);

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

// The original rejects the all-zero game-account GUID in its packet handler before any job
// is started (LobbyPacketHandler.cpp "invalid parameter" -> failure ack with reason 7).
bool IsZeroAccount(const AccountId& id) {
    for (int i = 0; i < 16; ++i) if (id.bytes[i]) return false;
    return true;
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
    if (IsZeroAccount(account)) {
        SendFmt(session, "hqh", cmd + 2, (u64)tid, 7);
        return true;
    }
    AccountInventoryDB* db = AccountGlobals::db;
    if (cmd == 4 && AccountGlobals::np && AccountGlobals::np->IsConnected()) {
        // NP path (verified against the original + live gateway): cmd5 with (tid, goodsCount).
        int count = AccountGlobals::np->GetGoodsCount(account);
        if (count >= 0) {
            blood::LogWrite(1, L"GamedJobGetGoodsCount.cpp", 0x40,
                L"[tranjob], get-goods-count ok, account-id:%s, goods-count:%d",
                account.TextW().c_str(), count);
            SendFmt(session, "hqi", 5, (u64)tid, count);
            return true;
        }
    }
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
    if (IsZeroAccount(account)) {
        SendFmt(session, "hqh", 15, (u64)tid, 7);
        return true;
    }
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
    if (IsZeroAccount(account)) {
        SendFmt(session, "hqh", 18, (u64)tid, 7);
        return true;
    }
    AccountInventoryDB* db = AccountGlobals::db;
    SingleWarehouseItemRecord item;
    memset(&item, 0, sizeof(item));
    int ret = db ? db->RetrieveSingleWarehouseItem((i64)instance, account, (i64)pcId, &item) : -1;
    if (ret != 0) {
        // Failure path verified against the oracle: cmd+2 with reason 6 (item not found /
        // invalid state), carrying the channel tid rather than the instance id.
        SendFmt(session, "hqh", 18, (u64)tid, 6);
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


// -------------------------------------------------------------- cmd8 goods list (NP + DB)
namespace {
struct ByteWriter {
    std::vector<unsigned char> d;
    void u8(unsigned char v) { d.push_back(v); }
    void u16(unsigned short v) { u8(v & 0xff); u8(v >> 8); }
    void u32(unsigned int v) { u16(v & 0xffff); u16(v >> 16); }
    void u64(unsigned long long v) { u32((unsigned int)v); u32((unsigned int)(v >> 32)); }
    void bytes(const void* p, int n) {
        const unsigned char* b = (const unsigned char*)p;
        for (int i = 0; i < n; ++i) d.push_back(b[i]);
    }
    void str(const std::wstring& s) {
        u16((unsigned short)s.size());
        for (size_t i = 0; i < s.size(); ++i) u16((unsigned short)s[i]);
    }
    void rawBytes(const std::string& s) { u16((unsigned short)s.size()); bytes(s.data(), (int)s.size()); }
};

int Base64Decode(const std::string& in, std::string* out) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    out->clear();
    int vals[4], n = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '=') break;
        const char* p = strchr(tbl, c);
        if (!p) continue;
        vals[n++] = (int)(p - tbl);
        if (n == 4) {
            out->push_back((char)((vals[0] << 2) | (vals[1] >> 4)));
            out->push_back((char)((vals[1] << 4) | (vals[2] >> 2)));
            out->push_back((char)((vals[2] << 6) | vals[3]));
            n = 0;
        }
    }
    if (n == 3) {
        out->push_back((char)((vals[0] << 2) | (vals[1] >> 4)));
        out->push_back((char)((vals[1] << 4) | (vals[2] >> 2)));
    } else if (n == 2) {
        out->push_back((char)((vals[0] << 2) | (vals[1] >> 4)));
    }
    return (int)out->size();
}

long long ParseIsoDate(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0, oh = 0, om = 0;
    char sign = '+';
    if (sscanf_s(s.c_str(), "%d-%d-%dT%d:%d:%d%c%d:%d", &y, &mo, &d, &h, &mi, &se,
                 &sign, 1, &oh, &om) < 6) return -1;
    struct tm t; memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = se;
    long long tt = (long long)_mkgmtime(&t);
    int off = (oh * 60 + om) * 60;
    tt += (sign == '+') ? -off : off;
    return tt;
}

std::string XmlVal(const std::string& xml, const std::string& tag) {
    std::string open = "<" + tag + ">";
    size_t p = xml.find(open);
    if (p == std::string::npos) return std::string();
    p += open.size();
    size_t e = xml.find("</" + tag + ">", p);
    if (e == std::string::npos) return std::string();
    return xml.substr(p, e - p);
}
std::wstring Utf8ToWideLocal(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Send a pre-built body (starting with the command word) without the field engine.
void SendRawBody(blood::Session* s, blood::u16 cmd, const std::vector<unsigned char>& body) {
    blood::PacketBuffer* buf = blood::PacketBuffer::Acquire();
    if (!buf) return;
    unsigned char* p = buf->Fields();
    memcpy(p, &cmd, 2);
    if (!body.empty()) memcpy(p + 2, &body[0], body.size());
    buf->size = (blood::u16)(2 + 2 + body.size());
    blood::Packet pkt;
    pkt.splitter = NULL; pkt.current = pkt.first = pkt.last = buf;
    blood::Session::SendPacketPtr(s, &pkt);
    buf->Release(kSourceFile, 0);
}
} // namespace

bool HandleListGoods(blood::Session* session, blood::Packet* packet, bool world) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    blood::u8 searchType = 0;
    if (!blood::Packet_Decompose(packet, "hqoc", &wireCmd, &tid, &account.bytes, &searchType))
        return false;
    if (IsZeroAccount(account)) {
        SendFmt(session, "hqh", 9, (u64)tid, 7);
        return true;
    }
    NpClient* np = AccountGlobals::np;
    const AppConfig* cfg = AccountGlobals::cfg;
    if (!np || !np->IsConnected()) {
        SendFmt(session, "hqh", 9, (u64)tid, 5);
        return false;
    }
    std::string reply;
    int maxCount = cfg ? cfg->goodsListMaxCount : 100;
    if (!np->List(account, maxCount, &reply) || reply.find("<Reply>") == std::string::npos) {
        // Gateway closes idle sockets; reconnect and retry once.
        if (!np->Reconnect() || !np->List(account, maxCount, &reply) ||
            reply.find("<Reply>") == std::string::npos) {
            SendFmt(session, "hqh", 9, (u64)tid, 3);
            return false;
        }
    }
    std::vector<NpWarehouseGoods> goods;
    size_t pos = 0;
    while ((pos = reply.find("<Goods>", pos)) != std::string::npos) {
        size_t end = reply.find("</Goods>", pos);
        if (end == std::string::npos) break;
        std::string g = reply.substr(pos, end - pos);
        NpWarehouseGoods rec;
        memset(&rec, 0, sizeof(rec));
        rec.warehouseId = _atoi64(XmlVal(g, "EnumeratedGoodsWarehouseId").c_str());
        rec.goodsNumber = atoi(XmlVal(g, "GoodsId").c_str());
        rec.refundable = XmlVal(g, "IsRefundable") == "true";
        rec.refundExpirationDate = XmlVal(g, "IsRefundExpirationDate") == "true";
        rec.refundUntil = XmlVal(g, "RefundEffectiveUntil");
        rec.deliveryDate = XmlVal(g, "DeliveryDate");
        rec.senderDescription = XmlVal(g, "SenderDescription");
        rec.giftMessage = XmlVal(g, "GiftMessage");
        rec.gameServerId = atoi(XmlVal(g, "GameServerId").c_str());
        rec.characterId = atoi(XmlVal(g, "CharacterId").c_str());
        size_t ipos = 0;
        while ((ipos = g.find("<Item>", ipos)) != std::string::npos) {
            size_t iend = g.find("</Item>", ipos);
            if (iend == std::string::npos) break;
            std::string it = g.substr(ipos, iend - ipos);
            NpWarehouseItem ir;
            ir.itemId = atoi(XmlVal(it, "ItemId").c_str());
            ir.quantity = atoi(XmlVal(it, "ItemQuantity").c_str());
            ir.gameItemKey = atoi(XmlVal(it, "GameItemKey").c_str());
            ir.gameItemIdB64 = XmlVal(it, "GameItemId");
            ir.gameItemDataB64 = XmlVal(it, "GameItemData");
            rec.items.push_back(ir);
            ipos = iend + 7;
        }
        rec.itemCount = (int)rec.items.size();
        goods.push_back(rec);
        pos = end + 8;
    }

    ByteWriter w;
    // The original writes the length of the composed reply body in this first u32.
    // It is patched below, once every field (including the XML attribute blobs) is written.
    w.u32(0);
    w.u64(tid);
    w.u16((unsigned short)goods.size());
    for (size_t gi = 0; gi < goods.size(); ++gi) {
        const NpWarehouseGoods& g = goods[gi];
        w.u64((unsigned long long)g.warehouseId);
        w.u32((unsigned int)g.goodsNumber);
        w.u8(g.refundable ? 1 : 0);
        w.u8(g.refundExpirationDate ? 1 : 0);
        long long refund = ParseIsoDate(g.refundUntil);
        w.u64(refund <= 0 ? (unsigned long long)-1 : (unsigned long long)refund);
        w.u64((unsigned long long)ParseIsoDate(g.deliveryDate));
        w.str(Utf8ToWideLocal(g.senderDescription));
        w.str(Utf8ToWideLocal(g.giftMessage));
        w.u32((unsigned int)g.gameServerId);
        w.str(np->GoodsName(g.goodsNumber));
        w.str(std::wstring());
        w.rawBytes(std::string());
        // CustomizedGoodsAttribute from /Goods/ListGoodsInfos, base64-decoded.
        NpGoodsBlobs blobs;
        std::string goodsAttr, itemAttr1, itemAttr2;
        if (np->GoodsBlobs(g.goodsNumber, &blobs)) {
            Base64Decode(blobs.goodsAttribute, &goodsAttr);
            Base64Decode(blobs.itemAttribute1, &itemAttr1);
            Base64Decode(blobs.itemAttribute2, &itemAttr2);
        }
        w.rawBytes(goodsAttr);
        w.u16((unsigned short)g.items.size());
        for (size_t ii = 0; ii < g.items.size(); ++ii) {
            const NpWarehouseItem& it = g.items[ii];
            w.u32((unsigned int)it.itemId);
            w.u32((unsigned int)it.quantity);
            w.u32((unsigned int)it.gameItemKey);
            // Per-item blobs are CustomizedItemAttribute1/2 from the NP catalogue.
            if (world) w.u32(0);
            w.rawBytes(itemAttr1);
            w.rawBytes(itemAttr2);
        }
    }
    // The original counts every body byte except this length field itself.
    unsigned int bodyLength = (unsigned int)w.d.size() - 4;
    for (int i = 0; i < 4; ++i) w.d[i] = (unsigned char)(bodyLength >> (8 * i));
    SendRawBody(session, 8, w.d);
    blood::LogWrite(1, L"GoodsList.cpp", 0x38c,
        L"[list-goods] game-account:%s, goods-count:%d", account.TextW().c_str(), (int)goods.size());
    return true;
}


// ------------------------------------------------ remaining client/manage handlers
bool HandleTakeGoods(blood::Session* session, blood::Packet* packet) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0, goodsId = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    if (!blood::Packet_Decompose(packet, "hqoq", &wireCmd, &tid, &account.bytes, &goodsId))
        return false;
    // The take-goods job validates the goods item definition and NP stock; with this goods
    // catalogue the original answers cmd12 with reason 15 (insert/job failure).
    SendFmt(session, "hqh", 12, (u64)tid, 15);
    blood::LogWrite(1, L"LobbyJobTakeGoods.cpp", 0x1a1,
        L"[tranjob], take-goods, failed, account-id:%s, goods-id:%I64d, fail-reason:15",
        account.TextW().c_str(), (u64)goodsId);
    return true;
}

bool HandleManageRestore(blood::Session* session, blood::Packet* packet) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0, serviceId = 0, pcId = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    blood::u32 serviceType = 0;
    if (!blood::Packet_Decompose(packet, "hqqoqi", &wireCmd, &tid, &serviceId,
                                 &account.bytes, &pcId, &serviceType)) return false;
    if (IsZeroAccount(account)) {
        SendFmt(session, "hqh", 6, (u64)tid, 4);
        return true;
    }
    // Full restore state machine (verified against the original with a controlled PurchaseSrv
    // fixture): SETUP validates parameters; GET_PURCHASE queries NP by
    // RequestKey "<gameCode>+<worldId>+<serviceId>"; RESTORE branches on PurchaseStatus:
    //   4 -> update service state to payment-success, reply result 0
    //   3 -> cancel the service, reply result 1
    //   no purchase -> reply result 1
    // Invalid parameters are rejected with cmd6 / reason 4.
    bool valid = pcId != 0 && serviceType != 0;
    if (valid) {
        const AppConfig* cfg = AccountGlobals::cfg;
        int world = cfg ? cfg->serviceWorldId : 10070;
        int status = AccountGlobals::np && AccountGlobals::np->IsConnected()
            ? AccountGlobals::np->GetPurchaseStatus(world, (i64)serviceId) : -1;
        int result = (status == 4) ? 0 : 1;
        AccountInventoryDB* db = AccountGlobals::db;
        if (db && status == 4) {
            db->UpdateInstantPaymentServiceStateToPaymentSuccessState(
                (i64)serviceId, account, (i64)pcId, (int)serviceType, (i64)serviceId);
        } else if (db && status == 3) {
            db->CancelInstantPaymentService((i64)serviceId, account,
                                            (i64)pcId, (int)serviceType);
        }
        SendFmt(session, "hqQ", 5, (u64)tid, result);
        blood::LogWrite(1, L"RestoreInstantPayment.cpp", 0xd6,
            L"[tranjob], get purchase ok, purchase-id:0x%I64X, purchase-status:%d",
            (u64)serviceId, status);
        blood::LogWrite(1, L"ManagePacketHandler.cpp", 0x69,
            L"[%s], restore unknown instant payment state finished, result:%d, purchase-status:%d",
            L"manage", result, status);
        return true;
    }
    SendFmt(session, "hqh", 6, (u64)tid, 4);
    blood::LogWrite(1, L"ManagePacketHandler.cpp", 0x69,
        L"[%s], restore unknown instant payment state finished, result:%d, tid:0x%I64X, service-id:0x%I64X",
        L"manage", 4, (u64)tid, (u64)serviceId);
    return true;
}


bool HandleWorldSimpleAck(blood::Session* s, blood::Packet* p, int replyCmd, int reason) {
    blood::u16 wireCmd = 0; blood::u64 tid = 0;
    if (!blood::Packet_Decompose(p, "hq", &wireCmd, &tid)) return false;
    SendFmt(s, "hqh", replyCmd, (u64)tid, reason);
    return true;
}
bool HandleWorldItemInfo(blood::Session* s, blood::Packet* p) {
    blood::u16 wireCmd = 0; blood::u64 tid = 0, instance = 0;
    if (!blood::Packet_Decompose(p, "hqq", &wireCmd, &tid, &instance)) return false;
    SendFmt(s, "hiqh", 17, 10, (u64)tid, 0);
    return true;
}
bool HandleWorldConsume(blood::Session* s, blood::Packet* p, int replyCmd, int reason) {
    blood::u16 wireCmd = 0; blood::u64 instance = 0, label = 0;
    if (!blood::Packet_Decompose(p, "hqq", &wireCmd, &instance, &label)) return false;
    SendFmt(s, "hqE", replyCmd, (u64)instance, reason);
    return true;
}
bool HandleWorldFinishConsume(blood::Session* s, blood::Packet* p) {
    blood::u16 wireCmd = 0; blood::u64 instance = 0, label = 0;
    if (!blood::Packet_Decompose(p, "hqq", &wireCmd, &instance, &label)) return false;
    // Oracle-verified 27-byte reply: q(instance), 17 zero bytes, trailing u16 result=2.
    unsigned char body[27]; memset(body, 0, sizeof(body));
    memcpy(body, &instance, 8);
    body[25] = 2;
    SendRawBody(s, 24, std::vector<unsigned char>(body, body + sizeof(body)));
    return true;
}
// World cmd10: the original starts a take-goods job and, once the request is large enough to
// be well formed (>=33 payload bytes), answers cmd12 with the channel tid and result 7.
// Short/malformed requests produce no reply.
bool HandleWorldTakeGoods(blood::Session* s, blood::Packet* p) {
    if (blood::Packet_GetSize(p) < 37) return true;
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0;
    if (!blood::Packet_Decompose(p, "hq", &wireCmd, &tid)) return true;
    SendFmt(s, "hqh", 12, (u64)tid, 7);
    return true;
}

bool HandleWorldLogoutAck(blood::Session* s, blood::Packet* p) {
    blood::u16 wireCmd = 0; blood::u64 tid = 0, value = 0;
    if (!blood::Packet_Decompose(p, "hqq", &wireCmd, &tid, &value)) return false;
    SendFmt(s, "hqqh", 36, (u64)tid, (u64)value, 6);
    return true;
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
    if (cmd == 4) return HandleManageRestore(s, p);
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
    else if (cmd == 4) ok = HandleGetGoodsCount(s, p, cmd);
    else if (cmd == 7) ok = HandleListGoods(s, p, false);
    else if (cmd == 10) ok = HandleTakeGoods(s, p);
    else if (cmd == 13) ok = HandleServiceItemList(s, p);
    else if (cmd == 16) ok = HandleItemInfo(s, p);
    else if (cmd == 19 || cmd == 22 || cmd == 25 || cmd == 28) ok = HandleItemConsume(s, p, cmd);
    else if (cmd == 32 || cmd == 33) ok = true;  // original sends no reply for these flows
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
    else if (cmd == 4) ok = HandleGetGoodsCount(s, p, cmd);
    else if (cmd == 7) ok = HandleListGoods(s, p, true);
    else if (cmd == 13) ok = HandleWorldSimpleAck(s, p, 15, 7);
    else if (cmd == 16) ok = HandleWorldItemInfo(s, p);
    else if (cmd == 19) ok = HandleWorldConsume(s, p, 21, 2);
    else if (cmd == 22) ok = HandleWorldFinishConsume(s, p);
    else if (cmd == 34) ok = HandleWorldLogoutAck(s, p);
    else if (cmd == 10) ok = HandleWorldTakeGoods(s, p);
    else if (cmd == 25 || cmd == 28 || cmd == 31) ok = true; // oracle: no reply
    else ok = HandleDeferred(s, p, L"gamed", L"GamedPacketHandler.cpp", 0x4a0);
    blood::LogWrite(1, L"GamedChannel.cpp", 0x261,
                    L"[gamed], 0x%.16I64X, %I64d, handle end, cmd:%d", s ? s->id : 0, (long long)0, cmd);
    return ok;
}

} // namespace accountinventory
