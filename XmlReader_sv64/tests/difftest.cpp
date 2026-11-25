// SOURCE_FILE("difftest.cpp");
// Differential test: original XmlReader_sv64.dll vs our reconstruction, driven
// through the same blood::XmlReaderApi (RankingDaemon/src/BloodLib/XmlReaderLib.cpp).
//
// Compared: GetInterfaceVersion; for every tests/data/*.xml the ReadFile result,
// document validity, full element tree in document order (name, line, child
// count, attributes by case-insensitive name lookup, node type and validity);
// the config reader on rankingd.config.xml with the 30 definitions of
// REPORT_XmlReader.md section 3.3 (the 0x78 fixed bytes field by field and every
// string), and GetError() on malformed.xml.
//
// NOTE: the original's element slot +0x30 is NOT an attribute-by-index getter
// (it crashes when called with an integer), so attributes are compared through
// slot +0x40 (GetAttribute(name)), which is the only form the daemon uses.
#include "XmlReader.h"

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <wchar.h>
#include <string>
#include <vector>

using blood::IXmlConfigReader;
using blood::IXmlDoc;
using blood::IXmlElement;
using blood::IXmlNode;
using blood::IXmlReader;
using blood::XmlConfigDef;
using blood::XmlReaderApi;

namespace {

const wchar_t* kOrigDll = L"C:\\serverside_re\\ranking\\analysis\\dll\\orig\\XmlReader_sv64.dll";
const wchar_t* kMyDll = L"C:\\serverside_re\\ranking\\XmlReader_sv64\\build\\XmlReader_sv64.dll";
const wchar_t* kDataDir = L"C:\\serverside_re\\ranking\\XmlReader_sv64\\tests\\data\\";

int g_checks = 0;
int g_failures = 0;

void Check(bool ok, const char* what, const wchar_t* detail = NULL) {
    ++g_checks;
    if (!ok) {
        ++g_failures;
        if (detail) printf("FAIL: %s [%ls]\n", what, detail);
        else printf("FAIL: %s\n", what);
    }
}

void CheckInt(int a, int b, const char* what) {
    ++g_checks;
    if (a != b) {
        ++g_failures;
        printf("FAIL: %s (orig=%d mine=%d)\n", what, a, b);
    }
}

void CheckStr(const wchar_t* a, const wchar_t* b, const char* what) {
    ++g_checks;
    if (!a || !b ? a != b : wcscmp(a, b) != 0) {
        ++g_failures;
        printf("FAIL: %s (orig=[%ls] mine=[%ls])\n", what,
               a ? a : L"<null>", b ? b : L"<null>");
    }
}

// ---------------------------------------------------------------------------
// Element-tree snapshot.
// ---------------------------------------------------------------------------
struct AttrSnap {
    std::wstring value;
};
struct ElemSnap {
    std::wstring name;
    int line;
    int childCount;
    int attrCount;
    std::vector<AttrSnap> attrs;   // one per candidate name, in a fixed order
    int nodeType;
    bool nodeValid;
    bool node2Valid;
};
struct TreeSnap {
    bool hasDoc;
    bool docValid;
    bool hasRoot;
    std::vector<ElemSnap> elements;
};

// The original's element slot +0x30 is not an index getter, and our DLL
// deliberately mirrors that, so attribute names come from the test data itself.
static void CollectNames(std::vector<std::wstring>& names) {
    static const wchar_t* base[] = {
        L"MixedCase", L"Name", L"a", L"apos", L"b", L"empty", L"encoding", L"flag",
        L"gt", L"id", L"internal-port", L"ip-address", L"lt", L"module", L"name",
        L"num", L"quot", L"release-module", L"release-side", L"reward-group-id",
        L"service-type", L"type", L"v", L"val", L"value", L"version", L"wid", NULL};
    for (int i = 0; base[i]; ++i) names.push_back(base[i]);
    wchar_t buf[64];
    for (int i = 1; i <= 20; ++i) {
        swprintf(buf, 64, L"reward-data-%d", i);       names.push_back(buf);
        swprintf(buf, 64, L"reward-group-top-%d", i);  names.push_back(buf);
        swprintf(buf, 64, L"reward-group-bottom-%d", i); names.push_back(buf);
    }
}

static void Walk(IXmlElement* e, const std::vector<std::wstring>& names, TreeSnap& snap) {
    ElemSnap s;
    const wchar_t* name = e->GetName();
    s.name = name ? name : L"<null>";
    s.line = e->GetLine();
    s.childCount = e->GetChildCount();
    s.attrCount = e->GetAttributeCount();
    for (size_t i = 0; i < names.size(); ++i) {
        AttrSnap a;
        const wchar_t* v = e->GetAttribute(names[i].c_str());
        a.value = v ? v : L"<null>";
        s.attrs.push_back(a);
    }
    IXmlNode* node = e->GetNode();
    IXmlNode* node2 = e->GetNode2();
    s.nodeType = node ? node->GetType() : -999;
    s.nodeValid = node ? node->IsValid() : false;
    s.node2Valid = node2 ? node2->IsValid() : false;
    snap.elements.push_back(s);
    IXmlElement* child = e->GetFirstChildElement();
    while (child) { Walk(child, names, snap); child = child->GetNextSiblingElement(); }
}

static TreeSnap Snapshot(const XmlReaderApi& api, const wchar_t* path,
                         const std::vector<std::wstring>& names) {
    TreeSnap snap;
    snap.hasDoc = false;
    snap.docValid = false;
    snap.hasRoot = false;
    IXmlReader* reader = api.CreateXmlReader();
    if (!reader) return snap;
    reader->Init(NULL, NULL, 0);
    IXmlDoc* doc = reader->ReadFile(path);
    if (doc) {
        snap.hasDoc = true;
        snap.docValid = doc->IsValid();
        IXmlElement* root = doc->GetRootElement();
        if (root) {
            snap.hasRoot = true;
            Walk(root, names, snap);
        }
        reader->DestroyDoc(doc);
    }
    reader->Close(false);
    api.DestroyXmlReader(reader);
    return snap;
}

static void CompareTree(const TreeSnap& a, const TreeSnap& b, const wchar_t* path) {
    wchar_t msg[1024];
    swprintf(msg, 1024, L"ReadFile doc for %ls", path);
    Check(a.hasDoc == b.hasDoc, "ReadFile doc", msg);
    if (!a.hasDoc || !b.hasDoc) return;
    Check(a.docValid == b.docValid, "doc IsValid", msg);
    Check(a.hasRoot == b.hasRoot, "doc GetRootElement", msg);
    CheckInt((int)a.elements.size(), (int)b.elements.size(), "element count");
    size_t n = a.elements.size() < b.elements.size() ? a.elements.size() : b.elements.size();
    for (size_t i = 0; i < n; ++i) {
        const ElemSnap& x = a.elements[i];
        const ElemSnap& y = b.elements[i];
        CheckStr(x.name.c_str(), y.name.c_str(), "element name");
        CheckInt(x.line, y.line, "element line");
        CheckInt(x.childCount, y.childCount, "element child count");
        CheckInt(x.attrCount, y.attrCount, "element attribute count");
        CheckInt(x.nodeType, y.nodeType, "node type");
        Check(x.nodeValid == y.nodeValid, "node validity");
        Check(x.node2Valid == y.node2Valid, "node2 validity");
        CheckInt((int)x.attrs.size(), (int)y.attrs.size(), "attribute snapshot count");
        size_t m = x.attrs.size() < y.attrs.size() ? x.attrs.size() : y.attrs.size();
        for (size_t j = 0; j < m; ++j)
            CheckStr(x.attrs[j].value.c_str(), y.attrs[j].value.c_str(), "attribute value");
    }
}

// ---------------------------------------------------------------------------
// Config definitions: original daemon table at host RVA 0x9F760.
// Frozen XmlConfigDef: type at +0x10 (conversion id), flags at +0x14
// (1 = required).
// ---------------------------------------------------------------------------
struct ConfigDefs {
    blood::XmlConfigDef defs[30];
    ConfigDefs() {
        static const long long off[30] = {-1,0,8,0x18,0x20,0x28,-1,0x10,-1,0x30,0x34,0x38,0x3C,0x40,
            -1,0x44,0x48,0x4C,-1,0x50,0x54,0x58,0x5C,-1,0x60,0x64,0x68,0x6C,0x70,0x74};
        static const wchar_t* name[30] = {NULL,L"root-path",L"data-path",L"log-path",L"config-path",
            L"transaction-log-path",NULL,L"cert-file",NULL,L"worker-thread",L"max-world",L"dump-type",
            L"timer-interval",L"timer-max-time",NULL,L"world-listen-port",L"infogate-listen-port",
            L"manage-listen-port",NULL,L"service-world-id",L"max-ranking-list-count-per-contents",
            L"max-ranking-count",L"max-search-count-one-time",NULL,L"skip-list-ex-max-level",
            L"skip-list-ex-prob",L"skip-list-ex-max-node-count",L"pc-list-hash-count",
            L"ranking-list-update-period",L"accumulate-season-interim-score-period"};
        static const int conv[30] = {0,7,6,7,7,7,0,8,0,1,1,1,1,1,0,1,1,1,0,1,1,1,1,0,1,1,1,1,1,1};
        static const int req[30] = {0,1,1,1,1,1,0,1,0,0,0,0,0,0,0,0,0,0,0,1,0,0,0,0,0,0,0,1,1,0};
        for (int i = 0; i < 30; ++i) {
            defs[i].offset = off[i];
            defs[i].name = name[i];
            // Verified against the original: the conversion id lives at +0x10
            // (XmlConfigDef::type) and the required marker at +0x14 (::flags).
            defs[i].type = conv[i];
            defs[i].flags = req[i];
            defs[i].defaultValue = NULL;
            defs[i].description = NULL;
        }
    }
};

static void* WINAPI TestAlloc(size_t n) { return malloc(n ? n : 1); }

struct ConfigResult {
    void* buffer;
    std::wstring error;
    ConfigResult() : buffer(NULL) {}
};

static ConfigResult LoadConfig(const XmlReaderApi& api, const wchar_t* path) {
    ConfigResult result;
    IXmlReader* reader = api.CreateXmlReader();
    if (!reader) { result.error = L"<no reader>"; return result; }
    reader->Init(NULL, NULL, 0);
    IXmlConfigReader* cfg = api.CreateXmlConfigReader(reader);
    if (!cfg) { result.error = L"<no config reader>"; api.DestroyXmlReader(reader); return result; }
    ConfigDefs defs;
    cfg->SetDefinitions(defs.defs, 30, &TestAlloc, 0x78);
    result.buffer = cfg->LoadConfig(path, L"rankingd", 0);
    const wchar_t* err = cfg->GetError();
    result.error = err ? err : L"";
    cfg->Destruct(false);
    api.DestroyXmlConfigReader(cfg);
    reader->Close(false);
    api.DestroyXmlReader(reader);
    return result;
}

static void CompareConfig(const ConfigResult& a, const ConfigResult& b, const wchar_t* path) {
    wchar_t msg[1024];
    swprintf(msg, 1024, L"LoadConfig %ls", path);
    Check(a.buffer != NULL && b.buffer != NULL, "LoadConfig result", msg);
    CheckStr(a.error.c_str(), b.error.c_str(), "LoadConfig error");
    if (!a.buffer || !b.buffer) return;
    const unsigned char* pa = (const unsigned char*)a.buffer;
    const unsigned char* pb = (const unsigned char*)b.buffer;
    ConfigDefs d;
    for (int i = 0; i < 30; ++i) {
        long long off = d.defs[i].offset;
        if (off < 0) continue;
        int conv = d.defs[i].type;
        if (conv == 1 || conv == 3) {
            int va = 0, vb = 0;
            memcpy(&va, pa + off, 4); memcpy(&vb, pb + off, 4);
            CheckInt(va, vb, "config int32 field");
        } else if (conv == 2 || conv == 5) {
            long long va = 0, vb = 0;
            memcpy(&va, pa + off, 8); memcpy(&vb, pb + off, 8);
            Check(va == vb, "config int64 field");
        } else if (conv == 4) {
            Check(pa[off] == pb[off], "config byte field");
        } else if (conv == 6 || conv == 7 || conv == 8) {
            const void *qa = NULL, *qb = NULL;
            memcpy(&qa, pa + off, 8); memcpy(&qb, pb + off, 8);
            Check(qa != NULL && qb != NULL, "config string pointer");
            if (qa && qb) {
                Check((const unsigned char*)qa >= pa + 0x78 &&
                      (const unsigned char*)qb >= pb + 0x78, "config string area");
                if (conv == 8) {
                    ++g_checks;
                    if (strcmp((const char*)qa, (const char*)qb) != 0) {
                        ++g_failures;
                        printf("FAIL: config UTF-8 string (orig=[%s] mine=[%s])\n",
                               (const char*)qa, (const char*)qb);
                    }
                } else {
                    CheckStr((const wchar_t*)qa, (const wchar_t*)qb, "config wide string");
                }
            }
        }
    }
}

static std::vector<std::wstring> ListDataFiles() {
    std::vector<std::wstring> files;
    std::wstring pattern = kDataDir;
    pattern += L"*.xml";
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return files;
    do {
        if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
            files.push_back(std::wstring(kDataDir) + fd.cFileName);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return files;
}

} // namespace

int wmain() {
    setvbuf(stdout, NULL, _IONBF, 0);
    XmlReaderApi orig, mine;
    if (!blood::XmlReader_Open(&orig, kOrigDll)) {
        printf("FAIL: cannot open original DLL %ls\n", kOrigDll);
        return 2;
    }
    if (!blood::XmlReader_Open(&mine, kMyDll)) {
        printf("FAIL: cannot open our DLL %ls\n", kMyDll);
        blood::XmlReader_Close(&orig);
        return 2;
    }
    printf("[version] original=%ls ours=%ls\n",
           orig.GetInterfaceVersion() ? orig.GetInterfaceVersion() : L"<null>",
           mine.GetInterfaceVersion() ? mine.GetInterfaceVersion() : L"<null>");
    CheckStr(orig.GetInterfaceVersion(), mine.GetInterfaceVersion(), "GetInterfaceVersion");
    Check(blood::XmlReader_CheckVersion(orig, NULL), "original version == 13");
    Check(blood::XmlReader_CheckVersion(mine, NULL), "our version == 13");

    // The original config reader is sensitive to global state left by many DOM
    // reads; run the config comparisons first.
    std::wstring configPath = L"C:\\serverside_re\\ranking\\XmlReader_sv64\\tests\\cfgdata\\";
    configPath += L"rankingd.config.xml";
    ConfigResult cb = LoadConfig(mine, configPath.c_str());
    ConfigResult ca = LoadConfig(orig, configPath.c_str());
    printf("[config] rankingd.config.xml: orig=%p ours=%p\n", ca.buffer, cb.buffer);
    CompareConfig(ca, cb, configPath.c_str());

    std::vector<std::wstring> files = ListDataFiles();
    printf("[xml] %zu files in tests\\data\n", files.size());
    Check(!files.empty(), "data files found");
    std::vector<std::wstring> names;
    CollectNames(names);
    for (size_t i = 0; i < files.size(); ++i) {
        TreeSnap a = Snapshot(orig, files[i].c_str(), names);
        TreeSnap b = Snapshot(mine, files[i].c_str(), names);
        printf("  %ls: orig=%zu elements, mine=%zu elements, %zu attribute names\n",
               files[i].c_str(), a.elements.size(), b.elements.size(), names.size());
        CompareTree(a, b, files[i].c_str());
    }

    std::wstring malformed = L"C:\\serverside_re\\ranking\\XmlReader_sv64\\tests\\cfgbad\\";
    malformed += L"malformed.xml";
    // The original rewrites this file as a canonical config; read it with ours FIRST.
    ConfigResult mb = LoadConfig(mine, malformed.c_str());
    ConfigResult ma = LoadConfig(orig, malformed.c_str());
    printf("[config] malformed.xml error: orig=[%ls] ours=[%ls]\n",
           ma.error.c_str(), mb.error.c_str());
    Check(ma.buffer == NULL && mb.buffer == NULL, "malformed config rejected");
    CheckStr(ma.error.c_str(), mb.error.c_str(), "malformed config GetError");

    free(ca.buffer); free(cb.buffer); free(ma.buffer); free(mb.buffer);
    blood::XmlReader_Close(&mine);
    blood::XmlReader_Close(&orig);
    printf("\n%s: checks=%d failures=%d\n", g_failures ? "FAIL" : "PASS", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
