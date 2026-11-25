// AccountInventoryDaemon - application daemon (original: AccountInventoryDaemon.cpp).
#pragma once
#include "../BloodLib/Daemon.h"
#include "Config.h"
#include "Db.h"
#include "AccountInventoryDB.h"
#include "NpClient.h"

namespace accountinventory {

class AccountInventoryDaemon : public blood::Daemon {
public:
    AccountInventoryDaemon();
    ~AccountInventoryDaemon() override;

    const wchar_t* GetServiceName() override { return L"BnS AccountInventoryDaemon"; }
    const wchar_t* GetLogTag() override { return L"accountinventoryd"; }
    const wchar_t* GetName() override { return L"AccountInventoryDaemon"; }
    const wchar_t* GetDescription() override { return L"Blade&Soul AccountInventory Daemon"; }
    int GetTimerInterval() override;
    int GetTimerMaxTime() override;
    void PrintVersion() override;

    bool initLog() override;
    bool initialize() override;
    bool initNetwork() override;
    bool onStart() override;
    void shutdown() override;
    bool reload(const wchar_t* what, int kind) override;

    const AppConfig* GetAppConfig() const { return config_; }

private:
    AppConfig* config_;
    Db* db_;
    AccountInventoryDB* appDb_;
    NpClient* np_;
};

} // namespace accountinventory
