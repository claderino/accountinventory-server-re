// Temporary probe of the original XmlReader_sv64.dll (not part of the final DLL).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>

typedef int (WINAPI *VFn)(void*, ...);

template <typename R>
static R Call(void* obj, size_t slot) {
    void** vt = *(void***)obj;
    return reinterpret_cast<R (*)(void*)>(vt[slot / 8])(obj);
}
template <typename R, typename A1>
static R Call(void* obj, size_t slot, A1 a1) {
    void** vt = *(void***)obj;
    return reinterpret_cast<R (*)(void*, A1)>(vt[slot / 8])(obj, a1);
}
template <typename R, typename A1, typename A2>
static R Call(void* obj, size_t slot, A1 a1, A2 a2) {
    void** vt = *(void***)obj;
    return reinterpret_cast<R (*)(void*, A1, A2)>(vt[slot / 8])(obj, a1, a2);
}
template <typename R, typename A1, typename A2, typename A3>
static R Call(void* obj, size_t slot, A1 a1, A2 a2, A3 a3) {
    void** vt = *(void***)obj;
    return reinterpret_cast<R (*)(void*, A1, A2, A3)>(vt[slot / 8])(obj, a1, a2, a3);
}

template <typename R, typename A1, typename A2, typename A3, typename A4>
static R Call(void* obj, size_t slot, A1 a1, A2 a2, A3 a3, A4 a4) {
    void** vt = *(void***)obj;
    return reinterpret_cast<R (*)(void*, A1, A2, A3, A4)>(vt[slot / 8])(obj, a1, a2, a3, a4);
}

static void* WINAPI AllocCb(size_t n) { return malloc(n); }

struct Def28 {
    long long offset;
    const wchar_t* name;
    int a;
    int b;
    const wchar_t* def;
    const wchar_t* desc;
};

static const wchar_t* kNames[7] = {0, L"root-path", L"data-path", L"log-path", L"cert-file",
                                   L"worker-thread", L"timer-interval"};
static const long long kOffsets[7] = {-1, 0, 8, 0x18, 0x10, 0x30, 0x3C};
static const int kFlagsA[7] = {0, 7, 6, 7, 8, 1, 1};
static const int kTypeA[7] = {0, 1, 1, 1, 1, 0, 0};

static const wchar_t* kAttrNames[] = {
    L"a", L"b", L"id", L"name", L"Name", L"flag", L"v", L"val", L"MixedCase", L"mixedcase",
    L"apos", L"gt", L"lt", L"quot", L"num", L"empty", L"service-type", L"ip-address",
    L"internal-port", L"wid", L"value", L"module", L"type", L"version", L"release-module", 0};

static void DumpElement(void* node, int depth) {
    const wchar_t* name = Call<const wchar_t*>(node, 0x18);
    int line = Call<int>(node, 0x20);
    int nAttr = Call<int>(node, 0x28);
    void* ni = Call<void*>(node, 0x58);
    int valid = ni ? (Call<int>(ni, 8) & 1) : -1;
    int nKids = Call<int>(node, 0);
    printf("%*sE %ls line=%d attrs=%d kids=%d valid=%d\n", depth, "", name, line, nAttr, nKids, valid);
    for (int i = 0; kAttrNames[i]; ++i) {
        const wchar_t* av = Call<const wchar_t*>(node, 0x40, kAttrNames[i]);
        if (av) {
            int idx = Call<int>(node, 0x48, kAttrNames[i]);
            printf("%*s  A %ls=%ls idx=%d\n", depth, "", kAttrNames[i], av, idx);
        }
    }
    void* child = Call<void*>(node, 8);
    while (child) {
        DumpElement(child, depth + 2);
        child = Call<void*>(child, 0x10);
    }
}

static void ProbeTree(void* xml, const wchar_t* path) {
    printf("\n=== TREE %ls ===\n", path);
    void* doc = Call<void*>(xml, 0x30, path);
    printf("doc=%p\n", doc);
    if (!doc) return;
    void* root = Call<void*>(doc, 0x10);
    printf("root=%p\n", root);
    if (root) DumpElement(root, 0);
    Call<void>(xml, 0x40, doc);
}

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1) {
        const wchar_t* orig0 = L"C:\\serverside_re\\ranking\\analysis\\dll\\orig\\XmlReader_sv64.dll";
        HMODULE h0 = LoadLibraryW(orig0);
        typedef void* (*CFn0)(); typedef void (*DFn0)(void*);
        CFn0 cr0 = (CFn0)GetProcAddress(h0, "CreateXmlReader");
        DFn0 dr0 = (DFn0)GetProcAddress(h0, "DestroyXmlReader");
        void* x0 = cr0();
        printf("init=%d\n", Call<int>(x0, 0, (void*)0, (void*)0, (char)0) & 1);
        ProbeTree(x0, argv[1]);
        dr0(x0);
        FreeLibrary(h0);
        return 0;
    }
    const wchar_t* orig = L"C:\\serverside_re\\ranking\\analysis\\dll\\orig\\XmlReader_sv64.dll";
    HMODULE h = LoadLibraryW(orig);
    if (!h) { printf("LoadLibrary failed, err=%u\n", GetLastError()); return 1; }
    printf("loaded %p\n", (void*)h);
    typedef const wchar_t* (*VerFn)();
    typedef void* (*CreateFn)();
    typedef void (*DestroyFn)(void*);
    VerFn ver = (VerFn)GetProcAddress(h, "GetInterfaceVersion");
    CreateFn create = (CreateFn)GetProcAddress(h, "CreateXmlReader");
    DestroyFn destroyReader = (DestroyFn)GetProcAddress(h, "DestroyXmlReader");
    typedef void* (*CreateCfgFn)(void*);
    typedef void (*DestroyCfgFn)(void*);
    CreateCfgFn createCfg = (CreateCfgFn)GetProcAddress(h, "CreateXmlConfigReader");
    DestroyCfgFn destroyCfg = (DestroyCfgFn)GetProcAddress(h, "DestroyXmlConfigReader");
    printf("version=%ls\n", ver ? ver() : L"<none>");
    void* xml = create();
    printf("xml=%p init=%d\n", xml, Call<int>(xml, 0) & 1);
    ProbeTree(xml, L"C:\\serverside_re\\ranking\\RankingDaemon\\config\\game-list.config.xml");
    ProbeTree(xml, L"C:\\serverside_re\\ranking\\RankingDaemon\\config\\rankingd.config.xml");
    ProbeTree(xml, L"C:\\serverside_re\\ranking\\RankingDaemon\\data\\RankingRewardGroupData.xml");
    static const wchar_t* crafted[] = {
        L"crafted1.xml", L"entities.xml", L"doctype.xml", L"malformed.xml", L"malformed_attr.xml",
        L"utf16.xml", L"utf16nobom.xml", L"bom.xml", 0};
    for (int i = 0; crafted[i]; ++i) {
        wchar_t full[512];
        swprintf(full, 512, L"C:\\serverside_re\\ranking\\XmlReader_sv64\\tests\\data\\%ls", crafted[i]);
        ProbeTree(xml, full);
    }
    destroyReader(xml);
    FreeLibrary(h);
    return 0;
}
#if 0
    const wchar_t* cfgPath = L"C:\\serverside_re\\ranking\\RankingDaemon\\config\\rankingd.config.xml";
    for (int layout = 1; layout >= 0; --layout) {
        Def28 defs[7];
        memset(defs, 0, sizeof(defs));
        for (int i = 0; i < 7; ++i) {
            defs[i].offset = kOffsets[i];
            defs[i].name = kNames[i];
            defs[i].a = (layout == 0) ? kFlagsA[i] : kTypeA[i];
            defs[i].b = (layout == 0) ? kTypeA[i] : kFlagsA[i];
        }
        void* cfg = createCfg(xml);
        printf("\n=== CFG layout %d cfg=%p ===\n", layout, cfg);
        int ok = Call<int>(cfg, 8, defs, 7, &AllocCb, (size_t)0x78) & 1;
        printf("SetDefs=%d\n", ok);
        size_t outSize = 0;
        void* buf = Call<void*>(cfg, 0x28, cfgPath, L"rankingd", (char)0);
        const wchar_t* err = Call<const wchar_t*>(cfg, 0x30);
        printf("buf=%p err=%ls\n", buf, err ? err : L"<null>");
        if (buf) {
            unsigned char* p = (unsigned char*)buf;
            for (size_t i = 0; i < 0x78; i += 8) {
                printf("%04zx:", i);
                for (size_t j = 0; j < 8; ++j) printf(" %02X", p[i + j]);
                printf("\n");
            }
            for (size_t i = 0; i < 6; ++i) {
                const wchar_t** fp = (const wchar_t**)(p + kOffsets[i + 1]);
                if (i == 4 || i == 5) continue;
                printf("field %d ptr=%p val=%ls\n", i + 1, (void*)*fp, *fp ? *fp : L"<null>");
            }
            printf("strings: ");
            const wchar_t* s = (const wchar_t*)(p + 0x78);
            for (int k = 0; k < 6 && *s; ++k) { printf("[%ls] ", s); s += wcslen(s) + 1; }
            printf("\n");
        }
        destroyCfg(cfg);
    }
    destroyReader(xml);
    FreeLibrary(h);
    return 0;
}
#endif
