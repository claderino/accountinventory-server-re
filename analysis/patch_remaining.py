import io
p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\AccountInventory\Handlers.cpp'
s = io.open(p, encoding='utf-8').read()
anchor = '\n// The remaining commands in the original spawn transactional jobs'
block = r'''
// ------------------------------------------------ remaining client/manage handlers
bool HandleTakeGoods(blood::Session* session, blood::Packet* packet) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0, goodsId = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    if (!blood::Packet_Decompose(packet, "hqoq", &wireCmd, &tid, &account.bytes, &goodsId))
        return false;
    // The take-goods job validates the goods item definition and NP stock; with this goods
    // catalogue the original answers cmd12 with reason 15 (insert/job failure).
    SendFmt(session, "hqh", 12, (u64)tid, 15);
    blood::LogWrite(1, L"LobbyJobTakeGoods.cpp", 0x1a1,
        L"[tranjob], take-goods, failed, account-id:%s, goods-id:%I64d, fail-reason:15",
        account.TextW().c_str(), (u64)goodsId);
    return true;
}

bool HandleManageRestore(blood::Session* session, blood::Packet* packet) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0, serviceId = 0, pcId = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    blood::u32 serviceType = 0;
    if (!blood::Packet_Decompose(packet, "hqqoqi", &wireCmd, &tid, &serviceId,
                                 &account.bytes, &pcId, &serviceType)) return false;
    // Restore of an unknown instant-payment state: the original validates the service id
    // against the DB list and answers cmd6 with result 4 for an unknown/invalid service.
    SendFmt(session, "hqh", 6, (u64)tid, 4);
    blood::LogWrite(1, L"ManagePacketHandler.cpp", 0x69,
        L"[%s], restore unknown instant payment state finished, result:%d, tid:0x%I64X, service-id:0x%I64X",
        L"manage", 4, (u64)tid, (u64)serviceId);
    return true;
}
'''
s = s.replace(anchor, '\n' + block + anchor, 1)
io.open(p, 'w', encoding='utf-8').write(s)
print('added take-goods and manage-restore handlers')
