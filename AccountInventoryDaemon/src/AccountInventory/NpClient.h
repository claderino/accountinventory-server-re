// AccountInventoryDaemon - Next Platform gateway client (STS/1.0 over TCP).
// Wire format recovered from the original daemon via a logging TCP proxy between a private
// copy of the binary and the live gateway:
//   POST <path> STS/1.0\r\n l:<body-length>\r\n s:<seq>\r\n \r\n <xml body>
//   STS/1.0 200 OK\r\n s:<seq>R\r\n l:<len>\r\n \r\n <xml reply>
#pragma once
#include "AccountInventoryDB.h"
#include <winsock2.h>
#include <string>

namespace accountinventory {

class NpClient {
public:
    NpClient();
    ~NpClient();
    bool Connect(const std::string& address, int port, int programId,
                 const std::string& gameCode);
    void Disconnect();
    bool IsConnected() const { return socket_ != INVALID_SOCKET; }

    // POST /Warehouse.<gameCode>/GetGoodsCount ; returns -1 on failure.
    int GetGoodsCount(const AccountId& account);
    // POST /Warehouse.<gameCode>/List ; returns the raw XML reply body.
    bool List(const AccountId& account, int maxGoodsCount, std::string* reply);

private:
    bool Request(const std::string& path, const std::string& body, std::string* response);
    SOCKET socket_;
    int sequence_;
    std::string gameCode_;
    std::string host_;
    int port_;
    int programId_;
};

} // namespace accountinventory
