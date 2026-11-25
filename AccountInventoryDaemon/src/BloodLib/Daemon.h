// BloodLib - daemon base class, packed ABI, and process entry (original: Daemon.h).
#pragma once
#include "BloodLib.h"
#include "Timer.h"
#include "XmlReader.h"
#include <time.h>

namespace blood {

class Daemon;
class Log;
class LogBuffer;
class DaemonConfig;
class DelayedRelease;

class Console {
public:
    virtual ~Console() {}
    virtual bool Open() = 0;
    virtual void Close() = 0;
    virtual void SetState(int state) = 0;
    virtual void SetTitle(const wchar_t* title) = 0;
    virtual void Print(int level, const wchar_t* format, ...) = 0;
    virtual bool WriteLog(LogBuffer* record) { (void)record; return false; }
    virtual bool IsWindowConsole() const { return false; }
    virtual HWND Hwnd() const { return NULL; }
};

Console* CreateConsole(int runMode);
Console* CreateWindowConsole();
void SetLogConsole(Console* console);   // mirror log lines into the active console
void SetConsoleStopEvent(HANDLE stopEvent);   // window console exit/shutdown callback
Console* CreateServiceConsole();
bool runAsServiceConsole(Daemon* daemon);
void ServiceMainEntry(Daemon* daemon);
bool InstallService(Daemon* daemon, const wchar_t* postfix, const wchar_t* confDir);
bool UninstallService(Daemon* daemon, const wchar_t* postfix);
void StartPipeConsole(Daemon* daemon);
void StopPipeConsole();
void StartPerfCounters(Daemon* daemon);
void StopPerfCounters();
void SetPerfCounterValue(int id, u64 value);

extern const XmlConfigDef kDaemonConfigDefs[23];
const int kDaemonConfigDefCount = 23;
const size_t kDaemonConfigSize = 0x60;

class DaemonConfig {
public:
    DaemonConfig();
    ~DaemonConfig();

    bool Load(const wchar_t* configPathOrFile, XmlReaderApi* xml = NULL);
    void PrintAll();
    const DaemonConfigData* Data() const { return data_; }
    void* RawData() const { return buffer_; }

    const wchar_t* RootPath() const;
    const wchar_t* ConfigPath() const;
    const wchar_t* LogPath() const;
    const wchar_t* DataPath() const;
    const char* CertFile() const;
    const wchar_t* AlertAddress() const;
    int LogCodePage() const;
    int InternalPort() const;
    int ManagePort() const;
    int WorkerThread() const;
    int DumpType() const;
    int CheckPingTime() const;
    bool PingEnable() const;
    bool DisconnectWhenPingTimeout() const;
    int EventThread() const;
    int MaxPcCount() const;
    int UpdateCycle() const;
    bool LogRegisterChange() const;

private:
    DaemonConfig(const DaemonConfig&);
    DaemonConfig& operator=(const DaemonConfig&);
    void* buffer_;
    DaemonConfigData* data_;
};

#pragma pack(push, 1)
struct DaemonAbi {
    Console* console;                 // absolute +0x08
    Log* log;                         // +0x10
    ThreadManager* threads;           // +0x18
    i32 state;                        // +0x20
    u32 pad24;
    HANDLE stopEvent;                 // +0x28
    DWORD startTick;                  // +0x30
    u32 pad34;
    u64 baseTick;                     // +0x38
    volatile LONG loopCounter;      // +0x40
    u32 pad44;
    time_t startTime;                 // +0x48
    u8 pad48[3];
    in_addr internalNumber;           // +0x53
    u8 pad4F;
    wchar_t version[20];              // +0x58
    u8 pad78[0x18];
    wchar_t hostname[256];            // +0x98
    u8 useConfiguredIp;               // +0x298
    u8 pad299[3];
    wchar_t publicAddress[20];        // +0x29C
    in_addr publicNumber;             // +0x2C4
    wchar_t internalAddress[20];      // +0x2C8
    wchar_t configPath[0x82];        // +0x2F0 (0x104 bytes)
    u8 pad2EC[4];
    wchar_t exeDirectory[0x82];      // +0x3F8 (0x104 bytes)
    u8 pad4F4[0x204];
    u32 internalMask;                 // +0x700
    u32 reserved704;                  // +0x704
    u32 publicMask;                   // +0x708
    u32 reserved70C;                  // +0x70C
    u16 addressFlags;                 // +0x710
    u8 pad712[6];
    TimerQueue* timers;               // +0x718
    DelayedRelease* delayedRelease;   // +0x720
    DaemonConfig* config;             // +0x728
    LogFile* fileMedia;               // +0x730
    void* achievementLog;             // +0x738
    void* eventManager;               // +0x740
    void* counterSet;                 // +0x748
    u8 application[0x60];             // +0x750..+0x7AF
};
#pragma pack(pop)

class Daemon {
public:
    Daemon();
    virtual ~Daemon();

    // Exact 16-slot Achievement vtable order.
    virtual const wchar_t* GetServiceName() = 0; // +0x08
    virtual const wchar_t* GetLogTag() = 0;      // +0x10
    virtual const wchar_t* GetName() = 0;        // +0x18
    virtual const wchar_t* GetDescription() = 0; // +0x20
    virtual int GetTimerInterval() { return 1000; } // +0x28
    virtual int GetTimerMaxTime() { return 20; }    // +0x30
    virtual void PrintVersion() = 0;             // +0x38
    virtual bool initLog() = 0;                  // +0x40
    virtual bool setStatus(int status) { return true; } // +0x48
    virtual bool initialize() = 0;               // +0x50
    virtual bool initNetwork() = 0;              // +0x58
    virtual bool onStart() = 0;                  // +0x60
    virtual void shutdown() = 0;                 // +0x68
    virtual bool reload(const wchar_t* what, int kind) = 0; // +0x70
    virtual bool cooldown() { return true; }     // +0x78

    static Daemon* Instance();
    bool Reload(const wchar_t* what, int kind);
    bool Cooldown();
    int State() const { return abi_.state; }
    void SetState(int state);
    const wchar_t* Version() const { return abi_.version; }
    time_t StartTime() const { return abi_.startTime; }
    DWORD StartTick() const { return abi_.startTick; }
    i32 LoopTick() const { return abi_.loopCounter; }
    HANDLE StopEvent() const { return abi_.stopEvent; }
    void RequestStop();
    const wchar_t* PublicAddress() const { return abi_.publicAddress; }
    const wchar_t* InternalAddress() const { return abi_.internalAddress; }
    const wchar_t* ConfigPath() const { return abi_.configPath; }
    void SetConfigPath(const wchar_t* path);
    const wchar_t* ExeDirectory() const { return abi_.exeDirectory; }
    const wchar_t* HostName() const { return abi_.hostname; }
    TimerQueue* Timers() { return abi_.timers; }
    ThreadManager* Threads() { return abi_.threads; }
    DelayedRelease* DelayedReleases() { return abi_.delayedRelease; }
    Console* GetConsole() const { return abi_.console; }
    Log* GetLog() const { return abi_.log; }
    DaemonConfig* GetConfig() const { return abi_.config; }
    void SetConfig(DaemonConfig* config) { abi_.config = config; }
    LogFile* GetFileMedia() const { return abi_.fileMedia; }
    void SetFileMedia(LogFile* media) { abi_.fileMedia = media; }
    void* AchievementLog() const { return abi_.achievementLog; }
    void* EventManager() const { return abi_.eventManager; }
    void* CounterSet() const { return abi_.counterSet; }
    bool IsTerminated() const { return abi_.application[0] != 0; }

    bool start(Console* console, bool asService);
    bool run();
    void timerLoop();
    void selectNetworkAddresses();
    void readVersion();
    void setCwdToExeDir();

    DaemonAbi abi_;

private:
    Daemon(const Daemon&);
    Daemon& operator=(const Daemon&);
};

int DaemonMain(int argc, wchar_t** argv, wchar_t** environment, void* reserved);
Console* Daemon_CreateConsole();
bool Daemon_Start(Console* console, bool asService);
void Daemon_FormatPath(wchar_t* output, const wchar_t* format, ...);

} // namespace blood
