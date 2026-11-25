// Temporary probe: locate a hanging call on the rebuilt DLL.
#include "XmlReader.h"
#include <stdio.h>

int wmain() {
    setvbuf(stdout, NULL, _IONBF, 0);
    blood::XmlReaderApi api;
    if (!blood::XmlReader_Open(&api, L"C:\\serverside_re\\ranking\\XmlReader_sv64\\build\\XmlReader_sv64.dll")) {
        printf("open failed %u\n", GetLastError()); return 1;
    }
    blood::IXmlReader* r = api.CreateXmlReader();
    printf("reader=%p\n", (void*)r);
    printf("init=%d\n", (int)r->Init(0, 0, 0));
    const wchar_t* path = L"C:\\serverside_re\\ranking\\XmlReader_sv64\\tests\\data\\bom.xml";
    blood::IXmlDoc* d = r->ReadFile(path);
    printf("doc=%p valid=%d\n", (void*)d, d ? (int)d->IsValid() : -1);
    blood::IXmlElement* root = d ? d->GetRootElement() : 0;
    printf("root=%p\n", (void*)root);
    if (root) {
        printf("name=%ls\n", root->GetName());
        printf("line=%d\n", root->GetLine());
        printf("kids=%d\n", root->GetChildCount());
        printf("attrs=%d\n", root->GetAttributeCount());
        printf("attr(a)=%ls\n", root->GetAttribute(L"a") ? root->GetAttribute(L"a") : L"<null>");
        blood::IXmlNode* n = root->GetNode();
        printf("node=%p\n", (void*)n);
        printf("type=%d\n", n->GetType());
        printf("valid=%d\n", (int)n->IsValid());
    }
    r->DestroyDoc(d);
    printf("doc destroyed\n");
    api.DestroyXmlReader(r);
    printf("reader destroyed\n");
    return 0;
}
