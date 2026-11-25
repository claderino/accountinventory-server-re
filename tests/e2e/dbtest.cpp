#include "../../AccountInventoryDaemon/src/AccountInventory/Db.h"
#include "../../AccountInventoryDaemon/src/AccountInventory/AccountInventoryDB.h"
#include <stdio.h>
using namespace accountinventory;
int wmain() {
    Db db;
    if (!db.Connect("DRIVER={ODBC Driver 17 for SQL Server};SERVER=localhost\\SQLEXPRESS;DATABASE=GameWarehouseDB;Trusted_Connection=yes;")) return 1;
    AccountInventoryDB adb(&db);
    AccountId a; memset(&a, 0, sizeof(a));
    unsigned char g[16] = {0x87,0xF6,0x44,0x71,0x7A,0xBD,0xA7,0x4C,0xB9,0x4A,0xC3,0xF1,0x96,0x44,0xCD,0xDF};
    memcpy(a.bytes, g, 16);
    printf("world=%d\n", adb.CheckWorldId(10070));
    SingleWarehouseItemRecord it; memset(&it,0,sizeof(it));
    printf("single=%d label=%I64d data=%d state=%d\n", adb.RetrieveSingleWarehouseItem(2834453015476308139,a,0,&it), it.labelId, it.itemDataId, it.itemState);
    printf("try=%d\n", adb.TryWarehouseItemConsumption(946,2834453015476308139,a));
    printf("cancel=%d\n", adb.CancelWarehouseItemConsumption(946,2834453015476308139,a));
    return 0;
}
