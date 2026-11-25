// SOURCE_FILE("XmlReader.cpp");
// The seven exports and IXmlReader implementation of XmlReader_sv64.dll.
#include "XmlReaderInternal.h"

#include <cstdarg>
#include <cstring>
#include <vector>

namespace xmlimpl {

// Error sinks are the only global mutable state permitted by the interface
// (one per backend). Every reader object itself is used by a single thread.
static void* g_errorSinks[2] = {NULL, NULL};

static thread_local std::wstring tlsLastError;

const std::wstring& LastXmlError() { return tlsLastError; }

void ReportXmlError(int backend, const wchar_t* fmt, ...) {
    wchar_t buf[1024];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 1024, _TRUNCATE, fmt, ap);
    va_end(ap);
    tlsLastError = buf;
    // UNVERIFIED: the original sink vtable is not used by any in-tree caller.
    // We record the formatted message (including the parse line when supplied)
    // but do not guess a call signature for an installed sink.
    (void)backend;
}

class XmlReaderImpl : public IXmlReader {
public:
    XmlReaderImpl() : io_(NULL), ownedIO_(NULL), backend_(0), valid_(false) {
        ownedIO_ = new XmlReaderFileIO();
        io_ = ownedIO_;
    }
    ~XmlReaderImpl() { delete ownedIO_; }

    bool Init(IXmlFileIO* io, void* /*reserved*/, char backend) override {
        backend_ = backend ? 1 : 0;
        io_ = io ? io : ownedIO_;
        valid_ = true;
        return true;
    }
    void* SetErrorSink(void* sink) override {
        void* old = g_errorSinks[backend_];
        g_errorSinks[backend_] = sink;
        return old;
    }
    void* GetErrorSink() override { return g_errorSinks[backend_]; }
    void Close(bool /*destroyHeap*/) override {
        valid_ = false;
        io_ = NULL;
    }
    IXmlFileIO* GetFileIO() override { return io_; }

    bool ParseWithBuilder(const wchar_t* path, IXmlSaxBuilder* sink) override {
        IXmlDoc* doc = ReadFile(path);
        if (!doc || !sink) { DestroyDoc(doc); return false; }
        sink->Begin();
        EmitElements(doc->GetRootElement(), sink);
        return true;
    }

    IXmlDoc* ReadFile(const wchar_t* path) override {
        if (!path || !*path) { ReportXmlError(backend_, L"invalid filename."); return NULL; }
        HANDLE h = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        if (h == INVALID_HANDLE_VALUE) {
            ReportXmlError(backend_, L"failed to read xml., file: %s", path);
            return NULL;
        }
        LARGE_INTEGER size;
        if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > (64LL * 1024 * 1024)) {
            CloseHandle(h);
            ReportXmlError(backend_, L"failed to read xml., file: %s", path);
            return NULL;
        }
        std::vector<unsigned char> buffer((size_t)size.QuadPart + 1);
        DWORD got = 0;
        BOOL ok = size.QuadPart == 0 || ::ReadFile(h, &buffer[0], (DWORD)size.QuadPart, &got, NULL);
        CloseHandle(h);
        if (!ok) {
            ReportXmlError(backend_, L"failed to read xml., file: %s", path);
            return NULL;
        }
        return ReadMemory(&buffer[0], got, path);
    }

    IXmlDoc* ReadMemory(const void* buffer, size_t length, const wchar_t* name) override {
        if (!buffer || length == 0) {
            ReportXmlError(backend_, L"buffer is null or size zero, file:%s", name ? name : L"");
            return NULL;
        }
        if (CheckHeaderSized(const_cast<void*>(buffer), length)) {
            // The original decodes its private 0x50-byte binary DOM here. The
            // daemon only ever produces ordinary XML, so this path is unused.
            ReportXmlError(backend_, L"unsupported binary XML DOM header");
            return NULL;
        }
        XmlDocImpl* doc = new XmlDocImpl();
        doc->SetFileName(name);
        int line = 1;
        std::wstring error;
        if (!ParseDocument(buffer, length, doc, &line, &error)) {
            ReportXmlError(backend_, L"error occurs in libxml2 (line %d) : %s", line, error.c_str());
            delete doc;
            return NULL;
        }
        return doc;
    }

    void DestroyDoc(IXmlDoc* doc) override {
        if (!doc) return;
        doc->Destroy();
        delete static_cast<XmlDocImpl*>(doc);
    }

    IXmlDoc* NewDoc() override { return new XmlDocImpl(); }

    bool CheckHeader(void* header) override {
        if (!header) return false;
        unsigned char* p = (unsigned char*)header;
        static const unsigned char magic[8] = {0x4C, 0x4D, 0x58, 0x42, 0x4F, 0x53, 0x4C, 0x42};
        for (int i = 0; i < 8; ++i) if (p[i] != magic[i]) return false;
        return p[8] == 3;
    }
    bool CheckHeaderSized(void* header, size_t size) override {
        if (!header || size <= 0x50 || !CheckHeader(header)) return false;
        unsigned int declared = 0;
        memcpy(&declared, (unsigned char*)header + 0x0C, 4);
        return declared == (unsigned int)size;
    }

private:
    static void EmitElements(IXmlElement* element, IXmlSaxBuilder* sink) {
        while (element) {
            sink->Element(element);
            EmitElements(element->GetFirstChildElement(), sink);
            element = element->GetNextSiblingElement();
        }
    }

    IXmlFileIO* io_;
    XmlReaderFileIO* ownedIO_;
    char backend_;
    bool valid_;
};

} // namespace xmlimpl

// ===========================================================================
// Exports (undecorated x64 names)
// ===========================================================================
extern "C" __declspec(dllexport) const wchar_t* GetInterfaceVersion() {
    return L"13";
}

extern "C" __declspec(dllexport) blood::IXmlReader* CreateXmlReader() {
    try { return new xmlimpl::XmlReaderImpl(); }
    catch (...) { return NULL; }
}

extern "C" __declspec(dllexport) void DestroyXmlReader(blood::IXmlReader* reader) {
    delete static_cast<xmlimpl::XmlReaderImpl*>(reader); // NULL-safe
}

extern "C" __declspec(dllexport) blood::IXmlConfigReader* CreateXmlConfigReader(blood::IXmlReader* reader);
extern "C" __declspec(dllexport) void DestroyXmlConfigReader(blood::IXmlConfigReader* reader);

// ---------------------------------------------------------------------------
// Generic packet serializers (unused by the daemon). Field descriptors are the
// 0x38-byte records of report section 3.5: +0 source offset, +8 string-area
// offset, +0x10 int32 type id. Implemented per report section 4.4.
// ---------------------------------------------------------------------------
struct ConfigFieldDesc {
    long long offset;       // +0x00
    long long stringBase;   // +0x08 (UNVERIFIED in the report)
    int type;               // +0x10
    int reserved;
    long long extra[3];
};

static int FieldBytes(int type) {
    switch (type) {
    case 1: case 3: return 4;
    case 2: case 5: return 8;
    case 4: return 1;
    default: return 0;
    }
}

extern "C" __declspec(dllexport) void* EncodeToConfigPacket(size_t* outSize, const void* base,
        int fieldCount, const void* fields, size_t fixedSize, const int* fieldOffsets,
        void* (*alloc)(size_t)) {
    if (!outSize || !base || !alloc || !fields || !fieldOffsets || fieldCount < 0) return NULL;
    const ConfigFieldDesc* desc = static_cast<const ConfigFieldDesc*>(fields);
    size_t stringBytes = 0;
    for (int i = 0; i < fieldCount; ++i) {
        if (desc[i].type == 6 || desc[i].type == 7 || desc[i].type == 8)
            stringBytes += (size_t)desc[i].extra[0]; // caller-provided string length
    }
    size_t total = fixedSize + stringBytes;
    void* dst = alloc(total);
    if (!dst) return NULL;
    memset(dst, 0, total);
    unsigned char* out = (unsigned char*)dst;
    const unsigned char* src = (const unsigned char*)base;
    size_t srcBase = stringBytes ? (size_t)desc[0].stringBase : 0;
    if (stringBytes) memcpy(out + fixedSize, src + srcBase, stringBytes);
    for (int i = 0; i < fieldCount; ++i) {
        size_t dest = (size_t)fieldOffsets[i];
        size_t from = (size_t)desc[i].offset;
        switch (desc[i].type) {
        case 1: case 3: memcpy(out + dest, src + from, 4); break;
        case 2: case 5: memcpy(out + dest, src + from, 8); break;
        case 4: out[dest] = src[from]; break;
        case 6: case 7: case 8: *(int*)(out + dest) = (int)from; break;
        default: break;
        }
    }
    *outSize = total;
    return dst;
}

extern "C" __declspec(dllexport) void* DecodeFromConfigPacket(size_t* outSize, const void* packet,
        size_t stringEnd, size_t stringBase, const int* fieldOffsets, size_t structSize,
        int fieldCount, const void* fields, void* (*alloc)(size_t)) {
    if (!outSize || !packet || !alloc || !fields || !fieldOffsets || fieldCount < 0 ||
        stringEnd < stringBase) return NULL;
    const ConfigFieldDesc* desc = static_cast<const ConfigFieldDesc*>(fields);
    size_t stringBytes = stringEnd - stringBase;
    size_t total = structSize + stringBytes;
    void* dst = alloc(total);
    if (!dst) return NULL;
    memset(dst, 0, total);
    unsigned char* out = (unsigned char*)dst;
    const unsigned char* src = (const unsigned char*)packet;
    if (stringBytes) memcpy(out + structSize, src + stringBase, stringBytes);
    for (int i = 0; i < fieldCount; ++i) {
        size_t from = (size_t)fieldOffsets[i];
        size_t dest = (size_t)desc[i].offset;
        int type = desc[i].type;
        int bytes = FieldBytes(type);
        if (bytes) {
            memcpy(out + dest, src + from, bytes);
        } else if (type == 6 || type == 7 || type == 8) {
            // Relative string offset in the packet -> pointer into the new buffer.
            int relative = 0;
            memcpy(&relative, src + from, 4);
            *(unsigned char**)(out + dest) = out + (size_t)(unsigned int)relative;
        }
    }
    *outSize = total;
    return dst;
}
