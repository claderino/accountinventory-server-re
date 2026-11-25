// AccountInventoryDaemon - full DB business-logic layer.
// Reconstructed from accountInventoryDB.cpp (decomp RVAs 0x09970..0x14C80) and the 43
// stored procedures of GameWarehouseDB (analysis/db/procs/).
#pragma once
#include "Db.h"
#include <sql.h>
#include <sqlext.h>
#include <string>
#include <vector>

namespace accountinventory {

using blood::u8;
using blood::i8;
using blood::u16;
using blood::i16;
using blood::u32;
using blood::i32;
using blood::u64;
using blood::i64;

struct AccountId {
    unsigned char bytes[16];
    std::string Text() const;
    std::wstring TextW() const;
    static AccountId FromWire(const void* p);
};

struct WarehouseGoodsRecord {
    i64 labelId;
    i32 goodsNumber;
    std::wstring senderDescription;
    std::wstring senderMessage;
    TIMESTAMP_STRUCT purchaseTime;
    TIMESTAMP_STRUCT registrationTime;
};

struct WarehouseItemRecord {
    i64 labelId;
    i64 itemInstanceId;
    i32 goodsItemNumber;
    i32 itemDataId;
    i32 itemAmount;
    i32 usableDuration;
    u8  itemState;
    i64 ownerPcId;
};

struct LobbyServiceItemRecord {
    i64 labelId;
    i64 itemInstanceId;
    i32 itemDataId;
    u8  itemState;
    i64 ownerPcId;
};

struct SingleWarehouseItemRecord {
    i64 labelId;
    i64 itemInstanceId;
    i32 itemDataId;
    u8  itemState;
};

struct UncompletedRegistrationRecord { i64 labelId; i64 goodsId; };
struct UncompletedAcquisitionRecord  { i64 labelId; i64 itemInstanceId; };
struct UncompletedInstantPaymentRecord { i64 serviceId; i32 serviceType; };

// The daemon's DB facade. All methods return the proc's return value (0 = success) or a
// negative error code, and fill the out parameters with the fetched rows.
class AccountInventoryDB {
public:
    explicit AccountInventoryDB(Db* db) : db_(db) {}
    SQLHDBC Handle() const { return db_ ? db_->Dbc() : SQL_NULL_HANDLE; }

    int CheckWorldId(int worldId);

    // Registration / goods
    int LoadUncompletedWarehouseRegistrationList(int loadCount, i64 loadKey,
            std::vector<UncompletedRegistrationRecord>* out);
    int TryWarehouseRegistration(const AccountId& account, i64 goodsId, i32 goodsNumber,
            const std::wstring& senderDesc, const std::wstring& senderMsg,
            const TIMESTAMP_STRUCT& purchaseTime,
            const std::vector<WarehouseItemRecord>& items, i64* newLabelId);
    int FinishWarehouseRegistration(bool result, i64 labelId, const AccountId& account,
            i64 goodsId, std::vector<WarehouseItemRecord>* addedItems);
    int RetrieveUncompletedWarehouseRegistrationList(const AccountId& account,
            std::vector<UncompletedRegistrationRecord>* out);

    // Warehouse listing / search
    int RetrieveWarehouseItemCount(const AccountId& account, i64 beginLabelId,
            i64* goodsCount, i64* itemCount);
    int RetrieveWarehouseItemList(const AccountId& account, int searchCount,
            int searchType, i64 searchKey, std::vector<WarehouseItemRecord>* out);

    // Item acquisition
    int RetrieveUncompletedItemAcquisitionList(i64 pcId,
            std::vector<UncompletedAcquisitionRecord>* out);
    int TryWarehouseItemAcquisition(i64 labelId, i64 itemInstanceId, const AccountId& account,
            int wid, i64 pcId, int itemDataId, int* itemAmount,
            TIMESTAMP_STRUCT* purchaseTime, int* usableDuration);
    int FinishWarehouseItemAcquisition(bool result, i64 labelId, i64 itemInstanceId,
            const AccountId& account, i64 pcId);
    int CancelWarehouseItemAcquisition(i64 labelId, i64 itemInstanceId,
            const AccountId& account, i64 pcId);

    // Lobby service items / consumption
    int RetrieveLobbyServiceItemList(const AccountId& account, int searchCount,
            int searchType, i64 searchKey, std::vector<LobbyServiceItemRecord>* out);
    int RetrieveSingleWarehouseItem(i64 itemInstanceId, const AccountId& account,
            i64 pcId, SingleWarehouseItemRecord* out);
    int TryWarehouseItemConsumption(i64 labelId, i64 itemInstanceId,
            const AccountId& account);
    int FinishWarehouseItemConsumption(bool result, i64 labelId, i64 itemInstanceId,
            const AccountId& account);
    int CancelWarehouseItemConsumption(i64 labelId, i64 itemInstanceId,
            const AccountId& account);
    int ConsumeWarehouseItem(i64 labelId, i64 itemInstanceId, const AccountId& account);

    // Instant payment
    int TryInstantPaymentService(const AccountId& account, int worldId, i64 pcId,
            int serviceType, int goodsNumber, i64* serviceId);
    int CancelInstantPaymentService(i64 serviceId, const AccountId& account,
            i64 pcId, int serviceType);
    int UpdateInstantPaymentServiceStateToPaymentSuccessState(i64 serviceId,
            const AccountId& account, i64 pcId, int serviceType, i64 purchaseId);
    int FinishInstantPaymentService(i64 serviceId, const AccountId& account,
            i64 pcId, int serviceType, bool result);
    int RetrieveUncompletedInstantPaymentServiceList(i64 pcId,
            std::vector<UncompletedInstantPaymentRecord>* out);
    int LoadUnknownInstantPaymentStateList(
            std::vector<UncompletedInstantPaymentRecord>* out);

private:
    int Exec(const wchar_t* query, int paramCount, ...);
    Db* db_;
};

} // namespace accountinventory
