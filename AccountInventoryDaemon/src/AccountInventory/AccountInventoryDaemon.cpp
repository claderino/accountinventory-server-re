// AccountInventoryDaemon - application daemon lifecycle.
#include "AccountInventoryDaemon.h"
#include "Config.h"
#include "Handlers.h"
#include "../BloodLib/Log.h"
#include "../BloodLib/Network.h"
#include "../BloodLib/PacketBuffer.h"
#include "../BloodLib/PacketBufferHandler.h"
#include "../BloodLib/Daemon.h"
#include <stdio.h>

namespace accountinventory {

SOURCE_FILE("AccountInventoryDaemon.cpp");

namespace {
std::wstring CertToWide(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_ACP, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
} // namespace

AccountInventoryDaemon::AccountInventoryDaemon() : config_(NULL), db_(NULL), appDb_(NULL) {}
AccountInventoryDaemon::~AccountInventoryDaemon() {
    delete appDb_;
    delete config_;
    delete db_;
}

int AccountInventoryDaemon::GetTimerInterval() {
    return config_ ? config_->timerInterval : 1000;
}
int AccountInventoryDaemon::GetTimerMaxTime() {
    return config_ ? config_->timerMaxTime : 30;
}

void AccountInventoryDaemon::PrintVersion() {
    // Original sub_07480.
    wprintf(L"packet accountinventory-game %d.0\n", 8);
    wprintf(L"packet accountinventory-lobby %d.0\n", 7);
    wprintf(L"packet accountinventory-manage %d.0\n", 1);
    wprintf(L"lib xmlreader %s.0\n", L"0.0.210");
    wprintf(L"lib stsmsglib %s.0\n", L"0.82.0");
}

bool AccountInventoryDaemon::initLog() {
    wchar_t file[MAX_PATH * 2];
    if (!ResolveConfigFile(ConfigPath(), file, _countof(file))) {
        blood::LogWrite(0, L"src\\Daemon.cpp", 0x1e7,
                        L"[%s], cannot resolve config path", GetLogTag());
        return false;
    }
    config_ = new AppConfig();
    if (!LoadAppConfig(file, config_)) {
        blood::LogWrite(0, L"AccountInventoryDaemon.cpp", 0x254,
                        L"[%s], failed to load config, config:%p", GetLogTag(), config_);
        return false;
    }
    if (config_->rootPath.empty()) {
        blood::LogWrite(0, L"AccountInventoryDaemon.cpp", 0x258,
                        L"[%s], invalid configuration", GetLogTag());
        return false;
    }
    SetCurrentDirectoryW(config_->rootPath.c_str());
    blood::SetLogConsole(GetConsole());
    blood::Log* log = blood::TheLog();
    if (!log) return false;
    if (!config_->logPath.empty())
        log->AddMedia(new blood::LogFile(config_->logPath.c_str(), CP_UTF8));
    PrintAllConfig(*config_);
    return true;
}

bool AccountInventoryDaemon::initialize() {
    // cert file -> raw ODBC connection string (original: no comments allowed).
    wchar_t certPath[MAX_PATH * 2];
    swprintf_s(certPath, L"%s", CertToWide(config_->certFile).c_str());
    FILE* fp = _wfopen(certPath, L"rb");
    if (!fp) {
        blood::LogWrite(0, L"AccountInventoryDaemon.cpp", 0xbf,
                        L"[%s], cannot execute, error:%d", GetLogTag(), 3);
        return false;
    }
    char raw[1024];
    size_t n = fread(raw, 1, sizeof(raw) - 1, fp);
    fclose(fp);
    raw[n] = '\0';
    std::string connectionString(raw);
    // Trim whitespace; the original rejects commented cert files.
    size_t a = connectionString.find_first_not_of(" \t\r\n");
    size_t b = connectionString.find_last_not_of(" \t\r\n");
    if (a == std::string::npos || (b + 1) <= a) {
        blood::LogWrite(0, L"AccountInventoryDaemon.cpp", 0xbf,
                        L"[%s], cannot execute, error:%d", GetLogTag(), 3);
        return false;
    }
    connectionString = connectionString.substr(a, b - a + 1);

    db_ = new Db();
    if (!db_->Connect(connectionString)) {
        blood::LogWrite(0, L"AccountInventoryDaemon.cpp", 0xd0,
                        L"[%s], failed to initialize db, dbId:%d", GetLogTag(), 1);
        return false;
    }
    int dbRet = -1;
    if (!db_->CheckWorldId(config_->serviceWorldId, &dbRet)) {
        blood::LogWrite(0, L"AccountInventoryDaemon.cpp", 0xd4,
                        L"[%s], failed to check world id, dbRetValue:%d, configWorldId:%d",
                        GetLogTag(), dbRet, config_->serviceWorldId);
        return false;
    }
    appDb_ = new AccountInventoryDB(db_);
    SetHandlerApp(appDb_, config_);

    blood::LogWrite(1, L"AccountInventoryDaemon.cpp", 0x26a,
                    L"[%s], initial job-id seed value: 0x%016I64X", GetLogTag(), (blood::u64)0);
    return true;
}

bool AccountInventoryDaemon::initNetwork() {
    blood::Network* net = blood::Network::Instance();
    if (!net) return false;
    if (!config_->lobbyListenPort || !config_->worldListenPort || !config_->manageListenPort) {
        blood::LogWrite(0, L"AccountInventoryDaemon.cpp", 0x248,
                        L"[%s], failed to initialize network", GetLogTag());
        return false;
    }
    // Port ids and order match the original (lobby=2, world=3, manage=4).
    if (!net->OpenPort(2, (blood::u16)config_->lobbyListenPort,
                       new LobbySessionHandler(), new blood::TrustedPacketBufferHandler()) ||
        !net->OpenPort(3, (blood::u16)config_->worldListenPort,
                       new WorldSessionHandler(), new blood::TrustedPacketBufferHandler()) ||
        !net->OpenPort(4, (blood::u16)config_->manageListenPort,
                       new ManageSessionHandler(), new blood::TrustedPacketBufferHandler())) {
        blood::LogWrite(0, L"AccountInventoryDaemon.cpp", 0x248,
                        L"[%s], failed to initialize network", GetLogTag());
        return false;
    }
    net->StartPort(2);
    net->StartPort(3);
    net->StartPort(4);
    blood::LogWrite(3, L"AccountInventoryDaemon.cpp", 0x272,
                    L"[%s], open world lobby ok, port:%d", GetLogTag(), config_->lobbyListenPort);
    blood::LogWrite(3, L"AccountInventoryDaemon.cpp", 0x29d,
                    L"[%s], open world port ok, port:%d", GetLogTag(), config_->worldListenPort);
    blood::LogWrite(3, L"AccountInventoryDaemon.cpp", 0x2b0,
                    L"[%s], open manage port ok, port:%d", GetLogTag(), config_->manageListenPort);
    return true;
}

bool AccountInventoryDaemon::onStart() {
    blood::LogWrite(1, L"AccountInventoryDaemon.cpp", 0x2c0, L"[%s], start np channel ok", GetLogTag());
    return true;
}

void AccountInventoryDaemon::shutdown() {
    blood::LogWrite(1, L"AccountInventoryDaemon.cpp", 0x2e0, L"[%s], shutdown", GetLogTag());
    if (db_) db_->Disconnect();
}

bool AccountInventoryDaemon::reload(const wchar_t* what, int kind) {
    (void)what; (void)kind;
    blood::LogWrite(1, L"AccountInventoryDaemon.cpp", 0x2d0, L"[%s], reload(%s) success",
                    GetLogTag(), L"all");
    return true;
}

} // namespace accountinventory
