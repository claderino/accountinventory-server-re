// AccountInventoryDaemon - NP gateway client implementation.
#include "NpClient.h"
#include "../BloodLib/BloodLib.h"
#include <stdio.h>

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
} // namespace

NpClient::NpClient() : socket_(INVALID_SOCKET), sequence_(0), port_(6601), programId_(1820) {}
NpClient::~NpClient() { Disconnect(); }

void NpClient::Disconnect() {
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
    return true;
}

bool NpClient::Request(const std::string& path, const std::string& body, std::string* response) {
    if (socket_ == INVALID_SOCKET) return false;
    char header[256];
    int seq = sequence_++;
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
    for (int page = 1; page <= 24; ++page) {
        char body[256];
        sprintf_s(body, "<Request>\n<GameCode>%s</GameCode>\n<PageIndex>%d</PageIndex>\n"
                        "<PageSize>50</PageSize>\n</Request>\n", gameCode_.c_str(), page);
        std::string reply;
        if (!Request("/Goods/ListGoodsInfos", body, &reply)) break;
        size_t pos = 0;
        while ((pos = reply.find("<GoodsInfo>", pos)) != std::string::npos) {
            size_t end = reply.find("</GoodsInfo>", pos);
            if (end == std::string::npos) break;
            std::string entry = reply.substr(pos, end - pos);
            std::string number = XmlValue(entry, "GoodsNumber");
            std::string name = XmlValue(entry, "GoodsName");
            if (!number.empty()) goodsNames_[atoi(number.c_str())] = Utf8ToWide(name);
            pos = end + 12;
        }
        // Stop when the reply reports no more pages.
        std::string pages = XmlValue(reply, "TotalPageCount");
        if (!pages.empty() && atoi(pages.c_str()) <= page) break;
    }
}

std::wstring NpClient::GoodsName(int goodsNumber) const {
    std::map<int, std::wstring>::const_iterator it = goodsNames_.find(goodsNumber);
    return it == goodsNames_.end() ? std::wstring() : it->second;
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
