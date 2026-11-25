import io
p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\AccountInventory\Handlers.cpp'
s = io.open(p, encoding='utf-8').read()
start = s.index('bool HandleGetGoodsCount(')
end = s.index('\n// The remaining commands in the original spawn transactional jobs', start)
new = r'''bool HandleGetGoodsCount(blood::Session* session, blood::Packet* packet, int cmd) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    if (!blood::Packet_Decompose(packet, "hqo", &wireCmd, &tid, &account.bytes)) return false;
    AccountInventoryDB* db = AccountGlobals::db;
    i64 goods = 0, items = 0;
    int ret = db ? db->RetrieveWarehouseItemCount(account, 1, &goods, &items) : -1;
    // With NP disabled the original replies cmd 6/9 with reason 5 (NP not available).
    SendFmt(session, "hqh", cmd + 2, (u64)tid, ret < 0 ? 7 : 5);
    return true;
}

bool HandleServiceItemList(blood::Session* session, blood::Packet* packet) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0, key = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    blood::u8 searchType = 0;
    if (!blood::Packet_Decompose(packet, "hqoQq", &wireCmd, &tid, &account.bytes,
                                 &searchType, &key)) return false;
    AccountInventoryDB* db = AccountGlobals::db;
    const AppConfig* cfg = AccountGlobals::cfg;
    if (!db || searchType == 0 || (searchType >= 2 && key == 0)) {
        SendFmt(session, "hqh", 15, (u64)tid, 7);
        return false;
    }
    std::vector<LobbyServiceItemRecord> items;
    int ret = db->RetrieveLobbyServiceItemList(account,
        cfg ? cfg->lobbyServiceItemSearchCount : 100, searchType, (i64)key, &items);
    if (ret != 0) {
        SendFmt(session, "hqh", 15, (u64)tid, 3);
        return false;
    }
    SendFmt(session, "hiqih", 14, 14, (u64)tid,
            cfg ? cfg->lobbyServiceItemSearchCount : 100, (int)items.size());
    blood::LogWrite(1, L"LobbyPacketHandler.cpp", 0x183,
        L"[%s], lobby service item, tid:0x%I64X, game-account-id:%s, count:%d",
        L"lobbyd", (u64)tid, account.Text().c_str(), (int)items.size());
    return true;
}

bool HandleItemInfo(blood::Session* session, blood::Packet* packet) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0, instance = 0, pcId = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    if (!blood::Packet_Decompose(packet, "hqqoq", &wireCmd, &tid, &instance,
                                 &account.bytes, &pcId)) return false;
    AccountInventoryDB* db = AccountGlobals::db;
    SingleWarehouseItemRecord item;
    memset(&item, 0, sizeof(item));
    int ret = db ? db->RetrieveSingleWarehouseItem((i64)instance, account, (i64)pcId, &item) : -1;
    if (ret != 0) {
        SendFmt(session, "hqE", 17, (u64)instance, 3);
        return false;
    }
    SendFmt(session, "hqqqqiQ", 17, (u64)tid, (u64)0, (u64)item.itemInstanceId,
            (u64)item.labelId, (int)item.itemDataId, (int)item.itemState);
    blood::LogWrite(1, L"LobbyPacketHandler.cpp", 0x196,
        L"[%s], lobby service item info, sent success ack, cmd:%d, item-instance-id:%I64d",
        L"lobbyd", 16, (u64)item.itemInstanceId);
    return true;
}

bool HandleItemConsume(blood::Session* session, blood::Packet* packet, int cmd) {
    blood::u16 wireCmd = 0;
    blood::u64 label = 0, instance = 0, unused = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    if (!blood::Packet_Decompose(packet, "hqqqo", &wireCmd, &label, &instance,
                                 &unused, &account.bytes)) return false;
    AccountInventoryDB* db = AccountGlobals::db;
    int ret = -1;
    if (db) {
        if (cmd == 19) ret = db->TryWarehouseItemConsumption((i64)label, (i64)instance, account);
        else if (cmd == 22) ret = db->FinishWarehouseItemConsumption(true, (i64)label, (i64)instance, account);
        else if (cmd == 25) ret = db->CancelWarehouseItemConsumption((i64)label, (i64)instance, account);
        else ret = db->ConsumeWarehouseItem((i64)label, (i64)instance, account);
    }
    SendFmt(session, "hqE", cmd + 2, (u64)instance, ret == 0 ? 0 : 7);
    blood::LogWrite(1, L"LobbyPacketHandler.cpp", 0x196,
        L"[%s], consume item, account-id:%s, label-id:%I64d, item-instance-id:%I64d, dbRetValue:%d",
        L"lobbyd", account.Text().c_str(), (u64)label, (u64)instance, ret);
    return ret == 0;
}
'''
s = s[:start] + new + s[end:]
io.open(p, 'w', encoding='utf-8').write(s)
print('restored DB-backed handlers with correct varargs')
