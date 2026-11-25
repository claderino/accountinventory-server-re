import io
p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\AccountInventory\Handlers.cpp'
s = io.open(p, encoding='utf-8').read()
s = s.replace('bool HandleListGoods(blood::Session* session, blood::Packet* packet);',
              'bool HandleListGoods(blood::Session* session, blood::Packet* packet, bool world);')
s = s.replace('bool HandleListGoods(blood::Session* session, blood::Packet* packet) {',
              'bool HandleListGoods(blood::Session* session, blood::Packet* packet, bool world) {')
s = s.replace('else if (cmd == 7) ok = HandleListGoods(s, p);',
              'else if (cmd == 7) ok = HandleListGoods(s, p, false);')
s = s.replace('''    w.u32(goods.empty() ? 10u : 261u);''',
              '''    w.u32(goods.empty() ? 10u : (world ? 281u : 261u));''')
s = s.replace('''            static const unsigned char itemData1[8] = {0,0,0,0,0,0,0,0};''',
              '''            if (world) w.u32(0);
            static const unsigned char itemData1[8] = {0,0,0,0,0,0,0,0};''')
# world dispatcher: replace HandleDeferred block with real handlers
old = '''    if (cmd == 1) ok = HandleWorldHello(s, p);
    else if (cmd == 4 || cmd == 7) ok = HandleGetGoodsCount(s, p, cmd);
    else ok = HandleDeferred(s, p, L"gamed", L"GamedPacketHandler.cpp", 0x4a0);'''
new = '''    if (cmd == 1) ok = HandleWorldHello(s, p);
    else if (cmd == 4) ok = HandleGetGoodsCount(s, p, cmd);
    else if (cmd == 7) ok = HandleListGoods(s, p, true);
    else if (cmd == 13) ok = HandleWorldSimpleAck(s, p, 15, 7);
    else if (cmd == 16) ok = HandleWorldItemInfo(s, p);
    else if (cmd == 19) ok = HandleWorldConsume(s, p, 21, 2);
    else if (cmd == 22) ok = HandleWorldFinishConsume(s, p);
    else if (cmd == 34) ok = HandleWorldLogoutAck(s, p);
    else if (cmd == 10 || cmd == 25 || cmd == 28 || cmd == 31) ok = true; // oracle: no reply
    else ok = HandleDeferred(s, p, L"gamed", L"GamedPacketHandler.cpp", 0x4a0);'''
assert old in s
s = s.replace(old, new)
# add forward declarations
s = s.replace('bool HandleListGoods(blood::Session* session, blood::Packet* packet, bool world);',
'''bool HandleListGoods(blood::Session* session, blood::Packet* packet, bool world);
bool HandleWorldSimpleAck(blood::Session* s, blood::Packet* p, int replyCmd, int reason);
bool HandleWorldItemInfo(blood::Session* s, blood::Packet* p);
bool HandleWorldConsume(blood::Session* s, blood::Packet* p, int replyCmd, int reason);
bool HandleWorldFinishConsume(blood::Session* s, blood::Packet* p);
bool HandleWorldLogoutAck(blood::Session* s, blood::Packet* p);''')
# appendworld handlers before HandleDeferred
anchor = '\n// The remaining commands in the original spawn transactional jobs'
block = r'''
bool HandleWorldSimpleAck(blood::Session* s, blood::Packet* p, int replyCmd, int reason) {
    blood::u16 wireCmd = 0; blood::u64 tid = 0;
    if (!blood::Packet_Decompose(p, "hq", &wireCmd, &tid)) return false;
    SendFmt(s, "hqh", replyCmd, (u64)tid, reason);
    return true;
}
bool HandleWorldItemInfo(blood::Session* s, blood::Packet* p) {
    blood::u16 wireCmd = 0; blood::u64 tid = 0, instance = 0;
    if (!blood::Packet_Decompose(p, "hqq", &wireCmd, &tid, &instance)) return false;
    SendFmt(s, "hiqh", 17, 10, (u64)tid, 0);
    return true;
}
bool HandleWorldConsume(blood::Session* s, blood::Packet* p, int replyCmd, int reason) {
    blood::u16 wireCmd = 0; blood::u64 instance = 0;
    if (!blood::Packet_Decompose(p, "hqq", &wireCmd, &instance, &instance)) return false;
    SendFmt(s, "hqE", replyCmd, (u64)instance, reason);
    return true;
}
bool HandleWorldFinishConsume(blood::Session* s, blood::Packet* p) {
    blood::u16 wireCmd = 0; blood::u64 instance = 0;
    if (!blood::Packet_Decompose(p, "hqq", &wireCmd, &instance, &instance)) return false;
    // Oracle-verified 28-byte reply: q(instance), u32(0), u64(0), u16(0), u16(2).
    unsigned char body[28]; memset(body, 0, sizeof(body));
    memcpy(body, &instance, 8);
    body[24] = 2;
    SendRawBody(s, 24, std::vector<unsigned char>(body, body + sizeof(body)));
    return true;
}
bool HandleWorldLogoutAck(blood::Session* s, blood::Packet* p) {
    blood::u16 wireCmd = 0; blood::u64 tid = 0, value = 0;
    if (!blood::Packet_Decompose(p, "hqq", &wireCmd, &tid, &value)) return false;
    SendFmt(s, "hqqh", 36, (u64)tid, (u64)value, 6);
    return true;
}
'''
s = s.replace(anchor, '\n' + block + anchor, 1)
io.open(p, 'w', encoding='utf-8').write(s)
print('world handlers added')
