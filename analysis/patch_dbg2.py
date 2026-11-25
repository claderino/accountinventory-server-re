import io
p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\AccountInventory\Handlers.cpp'
s = io.open(p, encoding='utf-8').read()
start = s.index('bool HandleGetGoodsCount(')
end = s.index('\nbool HandleServiceItemList(', start)
new = '''bool HandleGetGoodsCount(blood::Session* session, blood::Packet* packet, int cmd) {
    (void)packet;
    blood::u64 tid = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    fprintf(stderr, "DBG before decompose\\n");
    if (!blood::Packet_Decompose(packet, "hqo", &tid, &account.bytes)) {
        fprintf(stderr,"DBG decompose fail\\n");
        SendFmt(session, "hqh", cmd + 2, (u64)0, 7);
        return false;
    }
    fprintf(stderr, "DBG tid=%I64u\\n", tid);
    SendFmt(session, "hqh", cmd + 2, (u64)tid, 5);
    return true;
}
'''
s = s[:start] + new + s[end:]
io.open(p, 'w', encoding='utf-8').write(s)
print('patched: decompose only')
