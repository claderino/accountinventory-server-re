// AccountInventoryDaemon - configuration (original: accountinventoryd.config.xml reader).
#pragma once
#include "../BloodLib/BloodLib.h"
#include <map>
#include <string>

namespace accountinventory {

// All keys from the shipped accountinventoryd.config.xml, in the original order.
struct AppConfig {
    std::wstring rootPath;
    std::wstring logPath;
    std::wstring configPath;
    std::wstring transactionLogPath;
    std::string  certFile;
    int workerThread;
    int maxWorld;
    int timerInterval;
    int timerMaxTime;
    int transactionTimeout;
    int dumpType;
    int waitTimeForReusePoolObject;
    int initJobCount;
    int goodsListHashCount;
    int goodsJobHashCount;
    bool npEnabled;
    int npProgramId;
    std::string npGameCode;
    std::string npAddress;
    int npPort;
    int manageListenPort;
    int lobbyListenPort;
    int worldListenPort;
    int serviceWorldId;
    bool npWarehouseChangeoverEnable;
    bool overseasNp;
    int goodsInfoListPageSize;
    int automatedTakeGoodsType;
    int characterInventoryItemNotiCount;
    int goodsListMaxCount;
    int warehouseGoodsSearchCount;
    int lobbyServiceItemSearchCount;
    int uncompletedGoodsRegistrationLoadCount;
    int uncompletedGoodsRegistrationFailCountMax;
    bool enableInstantPayment;
    int instantPaymentGoodsCategoryUpdatePeriod;
    int instantPaymentGoodsCategoryType;
    int instantPaymentGoodsCategoryLanguageCode;
    int instantPaymentPurchaseCurrencyGroupId;
    int instantPaymentPurchaseRequestFromCode;
    bool enableInstantPaymentSecondPassword;
    int instantPaymentSecondPasswordType;
    int instantPaymentSecondPasswordActionIndex;
    int instantPaymentRestoreCountPerTimerInterval;

    AppConfig();
};

// Resolve the config file: <dir>\accountinventoryd.config.xml unless path already ends in .xml.
bool ResolveConfigFile(const wchar_t* configPathOrFile, wchar_t* output, size_t capacity);
bool LoadAppConfig(const wchar_t* file, AppConfig* out);
void PrintAllConfig(const AppConfig& cfg);

} // namespace accountinventory
