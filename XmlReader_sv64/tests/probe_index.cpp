// Temporary probe of element vtable slots 0x30/0x48/0x50 on the original DLL.
#include <windows.h>
#include <stdio.h>

template <typename R> static R V0(void* o, size_t s) {
    return reinterpret_cast<R (*)(void*)>((*(void***)o)[s / 8])(o);
}
template <typename R, typename A1> static R V1(void* o, size_t s, A1 a1) {
    return reinterpret_cast<R (*)(void*, A1)>((*(void***)o)[s / 8])(o, a1);
}
template <typename R, typename A2> static R V2(void* o, size_t s, void* a1, A2 a2) {
    return reinterpret_cast<R (*)(void*, void*, A2)>((*(void***)o)[s / 8])(o, a1, a2);
}

static void Show(void* p, const char* label) {
    printf("%s = %p", label, p);
    if (p && !IsBadReadPtr(p, 2)) {
        const wchar_t* s = (const wchar_t*)p;
        if (*s && !IsBadReadPtr(s, 4)) printf(" '%ls'", s);
    }
    printf("\n");
}

int wmain() {
    setvbuf(stdout, NULL, _IONBF, 0);
    HMODULE h = LoadLibraryW(L"C:\\serverside_re\\ranking\\analysis\\dll\\orig\\XmlReader_sv64.dll");
    printf("h=%p\n", (void*)h);
    void* (*create)() = (void* (*)())GetProcAddress(h, "CreateXmlReader");
    void (*dr)(void*) = (void (*)(void*))GetProcAddress(h, "DestroyXmlReader");
    void* xml = create();
    typedef void* (*InitFn)(void*, void*, void*, char);
    ((InitFn)((*(void***)xml)[0]))(xml, 0, 0, 0);
    void* doc = V1<void*>(xml, 0x30, (wchar_t*)L"C:\\serverside_re\\ranking\\XmlReader_sv64\\tests\\data\\game-list.config.xml");
    printf("doc=%p\n", doc);
    void* root = V0<void*>(doc, 0x10);
    void* el = V0<void*>(root, 8);
    printf("el=%p name=%ls line=%d attrCount=%d\n", el,
           V0<const wchar_t*>(el, 0x18), V0<int>(el, 0x20), V0<int>(el, 0x28));
    void* ni = V0<void*>(el, 0x58);
    printf("node iface=%p type=%d valid=%d\n", ni, V0<int>(ni, 0), V0<int>(ni, 8) & 1);
    for (int i = 0; i < 5; ++i) {
        printf("i=%d ", i);
        Show(V1<void*>(el, 0x30, (void*)(size_t)i), "slot30");
        printf("i=%d ", i);
        Show(V1<void*>(el, 0x50, (void*)(size_t)i), "slot50");
    }
    Show(V1<void*>(el, 0x48, (void*)L"id"), "slot48(id)");
    Show(V1<void*>(el, 0x48, (void*)L"bogus"), "slot48(bogus)");
    Show(V2<void*>(el, 0x30, (void*)0, (void*)L"id"), "slot30(0,id)");
    Show(V2<void*>(el, 0x30, (void*)1, (void*)L"id"), "slot30(1,id)");
    dr(xml);
    FreeLibrary(h);
    return 0;
}
