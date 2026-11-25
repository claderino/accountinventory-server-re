// AccountInventoryDaemon - ODBC database layer.
#include "Db.h"
#include "../BloodLib/BloodLib.h"
#include <stdio.h>

namespace accountinventory {

SOURCE_FILE("Db.cpp");

Db::Db() : env_(SQL_NULL_HANDLE), dbc_(SQL_NULL_HANDLE), connected_(false) {}

Db::~Db() { Disconnect(); }

void Db::Disconnect() {
    if (dbc_ != SQL_NULL_HANDLE) {
        SQLDisconnect(dbc_);
        SQLFreeHandle(SQL_HANDLE_DBC, dbc_);
        dbc_ = SQL_NULL_HANDLE;
    }
    if (env_ != SQL_NULL_HANDLE) {
        SQLFreeHandle(SQL_HANDLE_ENV, env_);
        env_ = SQL_NULL_HANDLE;
    }
    connected_ = false;
}

bool Db::Connect(const std::string& connectionString) {
    if (SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &env_) != SQL_SUCCESS) return false;
    if (SQLSetEnvAttr(env_, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0) != SQL_SUCCESS)
        return false;
    if (SQLAllocHandle(SQL_HANDLE_DBC, env_, &dbc_) != SQL_SUCCESS) return false;
    SQLCHAR outString[1024];
    SQLSMALLINT outLength = 0;
    SQLRETURN rc = SQLDriverConnectA(dbc_, NULL,
        (SQLCHAR*)connectionString.c_str(), (SQLSMALLINT)connectionString.size(),
        outString, sizeof(outString), &outLength, SQL_DRIVER_NOPROMPT);
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) {
        Disconnect();
        return false;
    }
    connected_ = true;
    return true;
}

bool Db::CheckWorldId(int worldId, int* dbRetValue) {
    if (!connected_) return false;
    SQLHSTMT stmt = SQL_NULL_HANDLE;
    if (SQLAllocHandle(SQL_HANDLE_STMT, dbc_, &stmt) != SQL_SUCCESS) return false;
    const char* query = "{?=CALL dbo.usp_CheckWorldId(?)}";
    SQLRETURN rc = SQLPrepareA(stmt, (SQLCHAR*)query, SQL_NTS);
    if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
        SQLINTEGER ret = 0, in = worldId;
        SQLLEN retInd = 0, inInd = 0;
        SQLBindParameter(stmt, 1, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0,
                         &ret, 0, &retInd);
        SQLBindParameter(stmt, 2, SQL_PARAM_INPUT, SQL_C_SLONG, SQL_INTEGER, 0, 0,
                         &in, 0, &inInd);
        rc = SQLExecute(stmt);
        if (rc == SQL_SUCCESS || rc == SQL_SUCCESS_WITH_INFO) {
            SQLRETURN fr = SQLFetch(stmt);
            if (fr == SQL_SUCCESS || fr == SQL_SUCCESS_WITH_INFO)
                ret = ret; // output parameter already bound
            if (dbRetValue) *dbRetValue = (int)ret;
            SQLFreeHandle(SQL_HANDLE_STMT, stmt);
            return ret == 0;
        }
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return false;
}

} // namespace accountinventory
