#include "../../AccountInventoryDaemon/src/AccountInventory/NpClient.h"
#include <stdio.h>
using namespace accountinventory;
int wmain() {
    NpClient np;
    printf("connect...\n"); fflush(stdout);
    bool ok = np.Connect("127.0.0.1", 6601, 1820, "bnsgrnTH");
    printf("connect=%d connected=%d\n", ok, np.IsConnected()); fflush(stdout);
    if (!np.IsConnected()) return 1;
    AccountId a; memset(&a, 0, sizeof(a));
    unsigned char g[16] = {0x87,0xF6,0x44,0x71,0x7A,0xBD,0xA7,0x4C,0xB9,0x4A,0xC3,0xF1,0x96,0x44,0xCD,0xDF};
    memcpy(a.bytes, g, 16);
    printf("count=%d\n", np.GetGoodsCount(a)); fflush(stdout);
    return 0;
}
