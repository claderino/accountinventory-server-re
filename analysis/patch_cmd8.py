import io
p = r'C:\serverside_re\accountinventory\AccountInventoryDaemon\src\AccountInventory\Handlers.cpp'
s = io.open(p, encoding='utf-8').read()
anchor = '\n// The remaining commands in the original spawn transactional jobs'
block = r'''
// -------------------------------------------------------------- cmd8 goods list (NP + DB)
namespace {
struct ByteWriter {
    std::vector<unsigned char> d;
    void u8(unsigned char v) { d.push_back(v); }
    void u16(unsigned short v) { u8(v & 0xff); u8(v >> 8); }
    void u32(unsigned int v) { u16(v & 0xffff); u16(v >> 16); }
    void u64(unsigned long long v) { u32((unsigned int)v); u32((unsigned int)(v >> 32)); }
    void bytes(const void* p, int n) {
        const unsigned char* b = (const unsigned char*)p;
        for (int i = 0; i < n; ++i) d.push_back(b[i]);
    }
    void str(const std::wstring& s) {
        u16((unsigned short)s.size());
        for (size_t i = 0; i < s.size(); ++i) u16((unsigned short)s[i]);
    }
    void rawBytes(const std::string& s) { u16((unsigned short)s.size()); bytes(s.data(), (int)s.size()); }
};

int Base64Decode(const std::string& in, std::string* out) {
    static const char* tbl = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    out->clear();
    int vals[4], n = 0;
    for (size_t i = 0; i < in.size(); ++i) {
        char c = in[i];
        if (c == '=') break;
        const char* p = strchr(tbl, c);
        if (!p) continue;
        vals[n++] = (int)(p - tbl);
        if (n == 4) {
            out->push_back((char)((vals[0] << 2) | (vals[1] >> 4)));
            out->push_back((char)((vals[1] << 4) | (vals[2] >> 2)));
            out->push_back((char)((vals[2] << 6) | vals[3]));
            n = 0;
        }
    }
    if (n == 3) {
        out->push_back((char)((vals[0] << 2) | (vals[1] >> 4)));
        out->push_back((char)((vals[1] << 4) | (vals[2] >> 2)));
    } else if (n == 2) {
        out->push_back((char)((vals[0] << 2) | (vals[1] >> 4)));
    }
    return (int)out->size();
}

long long ParseIsoDate(const std::string& s) {
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0, oh = 0, om = 0;
    char sign = '+';
    if (sscanf_s(s.c_str(), "%d-%d-%dT%d:%d:%d%c%d:%d", &y, &mo, &d, &h, &mi, &se,
                 &sign, 1, &oh, &om) < 6) return -1;
    struct tm t; memset(&t, 0, sizeof(t));
    t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
    t.tm_hour = h; t.tm_min = mi; t.tm_sec = se;
    long long tt = (long long)_mkgmtime(&t);
    int off = (oh * 60 + om) * 60;
    tt += (sign == '+') ? -off : off;
    return tt;
}

std::string XmlVal(const std::string& xml, const std::string& tag) {
    std::string open = "<" + tag + ">";
    size_t p = xml.find(open);
    if (p == std::string::npos) return std::string();
    p += open.size();
    size_t e = xml.find("</" + tag + ">", p);
    if (e == std::string::npos) return std::string();
    return xml.substr(p, e - p);
}
std::wstring Utf8ToWideLocal(const std::string& s) {
    if (s.empty()) return std::wstring();
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), NULL, 0);
    std::wstring w(n, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}

// Send a pre-built body (starting with the command word) without the field engine.
void SendRawBody(blood::Session* s, blood::u16 cmd, const std::vector<unsigned char>& body) {
    blood::PacketBuffer* buf = blood::PacketBuffer::Acquire();
    if (!buf) return;
    unsigned char* p = buf->Fields();
    memcpy(p, &cmd, 2);
    if (!body.empty()) memcpy(p + 2, &body[0], body.size());
    buf->size = (blood::u16)(2 + 2 + body.size());
    blood::Packet pkt;
    pkt.splitter = NULL; pkt.current = pkt.first = pkt.last = buf;
    blood::Session::SendPacketPtr(s, &pkt);
    buf->Release(kSourceFile, 0);
}
} // namespace

bool HandleListGoods(blood::Session* session, blood::Packet* packet) {
    blood::u16 wireCmd = 0;
    blood::u64 tid = 0;
    AccountId account;
    memset(&account, 0, sizeof(account));
    blood::u8 searchType = 0;
    if (!blood::Packet_Decompose(packet, "hqoc", &wireCmd, &tid, &account.bytes, &searchType))
        return false;
    NpClient* np = AccountGlobals::np;
    const AppConfig* cfg = AccountGlobals::cfg;
    if (!np || !np->IsConnected()) {
        SendFmt(session, "hqh", 9, (u64)tid, 5);
        return false;
    }
    std::string reply;
    int maxCount = cfg ? cfg->goodsListMaxCount : 100;
    if (!np->List(account, maxCount, &reply) || reply.find("<Reply>") == std::string::npos) {
        SendFmt(session, "hqh", 9, (u64)tid, 3);
        return false;
    }
    std::vector<NpWarehouseGoods> goods;
    size_t pos = 0;
    while ((pos = reply.find("<Goods>", pos)) != std::string::npos) {
        size_t end = reply.find("</Goods>", pos);
        if (end == std::string::npos) break;
        std::string g = reply.substr(pos, end - pos);
        NpWarehouseGoods rec;
        memset(&rec, 0, sizeof(rec));
        rec.warehouseId = _atoi64(XmlVal(g, "EnumeratedGoodsWarehouseId").c_str());
        rec.goodsNumber = atoi(XmlVal(g, "GoodsId").c_str());
        rec.refundable = XmlVal(g, "IsRefundable") == "true";
        rec.refundExpirationDate = XmlVal(g, "IsRefundExpirationDate") == "true";
        rec.refundUntil = XmlVal(g, "RefundEffectiveUntil");
        rec.deliveryDate = XmlVal(g, "DeliveryDate");
        rec.senderDescription = XmlVal(g, "SenderDescription");
        rec.giftMessage = XmlVal(g, "GiftMessage");
        rec.gameServerId = atoi(XmlVal(g, "GameServerId").c_str());
        rec.characterId = atoi(XmlVal(g, "CharacterId").c_str());
        size_t ipos = 0;
        while ((ipos = g.find("<Item>", ipos)) != std::string::npos) {
            size_t iend = g.find("</Item>", ipos);
            if (iend == std::string::npos) break;
            std::string it = g.substr(ipos, iend - ipos);
            NpWarehouseItem ir;
            ir.itemId = atoi(XmlVal(it, "ItemId").c_str());
            ir.quantity = atoi(XmlVal(it, "ItemQuantity").c_str());
            ir.gameItemKey = atoi(XmlVal(it, "GameItemKey").c_str());
            ir.gameItemIdB64 = XmlVal(it, "GameItemId");
            ir.gameItemDataB64 = XmlVal(it, "GameItemData");
            rec.items.push_back(ir);
            ipos = iend + 7;
        }
        rec.itemCount = (int)rec.items.size();
        goods.push_back(rec);
        pos = end + 8;
    }

    ByteWriter w;
    // The first u32 is a list-state value observed as 261 with goods / 10 without.
    w.u32(goods.empty() ? 10u : 261u);
    w.u64(tid);
    w.u16((unsigned short)goods.size());
    for (size_t gi = 0; gi < goods.size(); ++gi) {
        const NpWarehouseGoods& g = goods[gi];
        w.u64((unsigned long long)g.warehouseId);
        w.u32((unsigned int)g.goodsNumber);
        w.u8(g.refundable ? 1 : 0);
        w.u8(g.refundExpirationDate ? 1 : 0);
        long long refund = ParseIsoDate(g.refundUntil);
        w.u64(refund <= 0 ? (unsigned long long)-1 : (unsigned long long)refund);
        w.u64((unsigned long long)ParseIsoDate(g.deliveryDate));
        w.str(Utf8ToWideLocal(g.senderDescription));
        w.str(Utf8ToWideLocal(g.giftMessage));
        w.u32((unsigned int)g.gameServerId);
        w.str(np->GoodsName(g.goodsNumber));
        w.str(std::wstring());
        w.rawBytes(std::string());
        static const unsigned char tail[5] = {0,0,0,0,1};
        w.u16(5); w.bytes(tail, 5);
        w.u16((unsigned short)g.items.size());
        for (size_t ii = 0; ii < g.items.size(); ++ii) {
            const NpWarehouseItem& it = g.items[ii];
            w.u32((unsigned int)it.itemId);
            w.u32((unsigned int)it.quantity);
            w.u32((unsigned int)it.gameItemKey);
            std::string b1, b2;
            Base64Decode(it.gameItemIdB64, &b1);
            Base64Decode(it.gameItemDataB64, &b2);
            w.rawBytes(b1);
            w.rawBytes(b2);
        }
    }
    SendRawBody(session, 8, w.d);
    blood::LogWrite(1, L"GoodsList.cpp", 0x38c,
        L"[list-goods] game-account:%s, goods-count:%d", account.TextW().c_str(), (int)goods.size());
    return true;
}
'''
s = s.replace(anchor, '\n' + block + anchor, 1)
io.open(p, 'w', encoding='utf-8').write(s)
print('inserted cmd8 handler')
