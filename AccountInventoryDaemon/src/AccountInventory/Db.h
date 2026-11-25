// AccountInventoryDaemon - ODBC database layer (original: AccountInventoryDB.cpp / Db.cpp).
#pragma once
#include "../BloodLib/BloodLib.h"
#include <sql.h>
#include <sqlext.h>
#include "Config.h"

namespace accountinventory {

class Db {
public:
    Db();
    ~Db();
    bool Connect(const std::string& connectionString);
    void Disconnect();
    bool IsConnected() const { return connected_; }
    // {?=CALL dbo.usp_CheckWorldId(?)} : returns true when the proc returns 0.
    bool CheckWorldId(int worldId, int* dbRetValue);

private:
    SQLHENV env_;
    SQLHDBC dbc_;
    bool connected_;
};

} // namespace accountinventory
