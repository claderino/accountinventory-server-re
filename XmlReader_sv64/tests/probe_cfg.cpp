// Temporary probe: config reader only (not part of the final DLL).
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string.h>

template <typename R> static R V0(void* o, size_t s) {
    return reinterpret_cast<R (*)(void*)>((*(void***)o)[s / 8])(o);
}
template <typename R, typename A1> static R V1(void* o, size_t s, A1 a1) {
    return reinterpret_cast<R (*)(void*, A1)>((*(void***)o)[s / 8])(o, a1);
}
template <typename R, typename A1, typename A2> static R V2(void* o, size_t s, A1 a1, A2 a2) {
    return reinterpret_cast<R (*)(void*, A1, A2)>((*(void***)o)[s / 8])(o, a1, a2);
}
template <typename R, typename A1, typename A2, typename A3> static R V3(void* o, size_t s, A1 a1, A2 a2, A3 a3) {
    return reinterpret_cast<R (*)(void*, A1, A2, A3)>((*(void***)o)[s / 8])(o, a1, a2, a3);
}
template <typename R, typename A1, typename A2, typename A3, typename A4> static R V4(void* o, size_t s, A1 a1, A2 a2, A3 a3, A4 a4) {
    return reinterpret_cast<R (*)(void*, A1, A2, A3, A4)>((*(void***)o)[s / 8])(o, a1, a2, a3, a4);
}
static void* WINAPI AllocCb(size_t n) { return malloc(n); }
struct Def28 { long long offset; const wchar_t* name; int a; int b; const wchar_t* def; const wchar_t* desc; };
static const long long OFF[30] = {-1,0,8,0x18,0x20,0x28,-1,0x10,-1,0x30,0x34,0x38,0x3C,0x40,-1,0x44,0x48,0x4C,-1,0x50,0x54,0x58,0x5C,-1,0x60,0x64,0x68,0x6C,0x70,0x74};
static const wchar_t* NAM[30] = {0,L"root-path",L"data-path",L"log-path",L"config-path",L"transaction-log-path",0,L"cert-file",0,L"worker-thread",L"max-world",L"dump-type",L"timer-interval",L"timer-max-time",0,L"world-listen-port",L"infogate-listen-port",L"manage-listen-port",0,L"service-world-id",L"max-ranking-list-count-per-contents",L"max-ranking-count",L"max-search-count-one-time",0,L"skip-list-ex-max-level",L"skip-list-ex-prob",L"skip-list-ex-max-node-count",L"pc-list-hash-count",L"ranking-list-update-period",L"accumulate-season-interim-score-period"};
static const int FLG[30] = {0,7,6,7,7,7,0,8,0,1,1,1,1,1,0,1,1,1,0,1,1,1,1,0,1,1,1,1,1,1};
static const int TYP[30] = {0,1,1,1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,1,0};

int wmain(int argc, wchar_t** argv) {
    setvbuf(stdout, NULL, _IONBF, 0);
    const wchar_t* orig = L"C:\\serverside_re\\ranking\\analysis\\dll\\orig\\XmlReader_sv64.dll";
    HMODULE h = LoadLibraryW(orig);
    printf("lib=%p\n", (void*)h);
    typedef void* (*CFn)(); typedef void (*DFn)(void*);
    CFn create = (CFn)GetProcAddress(h, "CreateXmlReader");
    DFn dr = (DFn)GetProcAddress(h, "DestroyXmlReader");
    typedef void* (*CCFn)(void*); typedef void (*DCFn)(void*);
    CCFn ccfg = (CCFn)GetProcAddress(h, "CreateXmlConfigReader");
    DCFn dcfg = (DCFn)GetProcAddress(h, "DestroyXmlConfigReader");
    void* xml = create();
    printf("xml=%p init=%d\n", xml, V3<int>(xml, 0, (void*)0, (void*)0, (char)0) & 1);
    int layout = (argc > 1 && wcscmp(argv[1], L"header") == 0) ? 1 : 0;
    Def28 defs[30];
    memset(defs, 0, sizeof(defs));
    for (int i = 0; i < 30; ++i) {
        defs[i].offset = OFF[i];
        defs[i].name = NAM[i];
        defs[i].a = layout ? TYP[i] : FLG[i];
        defs[i].b = layout ? FLG[i] : TYP[i];
    }
    void* cfg = ccfg(xml);
    printf("cfg=%p layout=%d\n", cfg, layout);
    printf("setdefs=%d\n", V4<int>(cfg, 8, defs, 30, &AllocCb, (size_t)0x78) & 1);
    const wchar_t* path = (argc > 2) ? argv[2] : L"C:\\serverside_re\\ranking\\XmlReader_sv64\\tests\\cfgdata\\rankingd.config.xml";
    printf("calling LoadConfig...\n");
    void* buf = V3<void*>(cfg, 0x28, path, L"rankingd", (char)0);
    const wchar_t* err = V0<const wchar_t*>(cfg, 0x30);
    printf("buf=%p err=%ls\n", buf, err ? err : L"<null>");
    if (buf) {
        unsigned char* p = (unsigned char*)buf;
        for (size_t i = 0; i < 0x78; i += 8) {
            printf("%04zx:", i);
            for (size_t j = 0; j < 8; ++j) printf(" %02X", p[i + j]);
            printf("\n");
        }
        const wchar_t* s = (const wchar_t*)(p + 0x78);
        for (int k = 0; k < 30 && *s; ++k) { printf("[%ls]", s); s += wcslen(s) + 1; }
        printf("\n");
    }
    dcfg(cfg);
    dr(xml);
    FreeLibrary(h);
    return 0;
}
