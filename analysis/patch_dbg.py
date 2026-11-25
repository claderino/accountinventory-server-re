import io
p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\AccountInventory\Handlers.cpp'
s = io.open(p, encoding='utf-8').read()
start = s.index('bool HandleGetGoodsCount(')
end = s.index('\nbool HandleServiceItemList(', start)
new = '''bool HandleGetGoodsCount(blood::Session* session, blood::Packet* packet, int cmd) {
    (void)packet;
    SendFmt(session, "hqh", cmd + 2, (u64)0, 5);
    return true;
}
'''
s = s[:start] + new + s[end:]
io.open(p, 'w', encoding='utf-8').write(s)
print('patched HandleGetGoodsCount to minimal')
