// AccountInventoryDaemon process entry.
// The original parses CLI args only when a console exists (no console -> service mode), and
// checks --version before everything else. blood::DaemonMain implements exactly that.
#include "AccountInventoryDaemon.h"
#include "../BloodLib/Daemon.h"

namespace accountinventory {
namespace {
AccountInventoryDaemon g_daemon;   // stack/static object, like the original's local daemon
}

int DaemonEntry(int argc, wchar_t** argv, wchar_t** envp) {
    return blood::DaemonMain(argc, argv, envp, NULL);
}

} // namespace accountinventory

int wmain(int argc, wchar_t** argv, wchar_t** envp) {
    return accountinventory::DaemonEntry(argc, argv, envp);
}
