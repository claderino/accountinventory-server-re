// BloodLib - the 23 AchievementDaemon configuration definitions (original table RVA 0x0B4FC0).
#include "Daemon.h"
#include <stddef.h>

SOURCE_FILE("ConfigOptions.cpp");

namespace blood {

static_assert(offsetof(XmlConfigDef, offset) == 0x00, "XmlConfigDef ABI");
static_assert(offsetof(XmlConfigDef, name) == 0x08, "XmlConfigDef ABI");
static_assert(offsetof(XmlConfigDef, type) == 0x10, "XmlConfigDef ABI");
static_assert(offsetof(XmlConfigDef, flags) == 0x14, "XmlConfigDef ABI");
static_assert(offsetof(XmlConfigDef, defaultValue) == 0x18, "XmlConfigDef ABI");
static_assert(offsetof(XmlConfigDef, description) == 0x20, "XmlConfigDef ABI");
static_assert(sizeof(XmlConfigDef) == 0x28, "XmlConfigDef ABI");

namespace {
const i32 kDefaultInternalPort = 22200;
const i32 kDefaultManagePort = 32200;
const i32 kDefaultCheckPingTime = 30000;
const i32 kDefaultLogCodePage = 0;
const i32 kDefaultDumpType = 2;
const i32 kDefaultMaxPcCount = 400000;
const i32 kDefaultUpdateCycle = 3000;
const void* const kDefaultTrue = reinterpret_cast<const void*>(static_cast<uintptr_t>(1));
}

const XmlConfigDef kDaemonConfigDefs[23] = {
    { -1, L"",                         0, 0, NULL, L"path" },
    {  0, L"root-path",                7, 1, NULL, L"full root path" },
    { 16, L"log-path",                 7, 1, NULL, L"full log path" },
    {  8, L"config-path",              7, 1, NULL, L"full config path" },
    { 24, L"data-path",                7, 1, NULL, L"full data path" },
    { -1, L"",                         0, 0, NULL, L"certification" },
    { 32, L"cert",                     8, 1, NULL, L"certification file path" },
    { -1, L"",                         0, 0, NULL, L"network configuration" },
    { 52, L"internal-port",            1, 0, &kDefaultInternalPort, L"daemon internal access port" },
    { 56, L"manage-port",              1, 0, &kDefaultManagePort, L"ManagementDaemon access port" },
    { 40, L"alert-send-address",       6, 0, NULL, L"alert log send address (ip:port)" },
    { 72, L"ping-enable",              4, 0, kDefaultTrue, L"ping 사용 여부. default:yes" },
    { 68, L"check-ping-time",          1, 0, &kDefaultCheckPingTime,
      L"해당 시간 동안 ping패킷이 오지 않으면 접속 해제. default:30000 (30sec)" },
    { 73, L"disconnect-when-ping-timeout", 4, 0, kDefaultTrue,
      L"옵션에 설정된 시간동안 ping패킷이 오지 않았을 때 접속 해제 유무. default:yes" },
    { -1, L"",                         0, 0, NULL, L"system configuration" },
    { 48, L"log-codepage",             1, 1, &kDefaultLogCodePage,
      L"log file codepage, write numeric value only. etc) CP_ACP(0, default), CP_949(949), CP_UTF8(65001)" },
    { 64, L"dump-type",                1, 0, &kDefaultDumpType,
      L"0: no dump, 1: minimal dump, 2: full dump(default)" },
    { 60, L"worker-thread",            1, 0, NULL, L"0 is system recommend" },
    { 76, L"event-thread",             1, 0, NULL, L"0 is system recommend" },
    { 80, L"max-pc-count",             1, 0, &kDefaultMaxPcCount,
      L"max pc count (total universe), default:400,000" },
    { 84, L"update-cycle",             1, 0, &kDefaultUpdateCycle,
      L"update cycle (db/client), default:3000ms" },
    { -1, L"",                         0, 0, NULL, L"debug configuration" },
    { 88, L"log-register-change",      4, 0, NULL, L"if set y, log when register updated" },
};

} // namespace blood
