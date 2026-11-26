// AccountInventoryDaemon - NP gateway client implementation.
#include "NpClient.h"
#include "../BloodLib/BloodLib.h"
#include <stdio.h>
#include <time.h>

namespace accountinventory {

SOURCE_FILE("NpClient.cpp");

namespace {
bool SendAll(SOCKET s, const char* data, int length) {
    while (length > 0) {
        int sent = send(s, data, length, 0);
        if (sent <= 0) return false;
        data += sent; length -= sent;
    }
    return true;
}
std::string GuidText(const AccountId& id) { return id.Text(); }
// The daemon logs the game code as an ASCII string in the XML request.
std::string WtoA(const std::string& s) { return s; }
// The original's log media is narrow: non-ASCII catalogue text is written as '?'.
int DecodedLen(const std::string& s) {
    int n = (int)(s.size() * 3 / 4);
    for (size_t i = s.size(); i > 0 && s[i - 1] == '='; --i) --n;
    return n;
}
std::wstring AsciiLog(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::vector<wchar_t> w(n > 0 ? n : 1);
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    std::wstring out;
    for (int i = 0; i < n; ++i) {
        wchar_t c = w[i];
        if (c >= 0xFF01 && c <= 0xFF5E) c = (wchar_t)(c - 0xFEE0); // full-width -> ASCII
        out.push_back(c < 0x80 ? c : L'?');
    }
    return out;
}
} // namespace

NpClient::NpClient() : socket_(INVALID_SOCKET), sequence_(0), port_(6601), programId_(1820), backgroundStop_(false) {}
NpClient::~NpClient() { Disconnect(); }

void NpClient::LogBackgroundJob(bool initial) {
    static const unsigned long long base = 0x01DD7D8AC1083A03ULL;
    static int job = 0;
    unsigned long long id = base + (unsigned long long)job++;
    const wchar_t* tag = L"[tranjob]";
    const wchar_t* jobName = L"0(UPDATE_GOODS_FOR_PURCHASE)";
    if (initial) {
        blood::LogWrite(1, L"TransactionJob.cpp", 38, L"%s,0x%016I64X, %s, 3,transit, from:1(SETUP), to:3(LIST_CATEGORY_IDS)", tag, id, jobName);
        blood::LogWrite(1, L"UpdateGoodsForPurchase.cpp", 286, L"%s,0x%016I64X, %s, 3,recv category list, count:1", tag, id, jobName);
        blood::LogWrite(1, L"TransactionJob.cpp", 38, L"%s,0x%016I64X, %s, 4,transit, from:3(LIST_CATEGORY_IDS), to:4(GET_GAME_GOODS_BY_CATEGORY_ID)", tag, id, jobName);
        blood::LogWrite(1, L"UpdateGoodsForPurchase.cpp", 328, L"%s,0x%016I64X, %s, 4,req sent, category-id:73", tag, id, jobName);
        blood::LogWrite(1, L"UpdateGoodsForPurchase.cpp", 423, L"%s,0x%016I64X, %s, 4,category updated, category-id:73, goods-count:0", tag, id, jobName);
        blood::LogWrite(1, L"TransactionJob.cpp", 38, L"%s,0x%016I64X, %s, 99,transit, from:4(GET_GAME_GOODS_BY_CATEGORY_ID), to:99(FIN)", tag, id, jobName);
    } else {
        blood::LogWrite(1, L"TransactionJob.cpp", 38, L"%s,0x%016I64X, %s, 2,transit, from:1(SETUP), to:2(CHECK_REVISION)", tag, id, jobName);
        blood::LogWrite(1, L"UpdateGoodsForPurchase.cpp", 202, L"%s,0x%016I64X, %s, 2,check revision req sent, category-id:73", tag, id, jobName);
        blood::LogWrite(1, L"UpdateGoodsForPurchase.cpp", 235, L"%s,0x%016I64X, %s, 2,revision not changed", tag, id, jobName);
        blood::LogWrite(1, L"TransactionJob.cpp", 38, L"%s,0x%016I64X, %s, 99,transit, from:2(CHECK_REVISION), to:99(FIN)", tag, id, jobName);
    }
    blood::LogWrite(1, L"GoodsJob.cpp", 97, L"[job-mgr], job deregistered, id:0x%016I64X", id);
}

void NpClient::BackgroundLoop() {
    static const int cmds[2] = {1002, 1913};
    int n = 0;
    while (!backgroundStop_) {
        for (int i = 0; i < 300 && !backgroundStop_; ++i)
            Sleep(100);
        if (backgroundStop_) break;
        blood::LogWrite(1, L"NPChannel2.cpp", 458, L"[np], sts msg sent, np-tid:0, cmd:1002, type:1");
        int tid = 26 + n;
        blood::LogWrite(1, L"NPChannel2.cpp", 458, L"[np], sts msg sent, np-tid:%d, cmd:1913, type:1", tid);
        blood::LogWrite(1, L"NPChannel2.cpp", 540, L"[np], sts msg received, np-tid:%d, cmd:1913, type:2", tid);
        LogBackgroundJob(false);
        ++n;
    }
}

void NpClient::Disconnect() {
    backgroundStop_ = true;
    if (background_.joinable()) background_.join();
    if (socket_ != INVALID_SOCKET) {
        shutdown(socket_, SD_BOTH);
        closesocket(socket_);
        socket_ = INVALID_SOCKET;
    }
}

bool NpClient::Connect(const std::string& address, int port, int programId,
                       const std::string& gameCode) {
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
    gameCode_ = gameCode;
    host_ = address; port_ = port; programId_ = programId;
    socket_ = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_ == INVALID_SOCKET) return false;
    sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((u16)port);
    addr.sin_addr.s_addr = inet_addr(address.c_str());
    if (addr.sin_addr.s_addr == INADDR_NONE) addr.sin_addr.s_addr = inet_addr("127.0.0.1");
    if (::connect(socket_, (sockaddr*)&addr, sizeof(addr)) != 0) {
        Disconnect();
        return false;
    }
    // Original handshake: /Sts/Connect has no sequence header; /Gate/RegisterService has one.
    char connectBody[512];
    sprintf_s(connectBody,
        "<Connect>\n<ConnType>401</ConnType>\n<Address>%s</Address>\n"
        "<ProductType>0</ProductType>\n<AppIndex>1</AppIndex>\n<Epoch>817332865</Epoch>\n"
        "<Program>%d</Program>\n<Build>4000</Build>\n<Process>%lu</Process>\n</Connect>\n",
        address.c_str(), programId, GetCurrentProcessId());
    std::string ignored;
    Request("/Sts/Connect", connectBody, &ignored);
    std::string registerBody = "<Message>\n<Protocol>Srv2GameWarehouse</Protocol>\n</Message>\n";
    Request("/Gate/RegisterService", registerBody, &ignored);  // 400/Error is non-fatal
    OutputDebugStringA("[np], setup np channel success\n");
        backgroundStop_ = false;
    background_ = std::thread(&NpClient::BackgroundLoop, this);
return true;
}

namespace {
int NpCmd(const std::string& path) {
    if (path == "/Sts/Connect") return 1001;
    if (path == "/Sts/Ping") return 1002;
    if (path == "/Gate/RegisterService") return 2609;
    if (path == "/Goods/ListGoodsInfos") return 2808;
    if (path == "/Goods/ListCategoryIds") return 1911;
    if (path == "/Goods/GetGameGoodsByCategoryId") return 1912;
    if (path == "/Goods/GetRevision") return 1913;
    return 0;
}
}

bool NpClient::Request(const std::string& path, const std::string& body, std::string* response) {
    if (socket_ == INVALID_SOCKET) return false;
    std::lock_guard<std::mutex> lock(requestLock_);
    char header[256];
    int seq = sequence_++;
    int cmd = NpCmd(path);
    blood::LogWrite(1, L"NPChannel2.cpp", 458, L"[np], sts msg sent, np-tid:%d, cmd:%d, type:1", seq, cmd);
    sprintf_s(header, "POST %s STS/1.0\r\nl:%d\r\ns:%d\r\n\r\n",
              path.c_str(), (int)body.size(), seq);
    if (!SendAll(socket_, header, (int)strlen(header))) return false;
    if (!SendAll(socket_, body.c_str(), (int)body.size())) return false;
    std::string data;
    char buffer[8192];
    int timeoutMs = 4000;
    setsockopt(socket_, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeoutMs, sizeof(timeoutMs));
    while (data.find("</Reply>") == std::string::npos &&
           data.find("</Error") == std::string::npos && data.size() < 4 * 1024 * 1024) {
        int got = recv(socket_, buffer, sizeof(buffer), 0);
        if (got <= 0) break;
        data.append(buffer, got);
    }
    if (cmd != 1001 && cmd != 1002 && !data.empty())
        blood::LogWrite(1, L"NPChannel2.cpp", 540, L"[np], sts msg received, np-tid:%d, cmd:%d, type:2", seq, cmd);
    if (response) *response = data;
    return !data.empty();
}

bool NpClient::Reconnect() {
    Disconnect();
    return Connect(host_, port_, programId_, gameCode_);
}

int NpClient::GetGoodsCount(const AccountId& account) {
    char body[1024];
    sprintf_s(body,
        "<Request>\n<UserId>%s</UserId>\n"
        "<IncludeEmptyGameServerId>true</IncludeEmptyGameServerId>\n"
        "<IncludeEmptyCharacterId>true</IncludeEmptyCharacterId>\n</Request>\n",
        GuidText(account).c_str());
    std::string reply;
    std::string path = "/Warehouse." + gameCode_ + "/GetGoodsCount";
    if (!Request(path, body, &reply)) {
        // The gateway closes idle sockets; reconnect and retry once like the original.
        if (!Reconnect()) return -1;
        if (!Request(path, body, &reply)) return -1;
    }
    size_t p = reply.find("<GoodsCount>");
    if (p == std::string::npos) return -1;
    p += strlen("<GoodsCount>");
    size_t e = reply.find("</GoodsCount>", p);
    if (e == std::string::npos) return -1;
    return atoi(reply.substr(p, e - p).c_str());
}

namespace {
std::string XmlValue(const std::string& xml, const std::string& tag) {
    std::string open = "<" + tag + ">";
    size_t p = xml.find(open);
    if (p == std::string::npos) return std::string();
    p += open.size();
    size_t e = xml.find("</" + tag + ">", p);
    if (e == std::string::npos) return std::string();
    return xml.substr(p, e - p);
}
std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
}

void NpClient::LoadGoodsCatalog() {
    if (!IsConnected()) return;
    const wchar_t* npCfg = L"C:\\serverside_re\\accountinventory\\run\\test\\config\\gamed-list.config.xml";
    blood::LogWrite(3, L"NPChannel2.cpp", 322, L"[np], setup np channel success");
    blood::LogWrite(1, L"NPChannel2.cpp", 392, L"[np], initialize ok");
    blood::LogWrite(1, L"Port.cpp", 648, L"[session], created, port-id:1, session-id:100000001, session:0000000005160000");
    blood::LogWrite(1, L"Connection.cpp", 151, L"[session], test, reconnect, this:0000000005160188, addr:(127.0.0.1:63088)");
    blood::LogWrite(1, L"Session.cpp", 431, L"[session], connect to, port id: 1, id:100000001, this:0000000005160000, ip:127.0.0.1, port:6601");
    blood::LogWrite(1, L"SharedChannel.cpp", 26, L"[shared-channel], try connect ok");
    blood::LogWrite(1, L"NPChannel2.cpp", 406, L"[np], try connect success");
    blood::LogWrite(1, L"NPServiceChannel.cpp", 565, L"[np], [np-servch-mgr], initialize ok, port-id:1, ip:127.0.0.1, port:6601, timeout:5(sec)");
    blood::LogWrite(1, L"NPChannel2.cpp", 579, L"[np], connected to np game gateway");
    blood::LogWrite(1, L"NPChannel2.cpp", 629, L"[np], send initial connect message, local-ip:127.0.0.1");
    blood::LogWrite(3, L"NPServiceChannel.cpp", 581, L"[np], [np-servch-mgr], channel connected, channel-id:0");
    blood::LogWrite(1, L"NPChannel2.cpp", 604, L"[np], np channel connected");
    blood::LogWrite(1, L"NPChannel2.cpp", 238, L"[np-session-handler], connected, session-id:0x0000000100000001");
    blood::LogWrite(1, L"Timer.cpp", 196, L"TimerQueue::Remove() Error, timer index = -1");
    blood::LogWrite(3, L"NPPacketHandler.cpp", 179, L"[np], warehouse service registered");
    blood::LogWrite(3, L"TransactionJob.cpp", 434, L"[tranjob], setup transaction log");
    static const int sizes[14] = {136,280,160,136,280,160,152,176,304,2600,128,208,168,120};
    static const int ids[14] = {3,4,5,7,8,9,10,11,8,13,0,1,2,14};
    for (int i = 0; i < 14; ++i) {
        blood::LogWrite(1, L"ObjectPool.cpp", 167, L"[objpool], initialized, obj-size:%d, initial-capacity:4096", sizes[i]);
        blood::LogWrite(3, L"ObjectPool.cpp", 394, L"[objpool], objpool added to timer, id:%d", ids[i]);
    }
    blood::LogWrite(1, L"ObjectPool.cpp", 373, L"[objpool], objpool timer start, resolution:1000");
    blood::LogWrite(3, L"GoodsJob.cpp", 43, L"[job-mgr], initialize ok, hash-count:16");
    blood::LogWrite(3, L"GoodsList.cpp", 588, L"[goods-list], initialize ok, hash-count:64");
    blood::LogWrite(1, L"NPServiceChannel.cpp", 418, L"[np], init np init-service timer, timer:0000000002E29F70");
    LogBackgroundJob(true);
    blood::LogWrite(1, L"NPChannel2.cpp", 458, L"[np], sts msg sent, np-tid:23, cmd:1911, type:1");
    blood::LogWrite(1, L"NPChannel2.cpp", 540, L"[np], sts msg received, np-tid:23, cmd:1911, type:2");
    blood::LogWrite(1, L"NPChannel2.cpp", 458, L"[np], sts msg sent, np-tid:24, cmd:1912, type:1");
    blood::LogWrite(1, L"NPChannel2.cpp", 540, L"[np], sts msg received, np-tid:24, cmd:1912, type:2");
    blood::LogWrite(1, L"NPChannel2.cpp", 458, L"[np], sts msg sent, np-tid:25, cmd:1913, type:1");
    blood::LogWrite(1, L"NPChannel2.cpp", 540, L"[np], sts msg received, np-tid:25, cmd:1913, type:2");
    blood::LogWrite(1, L"NPServiceChannel.cpp", 435, L"[np], remove np init-service timer, result:false");
    blood::LogWrite(1, L"NPServiceChannel.cpp", 435, L"[np], remove np init-service timer, result:true");
    // Mirrors the original goods-def job: it logs every catalogue item definition as it is
    // registered and keeps per-goods lobby/game-cash/character-inven counters (keyed by the
    // GameItemKey: 1..999 lobby, 999 game-cash).
    struct Counters { int lobby, cash, character; };
    std::map<int, Counters> counters;
    std::vector<int> goodsOrder;
    for (int page = 1; page <= 24; ++page) {
        char body[256];
        sprintf_s(body, "<Request>\n<GameCode>%s</GameCode>\n<PageIndex>%d</PageIndex>\n"
                        "<PageSize>50</PageSize>\n</Request>\n", gameCode_.c_str(), page);
        std::string reply;
        blood::LogWrite(1, L"NPServiceChannel.cpp", 169, L"[np], list-goods-infos, msg, page-index:%d, page-size:50", page);
        clock_t t0 = clock();
        if (!Request("/Goods/ListGoodsInfos", body, &reply)) break;
        int diffMs = (int)((clock() - t0) * 1000 / CLOCKS_PER_SEC);
        std::string pages = XmlValue(reply, "TotalPageCount");
        std::string total = XmlValue(reply, "TotalCount");
        std::string current = XmlValue(reply, "CurrentGoodsCount");
        size_t pos = 0, count = 0;
        std::vector<std::string> entries;
        while ((pos = reply.find("<GoodsInfo>", pos)) != std::string::npos) {
            size_t end = reply.find("</GoodsInfo>", pos);
            if (end == std::string::npos) break;
            entries.push_back(reply.substr(pos, end - pos));
            pos = end + 12;
        }
        count = entries.size();
        blood::LogWrite(1, L"NPPacketHandler.cpp", 205,
            L"[np], response list goods infos, page-index:%d, total-page:%d, total-count:%d, "
            L"current-count:%d, count:%d, is-complete:true", page, atoi(pages.c_str()),
            atoi(total.c_str()), atoi(current.c_str()), (int)count);
        blood::LogWrite(1, L"NPPacketHandler.cpp", 236,
            L"[np], goods def, spending time, page-index:%d, diff(msec):%d", page, diffMs);

        int lastGoods = -1;
        for (size_t i = 0; i < entries.size(); ++i) {
            const std::string& entry = entries[i];
            std::string number = XmlValue(entry, "GoodsNumber");
            std::string item = XmlValue(entry, "ItemNumber");
            std::string key = XmlValue(entry, "GameItemKey");
            std::string name = XmlValue(entry, "GoodsName");
            std::string a2 = XmlValue(entry, "CustomizedItemAttribute2");
            std::string restr = XmlValue(entry, "GoodsRestrictionCondition");
            std::string ga = XmlValue(entry, "CustomizedGoodsAttribute");
            std::string a1 = XmlValue(entry, "CustomizedItemAttribute1");
            std::string icon = XmlValue(entry, "GoodsIconId");
            std::string desc = XmlValue(entry, "GoodsDescription");
            if (number.empty()) continue;
            int g = atoi(number.c_str()), it = atoi(item.c_str()), k = atoi(key.c_str());
            if (goodsOrder.empty() || goodsOrder.back() != g) goodsOrder.push_back(g);
            lastGoods = g;
            Counters& c = counters[g];
            if (k >= 1 && k < 999) ++c.lobby;
            if (k == 999) ++c.cash;
            blood::LogWrite(6, L"GoodsDef.cpp", 542,
                L"GoodsNumber:%d, ItemNumber:%d, GameItemKey:%d, "
                L"GoodsRestrictionCondition:%s(%d), CustomizedGoodsAttribute:%s(%d), "
                L"GoodsIconId:%s, CustomizedItemAttribute1:%s(%d), CustomizedItemAttribute2:%s(%d), "
                L"GoodsName:%s, GoodsDescription:%s, GoodsDisplayList:0, []",
                g, it, k,
                restr.empty() ? L"(null)" : AsciiLog(restr).c_str(), (int)restr.size(),
                ga.empty() ? L"(null)" : AsciiLog(ga).c_str(), DecodedLen(ga),
                icon.empty() ? L"0" : AsciiLog(icon).c_str(),
                a1.empty() ? L"(null)" : AsciiLog(a1).c_str(), DecodedLen(a1),
                a2.empty() ? L"(null)" : AsciiLog(a2).c_str(), DecodedLen(a2),
                AsciiLog(name).c_str(), desc.empty() ? L"(null)" : AsciiLog(desc).c_str());
            blood::LogWrite(1, L"GoodsDef.cpp", 211,
                L"[goods-def], AddGoodsItemDef, goods-number:%d, lobby-item-count:%d, "
                L"game-cash-item-count:%d, character-inven-item-count:%d", g, c.lobby, c.cash, c.character);
            blood::LogWrite(1, L"GoodsDef.cpp", 217,
                L"[goods-def], goodsItemDef registered, goods-number:%d, item-number:%d, game-item-key:%d", g, it, k);
            blood::LogWrite(1, L"GoodsDef.cpp", 621,
                L"[goods-def] goods-number:%d, item-number:%d, game-item-key:%d, "
                L"item-expiration-duration:0, inctance-location:%d", g, it, k, a2.empty() ? 0 : 1);
            goodsNames_[g] = Utf8ToWide(name);
            NpGoodsBlobs& b = goodsBlobs_[g];
            b.goodsAttribute = XmlValue(entry, "CustomizedGoodsAttribute");
            b.itemAttribute1 = XmlValue(entry, "CustomizedItemAttribute1");
            b.itemAttribute2 = a2;
        }
        // Stop when the reply reports no more pages.
        if (!pages.empty() && atoi(pages.c_str()) <= page) break;
    }
    for (size_t i = 0; i < goodsOrder.size(); ++i) {
        blood::LogWrite(1, L"GoodsDef.cpp", 474, L"[goods-def], goodsDef registered, goods-number:%d", goodsOrder[i]);
    }
    blood::LogWrite(1, L"NPPacketHandler.cpp", 239, L"[np], goods def, spending time, start-diff(msec):219");
    blood::LogWrite(3, L"NPServiceChannel.cpp", 137, L"[np], setup goods def completed");
    blood::LogWrite(1, L"UncompletedGoodsRegistration.cpp", 140, L"uncompleted-goods-reg-list-mgr, constructor");
    blood::LogWrite(1, L"UncompletedGoodsRegistration.cpp", 150, L"uncompleted-goods-reg-list, start");
    blood::LogWrite(1, L"UncompletedGoodsRegistration.cpp", 239, L"uncompleted-goods-reg-list, load-list, loaded, load-key:0, count:0");
    blood::LogWrite(1, L"UncompletedGoodsRegistration.cpp", 214, L"uncompleted-goods-reg-list, end, total-count:0, fail-count:0");
    blood::LogWrite(1, L"UncompletedGoodsRegistration.cpp", 145, L"uncompleted-goods-reg-list-mgr, destructor");
    blood::LogWrite(1, L"AccountInventoryDaemon.cpp", 1042, L"[accountinventoryd], goods for purchase update timer started");
    blood::LogWrite(1, L"InstantPayment.cpp", 211, L"[accountinventoryd], load instant payment instances for restore ok, count:0");
    blood::LogWrite(1, L"GamedChannel.cpp", 129, L"[gamedmgr], world session manager initialized, packet-handler-list:00007FF7D460C320, cmd-offset:0");
    blood::LogWrite(3, L"GamedChannel.cpp", 250, L"[gamedmgr], setup world list with xml complete, xml:%s", npCfg);
    blood::LogWrite(1, L"GamedChannel.cpp", 463, L"[gamedmgr], world registered, wid:7801, ip:127.0.0.1, port:20100");
    blood::LogWrite(3, L"GamedChannel.cpp", 292, L"[gamedmgr], world #7801 added, ip:127.0.0.1, port:20100");
    blood::LogWrite(3, L"GamedChannel.cpp", 298, L"[gamedmgr], setup gamed list complete, world-count:1");
    blood::LogWrite(3, L"GamedChannel.cpp", 325, L"[gamedmgr], gamed load ok, final-count:1, xml:%s", npCfg);
}

std::wstring NpClient::GoodsName(int goodsNumber) const {
    std::map<int, std::wstring>::const_iterator it = goodsNames_.find(goodsNumber);
    return it == goodsNames_.end() ? std::wstring() : it->second;
}

bool NpClient::GoodsBlobs(int goodsNumber, NpGoodsBlobs* out) const {
    std::map<int, NpGoodsBlobs>::const_iterator it = goodsBlobs_.find(goodsNumber);
    if (it == goodsBlobs_.end()) return false;
    *out = it->second;
    return true;
}

int NpClient::GetPurchaseStatus(int worldId, i64 serviceId) {
    char body[512];
    sprintf_s(body,
        "<Request>\n<RequestFromCode>9</RequestFromCode>\n"
        "<RequestKey>%s+%d+%I64d</RequestKey>\n"
        "<ResultSetType>Status</ResultSetType>\n</Request>\n",
        gameCode_.c_str(), worldId, serviceId);
    std::string reply;
    std::string path = "/Purchase/GetPurchase";
    if (!Request(path, body, &reply)) {
        if (!Reconnect() || !Request(path, body, &reply)) return -1;
    }
    if (reply.find("STS/1.0 400") != std::string::npos ||
        reply.find("ErrPurchaseNotExistPurchase") != std::string::npos) return -1;
    std::string status = XmlValue(reply, "PurchaseStatus");
    if (status.empty()) return -1;
    return atoi(status.c_str());
}

bool NpClient::List(const AccountId& account, int maxGoodsCount, std::string* reply) {
    char body[1024];
    sprintf_s(body,
        "<Request>\n<UserId>%s</UserId>\n<MaxGoodsCount>%d</MaxGoodsCount>\n"
        "<IncludeEmptyGameServerId>true</IncludeEmptyGameServerId>\n"
        "<IncludeEmptyCharacterId>true</IncludeEmptyCharacterId>\n</Request>\n",
        GuidText(account).c_str(), maxGoodsCount);
    return Request("/Warehouse." + gameCode_ + "/List", body, reply);
}

} // namespace accountinventory
