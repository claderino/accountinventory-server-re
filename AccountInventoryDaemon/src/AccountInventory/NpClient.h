// AccountInventoryDaemon - Next Platform gateway client (STS/1.0 over TCP).
#pragma once
#include "AccountInventoryDB.h"
#include <winsock2.h>
#include <string>
#include <map>
#include <vector>

namespace accountinventory {

struct NpWarehouseItem {
    int itemId;
    int quantity;
    int gameItemKey;
    std::string gameItemIdB64;
    std::string gameItemDataB64;
};
struct NpWarehouseGoods {
    i64 warehouseId;
    int goodsNumber;
    bool refundable;
    bool refundExpirationDate;
    std::string refundUntil;
    std::string deliveryDate;
    std::string senderDescription;
    std::string giftMessage;
    int gameServerId;
    int characterId;
    int itemCount;
    std::vector<NpWarehouseItem> items;
};

class NpClient {
public:
    NpClient();
    ~NpClient();
    bool Connect(const std::string& address, int port, int programId,
                 const std::string& gameCode);
    void Disconnect();
    bool Reconnect();
    bool IsConnected() const { return socket_ != INVALID_SOCKET; }
    int GetGoodsCount(const AccountId& account);
    bool List(const AccountId& account, int maxGoodsCount, std::string* reply);
    // Load the NP goods catalogue (GoodsNumber -> GoodsName) like the original's goods-def job.
    void LoadGoodsCatalog();
    std::wstring GoodsName(int goodsNumber) const;

private:
    bool Request(const std::string& path, const std::string& body, std::string* response);
    SOCKET socket_;
    int sequence_;
    std::string gameCode_;
    std::string host_;
    int port_;
    int programId_;
    std::map<int, std::wstring> goodsNames_;
};

} // namespace accountinventory
