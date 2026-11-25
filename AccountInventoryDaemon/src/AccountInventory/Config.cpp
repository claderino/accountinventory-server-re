// AccountInventoryDaemon - configuration reader.
// The original uses XmlReader_sv64.dll; we accept the same XML option/name/value syntax.
#include "Config.h"
#include "../BloodLib/BloodLib.h"
#include "../BloodLib/Daemon.h"
#include <shlwapi.h>
#include <stdio.h>

namespace accountinventory {

SOURCE_FILE("DaemonConfig.cpp");

AppConfig::AppConfig()
    : workerThread(0), maxWorld(10000), timerInterval(1000), timerMaxTime(30),
      transactionTimeout(0), dumpType(0), waitTimeForReusePoolObject(0),
      initJobCount(4096), goodsListHashCount(64), goodsJobHashCount(0),
      npEnabled(true), npProgramId(1820), npGameCode("bnsgrnTH"), npAddress("127.0.0.1"),
      npPort(6601), manageListenPort(31700), lobbyListenPort(21700), worldListenPort(21701),
      serviceWorldId(10070), npWarehouseChangeoverEnable(true), overseasNp(true),
      goodsInfoListPageSize(50), automatedTakeGoodsType(2), characterInventoryItemNotiCount(10),
      goodsListMaxCount(100), warehouseGoodsSearchCount(20), lobbyServiceItemSearchCount(100),
      uncompletedGoodsRegistrationLoadCount(100), uncompletedGoodsRegistrationFailCountMax(5),
      enableInstantPayment(true), instantPaymentGoodsCategoryUpdatePeriod(30),
      instantPaymentGoodsCategoryType(2), instantPaymentGoodsCategoryLanguageCode(11),
      instantPaymentPurchaseCurrencyGroupId(71), instantPaymentPurchaseRequestFromCode(9),
      enableInstantPaymentSecondPassword(false), instantPaymentSecondPasswordType(3),
      instantPaymentSecondPasswordActionIndex(0), instantPaymentRestoreCountPerTimerInterval(50) {}

bool ResolveConfigFile(const wchar_t* configPathOrFile, wchar_t* output, size_t capacity) {
    if (!configPathOrFile || !*configPathOrFile) return false;
    wchar_t path[MAX_PATH * 2];
    wcscpy_s(path, configPathOrFile);
    const wchar_t* ext = PathFindExtensionW(path);
    if (ext && _wcsicmp(ext, L".xml") == 0) {
        wcscpy_s(output, capacity, path);
        return true;
    }
    if (path[wcslen(path) - 1] != L'\\') wcscat_s(path, L"\\");
    wcscat_s(path, L"accountinventoryd.config.xml");
    wcscpy_s(output, capacity, path);
    return true;
}

namespace {
std::string WtoA(const std::wstring& w) {
    if (w.empty()) return std::string();
    int n = WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), NULL, 0, NULL, NULL);
    std::string s(n, '\0');
    if (n > 0) WideCharToMultiByte(CP_ACP, 0, w.c_str(), (int)w.size(), &s[0], n, NULL, NULL);
    return s;
}
std::wstring AtoW(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
bool IsTrue(const std::string& v) {
    return _stricmp(v.c_str(), "yes") == 0 || _stricmp(v.c_str(), "true") == 0 || v == "1";
}
int AsInt(const std::string& v, int def) {
    if (v.empty()) return def;
    char* end = NULL;
    long n = strtol(v.c_str(), &end, 10);
    return end == v.c_str() ? def : (int)n;
}
// Pull name='...' and value='...' (single or double quotes) out of an <option .../> tag.
bool ParseOptionTag(const std::string& line, std::string* name, std::string* value) {
    size_t p = line.find("name");
    if (p == std::string::npos) return false;
    p = line.find_first_of("'\"", p);
    if (p == std::string::npos) return false;
    char quote = line[p++];
    size_t e = line.find(quote, p);
    if (e == std::string::npos) return false;
    *name = line.substr(p, e - p);
    p = line.find("value", e);
    if (p == std::string::npos) return false;
    p = line.find_first_of("'\"", p);
    if (p == std::string::npos) return false;
    quote = line[p++];
    e = line.find(quote, p);
    if (e == std::string::npos) return false;
    *value = line.substr(p, e - p);
    return true;
}
} // namespace

bool LoadAppConfig(const wchar_t* file, AppConfig* out) {
    FILE* fp = _wfopen(file, L"rb");
    if (!fp) return false;
    std::map<std::string, std::string> vars;
    char buffer[8192];
    while (fgets(buffer, sizeof(buffer), fp)) {
        std::string line(buffer);
        std::string name, value;
        if (!ParseOptionTag(line, &name, &value)) continue;
        vars[name] = value;
    }
    fclose(fp);
    if (vars.empty()) return false;
#define GETS(dst, key) do { std::map<std::string,std::string>::iterator it=vars.find(key); if(it!=vars.end()) (dst)=it->second; } while(0)
#define GETW(dst, key) do { std::map<std::string,std::string>::iterator it=vars.find(key); if(it!=vars.end()) (dst)=AtoW(it->second); } while(0)
#define GETI(dst, key) do { std::map<std::string,std::string>::iterator it=vars.find(key); if(it!=vars.end()) (dst)=AsInt(it->second,(dst)); } while(0)
#define GETB(dst, key) do { std::map<std::string,std::string>::iterator it=vars.find(key); if(it!=vars.end()) (dst)=IsTrue(it->second); } while(0)
    GETW(out->rootPath, "root-path");
    GETW(out->logPath, "log-path");
    GETW(out->configPath, "config-path");
    GETW(out->transactionLogPath, "transaction-log-path");
    GETS(out->certFile, "cert-file");
    GETI(out->workerThread, "worker-thread");
    GETI(out->maxWorld, "max-world");
    GETI(out->timerInterval, "timer-interval");
    GETI(out->timerMaxTime, "timer-max-time");
    GETI(out->transactionTimeout, "transaction-timeout");
    GETI(out->dumpType, "dump-type");
    GETI(out->waitTimeForReusePoolObject, "wait-time-for-reuse-pool-object");
    GETI(out->initJobCount, "init-job-count");
    GETI(out->goodsListHashCount, "goods-list-hash-count");
    GETI(out->goodsJobHashCount, "goods-job-hash-count");
    GETB(out->npEnabled, "np-enabled");
    GETI(out->npProgramId, "np-program-id");
    GETS(out->npGameCode, "np-game-code");
    GETS(out->npAddress, "np-address");
    GETI(out->npPort, "np-port");
    GETI(out->manageListenPort, "manage-listen-port");
    GETI(out->lobbyListenPort, "lobby-listen-port");
    GETI(out->worldListenPort, "world-listen-port");
    GETI(out->serviceWorldId, "service-world-id");
    GETB(out->npWarehouseChangeoverEnable, "np-warehouse-changeover-enable");
    GETB(out->overseasNp, "overseas-np");
    GETI(out->goodsInfoListPageSize, "goods-info-list-page-size");
    GETI(out->automatedTakeGoodsType, "automated-take-goods-type");
    GETI(out->characterInventoryItemNotiCount, "character-inventory-item-noti-count");
    GETI(out->goodsListMaxCount, "goods-list-max-count");
    GETI(out->warehouseGoodsSearchCount, "warehouse-goods-search-count");
    GETI(out->lobbyServiceItemSearchCount, "lobby-service-item-search-count");
    GETI(out->uncompletedGoodsRegistrationLoadCount, "uncompleted-goods-registration-load-count");
    GETI(out->uncompletedGoodsRegistrationFailCountMax, "uncomoleted-goods-registration-fail-count-max");
    GETB(out->enableInstantPayment, "enable-instant-payment");
    GETI(out->instantPaymentGoodsCategoryUpdatePeriod, "instant-payment-goods-category-update-period");
    GETI(out->instantPaymentGoodsCategoryType, "instant-payment-goods-category-type");
    GETI(out->instantPaymentGoodsCategoryLanguageCode, "instant-payment-goods-category-language-code");
    GETI(out->instantPaymentPurchaseCurrencyGroupId, "instant-payment-purchase-currency-group-id");
    GETI(out->instantPaymentPurchaseRequestFromCode, "instant-payment-purchase-request-from-code");
    GETB(out->enableInstantPaymentSecondPassword, "enable-instant-payment-second-password");
    GETI(out->instantPaymentSecondPasswordType, "instant-payment-second-password-type");
    GETI(out->instantPaymentSecondPasswordActionIndex, "instant-payment-second-password-action-index");
    GETI(out->instantPaymentRestoreCountPerTimerInterval, "instant-payment-restore-count-per-timer-interval");
#undef GETS
#undef GETW
#undef GETI
#undef GETB
    return !out->rootPath.empty();
}

void PrintAllConfig(const AppConfig& cfg) {
    // Original DaemonConfig::PrintAll logs the cert path (line 164) and ints (141). Keep the
    // original source file / line numbers so log files compare line by line.
    blood::LogWrite(1, L"src\\DaemonConfig.cpp", 132, L"[%s], ==== config ====", L"accountinventoryd");
    blood::LogWrite(1, L"src\\DaemonConfig.cpp", 164, L"[%s], cert-file: %s", L"accountinventoryd",
                    AtoW(cfg.certFile).c_str());
    blood::LogWrite(1, L"src\\DaemonConfig.cpp", 141, L"[%s], worker-thread: %d", L"accountinventoryd", cfg.workerThread);
    blood::LogWrite(1, L"src\\DaemonConfig.cpp", 141, L"[%s], lobby-listen-port: %d", L"accountinventoryd", cfg.lobbyListenPort);
    blood::LogWrite(1, L"src\\DaemonConfig.cpp", 141, L"[%s], world-listen-port: %d", L"accountinventoryd", cfg.worldListenPort);
    blood::LogWrite(1, L"src\\DaemonConfig.cpp", 141, L"[%s], manage-listen-port: %d", L"accountinventoryd", cfg.manageListenPort);
    blood::LogWrite(1, L"src\\DaemonConfig.cpp", 141, L"[%s], service-world-id: %d", L"accountinventoryd", cfg.serviceWorldId);
}

} // namespace accountinventory
