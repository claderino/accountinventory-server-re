// AccountInventoryDaemon - full DB business-logic layer (implementation).
#include "AccountInventoryDB.h"
#include "../BloodLib/BloodLib.h"
#include <stdio.h>
#include <stdarg.h>

namespace accountinventory {

SOURCE_FILE("AccountInventoryDB.cpp");

std::string AccountId::Text() const {
    char buf[64];
    sprintf_s(buf, "%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X",
        (unsigned)((u32)bytes[0] | ((u32)bytes[1] << 8) | ((u32)bytes[2] << 16) | ((u32)bytes[3] << 24)),
        (unsigned)((u16)bytes[4] | ((u16)bytes[5] << 8)),
        (unsigned)((u16)bytes[6] | ((u16)bytes[7] << 8)),
        bytes[8], bytes[9], bytes[10], bytes[11], bytes[12], bytes[13], bytes[14], bytes[15]);
    return std::string(buf);
}

std::wstring AccountId::TextW() const {
    std::string s = Text();
    std::wstring w(s.size(), L'\0');
    for (size_t i = 0; i < s.size(); ++i) w[i] = (wchar_t)(unsigned char)s[i];
    return w;
}

AccountId AccountId::FromWire(const void* p) {
    AccountId a;
    memcpy(a.bytes, p, 16);
    return a;
}

namespace {

// One statement with its own indicator storage per binding (the original shares indicators;
// that corrupts wide strings, so we give each column/parameter its own SQLLEN).
class Stmt {
public:
    explicit Stmt(SQLHDBC dbc) : ok_(false) {
        if (SQLAllocHandle(SQL_HANDLE_STMT, dbc, &h_) == SQL_SUCCESS) ok_ = true;
    }
    ~Stmt() { if (h_ != SQL_NULL_HANDLE) SQLFreeHandle(SQL_HANDLE_STMT, h_); }
    bool Prepare(const wchar_t* sql) {
        return SQLPrepareW(h_, (SQLWCHAR*)sql, SQL_NTS) == SQL_SUCCESS ||
               SQLPrepareW(h_, (SQLWCHAR*)sql, SQL_NTS) == SQL_SUCCESS_WITH_INFO;
    }
    // param 1 of "{?=CALL ...}" is always the procedure return value.
    SQLRETURN BindReturn() {
        ret_ = 0; retInd_ = SQL_NTS;
        ret_ = 0; retInd_ = 0;
        SQLRETURN rc = SQLBindParameter(h_, 1, SQL_RETURN_VALUE, SQL_C_SLONG, SQL_INTEGER,
                                        0, 0, &ret_, 0, &retInd_);
        if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO)
            return SQLBindParameter(h_, 1, SQL_PARAM_OUTPUT, SQL_C_SLONG, SQL_INTEGER,
                                    0, 0, &ret_, 0, &retInd_);
        return rc;
    }
    SQLRETURN BindI32(int index, i32 value) {
        vals_.push_back(new i32(value));
        inds_.push_back(new SQLLEN(sizeof(i32)));
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_INPUT, SQL_C_SLONG,
            SQL_INTEGER, 0, 0, vals_.back(), 0, inds_.back());
    }
    SQLRETURN BindI64(int index, i64 value) {
        vals_.push_back(new i64(value));
        inds_.push_back(new SQLLEN(sizeof(i64)));
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_INPUT, SQL_C_SBIGINT,
            SQL_BIGINT, 0, 0, vals_.back(), 0, inds_.back());
    }
    SQLRETURN BindNull(int index, SQLSMALLINT sqlType) {
        SQLLEN* ind = new SQLLEN(SQL_NULL_DATA);
        inds_.push_back(ind);
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_INPUT, SQL_C_DEFAULT,
            sqlType, 0, 0, NULL, 0, ind);
    }
    SQLRETURN BindI16(int index, i16 value) {
        vals_.push_back(new i16(value));
        inds_.push_back(new SQLLEN(sizeof(i16)));
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_INPUT, SQL_C_SSHORT,
            SQL_SMALLINT, 0, 0, vals_.back(), 0, inds_.back());
    }
    SQLRETURN BindU8(int index, u8 value) {
        vals_.push_back(new i32(value));
        inds_.push_back(new SQLLEN(sizeof(i32)));
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_INPUT, SQL_C_UTINYINT,
            SQL_TINYINT, 0, 0, vals_.back(), 0, inds_.back());
    }
    SQLRETURN BindGuid(int index, const AccountId& id) {
        vals_.push_back(new AccountId(id));
        inds_.push_back(new SQLLEN(16));
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_INPUT, SQL_C_BINARY,
            SQL_GUID, 16, 16, vals_.back(), 16, inds_.back());
    }
    SQLRETURN BindWString(int index, const std::wstring& s, int maxChars) {
        wchar_t* buf = new wchar_t[maxChars + 1];
        memset(buf, 0, (maxChars + 1) * sizeof(wchar_t));
        wcsncpy_s(buf, maxChars + 1, s.c_str(), _TRUNCATE);
        wbufs_.push_back(buf);
        vals_.push_back(buf);
        inds_.push_back(new SQLLEN(s.size() * sizeof(wchar_t)));
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_INPUT, SQL_C_WCHAR,
            SQL_WVARCHAR, maxChars, 0, buf, (SQLSMALLINT)(s.size() * sizeof(wchar_t)), inds_.back());
    }
    SQLRETURN BindTimestamp(int index, const TIMESTAMP_STRUCT& ts) {
        vals_.push_back(new TIMESTAMP_STRUCT(ts));
        inds_.push_back(new SQLLEN(sizeof(TIMESTAMP_STRUCT)));
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_INPUT, SQL_C_TIMESTAMP,
            SQL_TIMESTAMP, 0, 0, vals_.back(), sizeof(TIMESTAMP_STRUCT), inds_.back());
    }
    // Output-parameter variants: caller owns the storage and indicator.
    SQLRETURN BindOutI64(int index, i64* value, SQLLEN* ind) {
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_OUTPUT, SQL_C_SBIGINT,
            SQL_BIGINT, 0, 0, value, 0, ind);
    }
    SQLRETURN BindOutI32(int index, i32* value, SQLLEN* ind) {
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_OUTPUT, SQL_C_SLONG,
            SQL_INTEGER, 0, 0, value, 0, ind);
    }
    SQLRETURN BindOutTimestamp(int index, TIMESTAMP_STRUCT* value, SQLLEN* ind) {
        return SQLBindParameter(h_, (SQLUSMALLINT)index, SQL_PARAM_OUTPUT, SQL_C_TIMESTAMP,
            SQL_TIMESTAMP, 0, 0, value, sizeof(TIMESTAMP_STRUCT), ind);
    }
    SQLRETURN Execute() { return SQLExecute(h_); }
    // A RETURN parameter from "{?=CALL ...}" is only final after all result sets are consumed.
    void Drain() {
        while (SQLFetch(h_) == SQL_SUCCESS || SQLFetch(h_) == SQL_SUCCESS_WITH_INFO) {}
        while (SQLMoreResults(h_) == SQL_SUCCESS || SQLMoreResults(h_) == SQL_SUCCESS_WITH_INFO) {
            while (SQLFetch(h_) == SQL_SUCCESS || SQLFetch(h_) == SQL_SUCCESS_WITH_INFO) {}
        }
    }
    bool ExecDirect(const wchar_t* sql) {
        return SQLExecDirectW(h_, (SQLWCHAR*)sql, SQL_NTS) == SQL_SUCCESS ||
               SQLExecDirectW(h_, (SQLWCHAR*)sql, SQL_NTS) == SQL_SUCCESS_WITH_INFO;
    }
    SQLRETURN Fetch() { return SQLFetch(h_); }
    int ReturnValue() const { return (int)ret_; }
    SQLHSTMT Handle() const { return h_; }
    bool Ok() const { return ok_; }

    // Column bind helpers. Each call allocates persistent indicator storage.
    SQLRETURN ColI64(int col, i64* v) {
        SQLLEN* ind = new SQLLEN(0); inds_.push_back(ind);
        return SQLBindCol(h_, (SQLUSMALLINT)col, SQL_C_SBIGINT, v, 0, ind);
    }
    SQLRETURN ColI32(int col, i32* v) {
        SQLLEN* ind = new SQLLEN(0); inds_.push_back(ind);
        return SQLBindCol(h_, (SQLUSMALLINT)col, SQL_C_SLONG, v, 0, ind);
    }
    SQLRETURN ColU8(int col, u8* v) {
        SQLLEN* ind = new SQLLEN(0); inds_.push_back(ind);
        return SQLBindCol(h_, (SQLUSMALLINT)col, SQL_C_UTINYINT, v, 0, ind);
    }
    SQLRETURN ColI16(int col, i16* v) {
        SQLLEN* ind = new SQLLEN(0); inds_.push_back(ind);
        return SQLBindCol(h_, (SQLUSMALLINT)col, SQL_C_SSHORT, v, 0, ind);
    }
    SQLRETURN ColTimestamp(int col, TIMESTAMP_STRUCT* v) {
        SQLLEN* ind = new SQLLEN(0); inds_.push_back(ind);
        return SQLBindCol(h_, (SQLUSMALLINT)col, SQL_C_TIMESTAMP, v, sizeof(TIMESTAMP_STRUCT), ind);
    }
    SQLRETURN ColWString(int col, wchar_t* buf, SQLLEN chars, SQLLEN* indOut) {
        SQLLEN* ind = new SQLLEN(0); inds_.push_back(ind);
        SQLRETURN rc = SQLBindCol(h_, (SQLUSMALLINT)col, SQL_C_WCHAR, buf,
                                  chars * sizeof(wchar_t), ind);
        if (indOut) *indOut = 0;
        wbufs_.push_back(NULL); // placeholder, not owned
        return rc;
    }

private:
    SQLHSTMT h_;
    bool ok_;
    i64 ret_;
    SQLLEN retInd_;
    std::vector<void*> vals_;
    std::vector<SQLLEN*> inds_;
    std::vector<wchar_t*> wbufs_;
};

} // namespace

int AccountInventoryDB::CheckWorldId(int worldId) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_CheckWorldId(?)}")) return -1;
    s.BindReturn();
    s.BindI32(2, worldId);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    return s.ReturnValue();
}

int AccountInventoryDB::RetrieveWarehouseItemCount(const AccountId& account,
        i64 beginLabelId, i64* goodsCount, i64* itemCount) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_RetrieveWarehouseItemCount(?,?)}")) return -1;
    s.BindReturn();
    s.BindGuid(2, account);
    s.BindI64(3, beginLabelId);
    if (SQL_SUCCESS != s.Execute() && SQL_SUCCESS_WITH_INFO != s.Execute()) return -1;
    // result set: GoodsCount, ItemCount
    i64 g = 0, it = 0;
    s.ColI64(1, &g);
    s.ColI64(2, &it);
    if (s.Fetch() != SQL_SUCCESS && s.Fetch() != SQL_SUCCESS_WITH_INFO) return -1;
    if (goodsCount) *goodsCount = g;
    if (itemCount) *itemCount = it;
    return s.ReturnValue();
}

int AccountInventoryDB::RetrieveWarehouseItemList(const AccountId& account,
        int searchCount, int searchType, i64 searchKey,
        std::vector<WarehouseItemRecord>* out) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_RetrieveWarehouseItemList(?,?,?,?)}")) return -1;
    s.BindReturn();
    s.BindGuid(2, account);
    s.BindI32(3, searchCount);
    s.BindU8(4, (u8)searchType);
    s.BindI64(5, searchKey);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    i64 labelId = 0, itemInstance = 0;
    i32 goodsNum = 0, dataId = 0, amount = 0, usable = 0, state = 0;
    s.ColI64(1, &labelId); s.ColI32(2, &goodsNum);
    s.ColI64(7, &itemInstance); s.ColI32(8, &dataId);
    s.ColI32(9, &amount); s.ColI32(10, &usable); s.ColI32(11, &state);
    while (s.Fetch() == SQL_SUCCESS || s.Fetch() == SQL_SUCCESS_WITH_INFO) {
        WarehouseItemRecord r;
        memset(&r, 0, sizeof(r));
        r.labelId = labelId; r.itemInstanceId = itemInstance;
        r.goodsItemNumber = goodsNum; r.itemDataId = dataId;
        r.itemAmount = amount; r.usableDuration = usable; r.itemState = (u8)state;
        out->push_back(r);
    }
    return s.ReturnValue();
}

int AccountInventoryDB::RetrieveLobbyServiceItemList(const AccountId& account,
        int searchCount, int searchType, i64 searchKey,
        std::vector<LobbyServiceItemRecord>* out) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_RetrieveLobbyServiceItemList(?,?,?,?)}")) return -1;
    s.BindReturn();
    s.BindGuid(2, account);
    s.BindI32(3, searchCount);
    s.BindU8(4, (u8)searchType);
    s.BindI64(5, searchKey);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    i64 labelId = 0, itemInstance = 0, ownerPc = 0;
    i32 dataId = 0, state = 0;
    s.ColI64(1, &labelId); s.ColI64(2, &itemInstance);
    s.ColI32(3, &dataId); s.ColI32(4, &state); s.ColI64(5, &ownerPc);
    while (s.Fetch() == SQL_SUCCESS || s.Fetch() == SQL_SUCCESS_WITH_INFO) {
        LobbyServiceItemRecord r;
        memset(&r, 0, sizeof(r));
        r.labelId = labelId; r.itemInstanceId = itemInstance;
        r.itemDataId = dataId; r.itemState = (u8)state; r.ownerPcId = ownerPc;
        out->push_back(r);
    }
    return s.ReturnValue();
}

int AccountInventoryDB::RetrieveSingleWarehouseItem(i64 itemInstanceId,
        const AccountId& account, i64 pcId, SingleWarehouseItemRecord* out) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_RetrieveSingleWarehouseItem(?,?,?)}")) return -1;
    s.BindReturn();
    s.BindI64(2, itemInstanceId);
    s.BindGuid(3, account);
    // Wire pc-id 0 means "no owner pc": the original binds SQL NULL so the proc skips the
    // OwnerPCID equality filter and returns the item by instance id alone.
    if (pcId == 0) s.BindNull(4, SQL_BIGINT);
    else s.BindI64(4, pcId);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    i64 labelId = 0, instance = 0;
    i32 dataId = 0, state = 0;
    s.ColI64(1, &labelId); s.ColI64(2, &instance);
    s.ColI32(3, &dataId); s.ColI32(4, &state);
    SQLRETURN fr = s.Fetch();
    if (fr != SQL_SUCCESS && fr != SQL_SUCCESS_WITH_INFO) return 2; // no such item
    out->labelId = labelId; out->itemInstanceId = instance;
    out->itemDataId = dataId; out->itemState = (u8)state;
    return s.ReturnValue();
}

int AccountInventoryDB::TryWarehouseItemConsumption(i64 labelId, i64 itemInstanceId,
        const AccountId& account) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_TryWarehouseItemConsumption(?,?,?)}")) return -1;
    s.BindReturn();
    s.BindI64(2, labelId); s.BindI64(3, itemInstanceId); s.BindGuid(4, account);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::FinishWarehouseItemConsumption(bool result, i64 labelId,
        i64 itemInstanceId, const AccountId& account) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_FinishWarehouseItemConsumption(?,?,?,?)}")) return -1;
    s.BindReturn();
    s.BindU8(2, result ? 1 : 0); s.BindI64(3, labelId);
    s.BindI64(4, itemInstanceId); s.BindGuid(5, account);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::CancelWarehouseItemConsumption(i64 labelId, i64 itemInstanceId,
        const AccountId& account) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_CancelWarehouseItemConsumption(?,?,?)}")) return -1;
    s.BindReturn();
    s.BindI64(2, labelId); s.BindI64(3, itemInstanceId); s.BindGuid(4, account);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::ConsumeWarehouseItem(i64 labelId, i64 itemInstanceId,
        const AccountId& account) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_ConsumeWarehouseItem(?,?,?)}")) return -1;
    s.BindReturn();
    s.BindI64(2, labelId); s.BindI64(3, itemInstanceId); s.BindGuid(4, account);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::RetrieveUncompletedWarehouseRegistrationList(
        const AccountId& account, std::vector<UncompletedRegistrationRecord>* out) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_RetrieveUncompletedWarehouseRegistrationList(?)}")) return -1;
    s.BindReturn(); s.BindGuid(2, account);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    i64 labelId = 0, goodsId = 0;
    s.ColI64(1, &labelId); s.ColI64(2, &goodsId);
    while (s.Fetch() == SQL_SUCCESS || s.Fetch() == SQL_SUCCESS_WITH_INFO) {
        UncompletedRegistrationRecord r = { labelId, goodsId };
        out->push_back(r);
    }
    return s.ReturnValue();
}

int AccountInventoryDB::RetrieveUncompletedItemAcquisitionList(i64 pcId,
        std::vector<UncompletedAcquisitionRecord>* out) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_RetrieveUncompletedItemAcquisitionList(?)}")) return -1;
    s.BindReturn(); s.BindI64(2, pcId);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    i64 labelId = 0, instance = 0;
    s.ColI64(1, &labelId); s.ColI64(2, &instance);
    while (s.Fetch() == SQL_SUCCESS || s.Fetch() == SQL_SUCCESS_WITH_INFO) {
        UncompletedAcquisitionRecord r = { labelId, instance };
        out->push_back(r);
    }
    return s.ReturnValue();
}

int AccountInventoryDB::RetrieveUncompletedInstantPaymentServiceList(i64 pcId,
        std::vector<UncompletedInstantPaymentRecord>* out) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_RetrieveUncompletedInstantPaymentServiceList(?)}")) return -1;
    s.BindReturn(); s.BindI64(2, pcId);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    i64 serviceId = 0; i32 serviceType = 0;
    s.ColI64(1, &serviceId); s.ColI32(2, &serviceType);
    while (s.Fetch() == SQL_SUCCESS || s.Fetch() == SQL_SUCCESS_WITH_INFO) {
        UncompletedInstantPaymentRecord r = { serviceId, serviceType };
        out->push_back(r);
    }
    return s.ReturnValue();
}

int AccountInventoryDB::LoadUnknownInstantPaymentStateList(
        std::vector<UncompletedInstantPaymentRecord>* out) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_LoadUnknownInstantPaymentStateList}")) return -1;
    s.BindReturn();
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    i64 serviceId = 0; i32 serviceType = 0;
    s.ColI64(1, &serviceId); s.ColI32(2, &serviceType);
    while (s.Fetch() == SQL_SUCCESS || s.Fetch() == SQL_SUCCESS_WITH_INFO) {
        UncompletedInstantPaymentRecord r = { serviceId, serviceType };
        out->push_back(r);
    }
    return s.ReturnValue();
}

// ---- registration / acquisition / payment procedures -------------------------------

int AccountInventoryDB::LoadUncompletedWarehouseRegistrationList(int loadCount, i64 loadKey,
        std::vector<UncompletedRegistrationRecord>* out) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_LoadUncompletedWarehouseRegistrationList(?,?)}")) return -1;
    s.BindReturn(); s.BindI32(2, loadCount); s.BindI64(3, loadKey);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    i64 labelId = 0, goodsId = 0;
    s.ColI64(1, &labelId); s.ColI64(2, &goodsId);
    while (s.Fetch() == SQL_SUCCESS || s.Fetch() == SQL_SUCCESS_WITH_INFO) {
        UncompletedRegistrationRecord r = { labelId, goodsId };
        out->push_back(r);
    }
    return s.ReturnValue();
}

int AccountInventoryDB::TryWarehouseRegistration(const AccountId& account, i64 goodsId,
        i32 goodsNumber, const std::wstring& senderDesc, const std::wstring& senderMsg,
        const TIMESTAMP_STRUCT& purchaseTime,
        const std::vector<WarehouseItemRecord>& items, i64* newLabelId) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    const wchar_t* sql =
        L"{?=CALL dbo.usp_TryWarehouseRegistration(?,?,?,?,?,?,"
        L"?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)}";
    if (!s.Prepare(sql)) return -1;
    i64 label = 0; SQLLEN labelInd = 0;
    s.BindReturn();
    s.BindOutI64(2, &label, &labelInd);
    s.BindGuid(3, account); s.BindI64(4, goodsId); s.BindI32(5, goodsNumber);
    s.BindWString(6, senderDesc, 256); s.BindWString(7, senderMsg, 256);
    s.BindTimestamp(8, purchaseTime);
    int param = 9;
    for (size_t i = 0; i < 10 && i < items.size(); ++i) {
        s.BindI32(param++, items[i].goodsItemNumber);
        s.BindI32(param++, items[i].itemDataId);
        s.BindI32(param++, items[i].itemAmount);
        s.BindI32(param++, items[i].usableDuration);
    }
    // pad to 40 input params (4 per item x 10)
    while (param <= 48) s.BindI32(param++, 0);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    if (newLabelId) *newLabelId = label;
    return s.ReturnValue();
}

int AccountInventoryDB::FinishWarehouseRegistration(bool result, i64 labelId,
        const AccountId& account, i64 goodsId,
        std::vector<WarehouseItemRecord>* addedItems) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_FinishWarehouseRegistration(?,?,?,?)}")) return -1;
    s.BindReturn();
    s.BindU8(2, result ? 1 : 0); s.BindI64(3, labelId);
    s.BindGuid(4, account); s.BindI64(5, goodsId);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    i64 instanceId = 0; i32 goodsNum = 0, dataId = 0, amount = 0, usable = 0, state = 0;
    TIMESTAMP_STRUCT purchase, reg;
    s.ColI64(1, &labelId); s.ColI32(2, &goodsNum); s.ColTimestamp(3, &purchase);
    s.ColTimestamp(4, &reg); s.ColI64(5, &instanceId); s.ColI32(6, &dataId);
    s.ColI32(7, &amount); s.ColI32(8, &usable); s.ColI32(9, &state);
    while (s.Fetch() == SQL_SUCCESS || s.Fetch() == SQL_SUCCESS_WITH_INFO) {
        WarehouseItemRecord r; memset(&r, 0, sizeof(r));
        r.labelId = labelId; r.itemInstanceId = instanceId; r.goodsItemNumber = goodsNum;
        r.itemDataId = dataId; r.itemAmount = amount; r.usableDuration = usable;
        r.itemState = (u8)state;
        addedItems->push_back(r);
    }
    return s.ReturnValue();
}

int AccountInventoryDB::TryWarehouseItemAcquisition(i64 labelId, i64 itemInstanceId,
        const AccountId& account, int wid, i64 pcId, int itemDataId,
        int* itemAmount, TIMESTAMP_STRUCT* purchaseTime, int* usableDuration) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_TryWarehouseItemAcquisition(?,?,?,?,?,?,?,?,?)}")) return -1;
    SQLLEN amtInd = 0, purInd = 0, durInd = 0;
    s.BindReturn();
    s.BindI64(2, labelId); s.BindI64(3, itemInstanceId); s.BindGuid(4, account);
    s.BindI16(5, (i16)wid); s.BindI64(6, pcId); s.BindI32(7, itemDataId);
    s.BindOutI32(8, itemAmount, &amtInd);
    s.BindOutTimestamp(9, purchaseTime, &purInd);
    s.BindOutI32(10, usableDuration, &durInd);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::FinishWarehouseItemAcquisition(bool result, i64 labelId,
        i64 itemInstanceId, const AccountId& account, i64 pcId) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_FinishWarehouseItemAcquisition(?,?,?,?,?)}")) return -1;
    s.BindReturn();
    s.BindU8(2, result ? 1 : 0); s.BindI64(3, labelId); s.BindI64(4, itemInstanceId);
    s.BindGuid(5, account); s.BindI64(6, pcId);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::CancelWarehouseItemAcquisition(i64 labelId, i64 itemInstanceId,
        const AccountId& account, i64 pcId) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_CancelWarehouseItemAcquisition(?,?,?,?)}")) return -1;
    s.BindReturn();
    s.BindI64(2, labelId); s.BindI64(3, itemInstanceId); s.BindGuid(4, account);
    s.BindI64(5, pcId);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::TryInstantPaymentService(const AccountId& account, int worldId,
        i64 pcId, int serviceType, int goodsNumber, i64* serviceId) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_TryInstantPaymentService(?,?,?,?,?,?)}")) return -1;
    SQLLEN sidInd = 0;
    s.BindReturn();
    s.BindOutI64(2, serviceId, &sidInd);
    s.BindGuid(3, account); s.BindI16(4, (i16)worldId); s.BindI64(5, pcId);
    s.BindI32(6, serviceType); s.BindI32(7, goodsNumber);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::CancelInstantPaymentService(i64 serviceId,
        const AccountId& account, i64 pcId, int serviceType) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_CancelInstantPaymentService(?,?,?,?)}")) return -1;
    s.BindReturn();
    s.BindI64(2, serviceId); s.BindGuid(3, account); s.BindI64(4, pcId);
    s.BindI32(5, serviceType);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::UpdateInstantPaymentServiceStateToPaymentSuccessState(
        i64 serviceId, const AccountId& account, i64 pcId, int serviceType, i64 purchaseId) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_UpdateInstantPaymentServiceStateToPaymentSuccessState(?,?,?,?,?)}")) return -1;
    s.BindReturn();
    s.BindI64(2, serviceId); s.BindGuid(3, account); s.BindI64(4, pcId);
    s.BindI32(5, serviceType); s.BindI64(6, purchaseId);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

int AccountInventoryDB::FinishInstantPaymentService(i64 serviceId,
        const AccountId& account, i64 pcId, int serviceType, bool result) {
    if (!Handle()) return -1;
    Stmt s(Handle());
    if (!s.Prepare(L"{?=CALL dbo.usp_FinishInstantPaymentService(?,?,?,?,?)}")) return -1;
    s.BindReturn();
    s.BindI64(2, serviceId); s.BindGuid(3, account); s.BindI64(4, pcId);
    s.BindI32(5, serviceType); s.BindU8(6, result ? 1 : 0);
    SQLRETURN rc = s.Execute();
    if (rc != SQL_SUCCESS && rc != SQL_SUCCESS_WITH_INFO) return -1;
    s.Drain();
    return s.ReturnValue();
}

} // namespace accountinventory
