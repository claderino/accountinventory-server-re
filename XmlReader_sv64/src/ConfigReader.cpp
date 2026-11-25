// SOURCE_FILE("ConfigReader.cpp");
// IXmlConfigReader: schema installation, <config module='...'> validation,
// <option name='...' value='...'/> walk and packed-buffer construction.
#include "ConfigReaderInternal.h"

#include <cstdarg>
#include <cstdlib>
#include <cstring>
#include <new>

namespace xmlimpl {
namespace {

static void* HeapAllocValue(size_t bytes) {
    return HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, bytes);
}
static void HeapFreeValue(void* p) { if (p) HeapFree(GetProcessHeap(), 0, p); }

static wchar_t* WideDup(const wchar_t* s) {
    if (!s) return NULL;
    size_t bytes = (wcslen(s) + 1) * sizeof(wchar_t);
    wchar_t* copy = (wchar_t*)HeapAllocValue(bytes);
    if (copy) memcpy(copy, s, bytes);
    return copy;
}

// Frozen header XmlConfigDef: flags at +0x10 selects the conversion
// (1/3 = int32, 4 = byte, 2/5 = u64, 6 = wide string,
// 7 = path string with trailing separator, 8 = UTF-8 string); type at +0x14
// marks required options (1). Normal (0x28-byte) entries are read through the
// struct fields; the 0x38-byte grouped form needs raw offsets because it has no
// matching C++ type in the frozen header.
static inline int DefFlags(const blood::XmlConfigDef* d, size_t stride, int i) {
    // +0x10 holds the conversion id (1/3 = int32, 4 = byte, 2/5 = u64, 6/7 = wide string,
    // 8 = UTF-8 string). Read the frozen offset directly so header field naming cannot change it.
    int value = 0;
    memcpy(&value, (const unsigned char*)d + (size_t)i * stride + 0x10, 4);
    return value;
}
static inline int DefType(const blood::XmlConfigDef* d, size_t stride, int i) {
    // +0x14 holds the required marker (1 = required). Read the frozen offset directly.
    int value = 0;
    memcpy(&value, (const unsigned char*)d + (size_t)i * stride + 0x14, 4);
    return value;
}
static inline long long DefOffset(const blood::XmlConfigDef* d, size_t stride, int i) {
    if (stride == sizeof(blood::XmlConfigDef)) return d[i].offset;
    long long value = 0;
    memcpy(&value, (const unsigned char*)d + (size_t)i * stride, 8);
    return value;
}
static inline const wchar_t* DefName(const blood::XmlConfigDef* d, size_t stride, int i) {
    if (stride == sizeof(blood::XmlConfigDef)) return d[i].name;
    const wchar_t* value = NULL;
    memcpy(&value, (const unsigned char*)d + (size_t)i * stride + 0x08, 8);
    return value;
}
static inline const wchar_t* DefDefault(const blood::XmlConfigDef* d, size_t stride, int i) {
    if (stride == sizeof(blood::XmlConfigDef)) return d[i].defaultValue;
    const wchar_t* value = NULL;
    memcpy(&value, (const unsigned char*)d + (size_t)i * stride + 0x18, 8);
    return value;
}

} // namespace

void XmlConfigReaderImpl::Reset() {
    defs_ = NULL;
    altDefs_ = NULL;
    count_ = 0;
    values_ = NULL;
    seen_ = NULL;
    error_.clear();
    alloc_ = NULL;
    structSize_ = 0;
    grouped_ = false;
    versionCheck_ = false;
}

void XmlConfigReaderImpl::FreeArrays() {
    if (values_) {
        for (int i = 0; i < count_; ++i) HeapFreeValue(values_[i]);
        delete[] values_;
        values_ = NULL;
    }
    delete[] seen_;
    seen_ = NULL;
}

void XmlConfigReaderImpl::SetError(const wchar_t* fmt, ...) {
    wchar_t buf[2048];
    va_list ap;
    va_start(ap, fmt);
    _vsnwprintf_s(buf, 2048, _TRUNCATE, fmt, ap);
    va_end(ap);
    error_ = buf;
}

bool XmlConfigReaderImpl::InstallDefinitions(const void* defs, int count,
        void* (*alloc)(size_t), size_t structSize, bool grouped) {
    if (count_ > 0 || values_ || seen_ || !error_.empty()) {
        SetError(L"Already read completed");
        return false;
    }
    if (count < 1 || !defs || !alloc || structSize == 0) {
        SetError(L"Invalid parameter, def: %p, cnt: %d, create: %p, struct size: %d",
                 defs, count, alloc, (int)structSize);
        return false;
    }
    count_ = count;
    defs_ = static_cast<const blood::XmlConfigDef*>(defs);
    altDefs_ = grouped ? defs : NULL;
    alloc_ = alloc;
    structSize_ = structSize;
    grouped_ = grouped;
    values_ = new (std::nothrow) void*[count]();
    seen_ = new (std::nothrow) unsigned char[count]();
    if (!values_ || !seen_) {
        SetError(L"Invalid parameter, def: %p, cnt: %d, create: %p, struct size: %d",
                 defs, count, alloc, (int)structSize);
        return false;
    }
    size_t stride = grouped ? 0x38 : 0x28;
    for (int i = 0; i < count_; ++i) {
        int conversion = DefFlags(defs_, stride, i);
        if (conversion == 0) {
            seen_[i] = 1; // group/comment header
        } else if (conversion != 2) {
            ApplyDefault(i);   // not a derived option
        }
    }
    return true;
}

bool XmlConfigReaderImpl::SetDefinitions(const blood::XmlConfigDef* defs, int count,
        void* (*alloc)(size_t), size_t structSize) {
    return InstallDefinitions(defs, count, alloc, structSize, false);
}
bool XmlConfigReaderImpl::SetGroupedDefinitions(const void* defs, int count,
        void* (*alloc)(size_t), size_t structSize) {
    return InstallDefinitions(defs, count, alloc, structSize, true);
}
void* XmlConfigReaderImpl::GetReserved10() { return NULL; }

void XmlConfigReaderImpl::ApplyDefault(int index) {
    size_t stride = grouped_ ? 0x38 : 0x28;
    const wchar_t* def = DefDefault(defs_, stride, index);
    if (!def || !*def) return;
    ConvertValue(index, def);
}

int XmlConfigReaderImpl::FindDefinition(const wchar_t* name) const {
    if (!name) return -1;
    size_t stride = grouped_ ? 0x38 : 0x28;
    for (int i = 0; i < count_; ++i) {
        const wchar_t* n = DefName(defs_, stride, i);
        if (n && _wcsicmp(n, name) == 0) return i;
    }
    return -1;
}

void XmlConfigReaderImpl::ConvertValue(int index, const wchar_t* value) {
    if (!value || !values_) return;
    size_t stride = grouped_ ? 0x38 : 0x28;
    int conversion = DefFlags(defs_, stride, index);
    HeapFreeValue(values_[index]);
    values_[index] = NULL;
    switch (conversion) {
    case 1: case 3: {
        int* p = (int*)HeapAllocValue(4);
        if (p) *p = _wtoi(value);
        values_[index] = p;
        break;
    }
    case 4: {
        unsigned char* p = (unsigned char*)HeapAllocValue(1);
        if (p) *p = (unsigned char)_wtoi(value);
        values_[index] = p;
        break;
    }
    case 2: case 5: {
        long long* p = (long long*)HeapAllocValue(8);
        if (p) *p = _wtoi64(value);
        values_[index] = p;
        break;
    }
    case 6:
        values_[index] = WideDup(value);
        break;
    case 7: {
        // The original's path conversion ensures a trailing separator.
        std::wstring path(value);
        if (!path.empty() && path[path.size() - 1] != L'\\' && path[path.size() - 1] != L'/')
            path.push_back(L'\\');
        values_[index] = WideDup(path.c_str());
        break;
    }
    case 8: {
        int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, NULL, 0, NULL, NULL);
        if (bytes > 0) {
            char* p = (char*)HeapAllocValue((size_t)bytes);
            if (p && WideCharToMultiByte(CP_UTF8, 0, value, -1, p, bytes, NULL, NULL) > 0)
                values_[index] = p;
            else
                HeapFreeValue(p);
        }
        break;
    }
    default: {
        // Type 2 (derived) is not used by this daemon; treat it as a wide
        // string so that derived values still have a plausible representation.
        values_[index] = WideDup(value);
        break;
    }
    }
}

void* XmlConfigReaderImpl::ValidateAndPack(const void* mem, size_t len, const wchar_t* fileName,
        const wchar_t* module, unsigned major, size_t* outSize) {
    if (!reader_ || !fileName || !module || !alloc_) {
        SetError(L"Invalid parameter, filename: %s, configname: %s", fileName, module);
        return NULL;
    }
    IXmlDoc* doc = reader_->ReadMemory(mem, len, fileName);
    if (!doc) { SetError(L"Cannot read '%s'", fileName); return NULL; }
    IXmlElement* root = doc->GetRootElement();
    if (!doc->IsValid() || !root || _wcsicmp(root->GetName(), L"config") != 0) {
        reader_->DestroyDoc(doc);
        SetError(L"Invalid XML file");
        return NULL;
    }
    const wchar_t* mod = root->GetAttribute(L"module");
    if (!mod || _wcsicmp(mod, module) != 0) {
        SetError(L"invalid module name(%s), expected(%s)", mod ? mod : L"<NULL>", module);
        reader_->DestroyDoc(doc);
        return NULL;
    }
    if (grouped_ && versionCheck_) {
        const wchar_t* v = root->GetAttribute(L"major-version");
        if (!v) {
            SetError(L"major version is not specified");
            reader_->DestroyDoc(doc);
            return NULL;
        }
        int got = _wtoi(v);
        if ((unsigned)got != major) {
            SetError(L"incompatible major version(%d), expected(%d)", got, (int)major);
            reader_->DestroyDoc(doc);
            return NULL;
        }
    }

    IXmlElement* option = root->GetFirstChildElement();
    while (option) {
        const wchar_t* name = option->GetAttribute(L"name");
        const wchar_t* value = option->GetAttribute(L"value");
        int idx = FindDefinition(name);
        if (idx < 0) {
            // The original logs "config:%s, value:%s is maybe obsolete" here.
            // It is a diagnostic only and is not part of GetError().
        } else {
            seen_[idx] = 1;
            if (value) ConvertValue(idx, value);
        }
        option = option->GetNextSiblingElement();
    }

    std::wstring missing;
    size_t stride = grouped_ ? 0x38 : 0x28;
    for (int i = 0; i < count_; ++i) {
        int required = 0;
        memcpy(&required, (const unsigned char*)defs_ + (size_t)i * stride + 0x14, 4);
        if (required == 1 && !seen_[i]) {
            const wchar_t* n = DefName(defs_, stride, i);
            if (!n || !*n) continue;
            if (!missing.empty()) missing += L", ";
            missing += n;
        }
    }
    reader_->DestroyDoc(doc);
    if (!missing.empty()) {
        SetError(L"option '%s' is REQUIRED", missing.c_str());
        return NULL;
    }

    size_t stringBytes = 0;
    for (int i = 0; i < count_; ++i) {
        int conversion = DefFlags(defs_, stride, i);
        void* v = values_[i];
        if ((conversion == 6 || conversion == 7) && v)
            stringBytes += (wcslen((wchar_t*)v) + 1) * sizeof(wchar_t);
        else if (conversion == 8 && v) stringBytes += strlen((char*)v) + 1;
    }
    size_t total = structSize_ + stringBytes;
    void* dst = alloc_(total);
    if (!dst) {
        SetError(L"couldn't allocate memory for parser");
        return NULL;
    }
    memset(dst, 0, total);
    unsigned char* out = (unsigned char*)dst;
    unsigned char* cursor = out + structSize_;
    for (int i = 0; i < count_; ++i) {
        long long off = DefOffset(defs_, stride, i);
        if (off < 0) continue;
        int conversion = DefFlags(defs_, stride, i);
        void* v = values_[i];
        switch (conversion) {
        case 1: case 3: if (v) memcpy(out + off, v, 4); break;
        case 2: case 5: if (v) memcpy(out + off, v, 8); break;
        case 4: if (v) out[off] = *(unsigned char*)v; break;
        case 6: case 7:
            if (v) {
                size_t bytes = (wcslen((wchar_t*)v) + 1) * sizeof(wchar_t);
                *(wchar_t**)(out + off) = (wchar_t*)cursor;
                memcpy(cursor, v, bytes);
                cursor += bytes;
            }
            break;
        case 8:
            if (v) {
                size_t bytes = strlen((char*)v) + 1;
                *(char**)(out + off) = (char*)cursor;
                memcpy(cursor, v, bytes);
                cursor += bytes;
            }
            break;
        default: break;
        }
    }
    if (outSize) *outSize = total;
    return dst;
}

void* XmlConfigReaderImpl::LoadFromMemory(size_t* outSize, const void* mem, size_t len,
        const wchar_t* fileName, const wchar_t* module, unsigned major, unsigned /*minor*/,
        bool /*createDefault*/, bool flags) {
    versionCheck_ = flags != 0;
    return ValidateAndPack(mem, len, fileName, module, major, outSize);
}

void* XmlConfigReaderImpl::Load(size_t* outSize, const wchar_t* fileName, const wchar_t* module,
        char /*flag*/, unsigned major, unsigned minor, char createDefault, char flags) {
    if (!fileName || !*fileName) { SetError(L"Invalid parameter, filename: %s, configname: %s", fileName, module); return NULL; }
    HANDLE h = CreateFileW(fileName, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
                          FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) { SetError(L"Cannot read '%s'", fileName); return NULL; }
    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size) || size.QuadPart < 0 || size.QuadPart > (64LL * 1024 * 1024)) {
        CloseHandle(h); SetError(L"Cannot read '%s'", fileName); return NULL;
    }
    std::vector<unsigned char> buf((size_t)size.QuadPart + 1);
    DWORD got = 0;
    BOOL ok = size.QuadPart == 0 || ReadFile(h, &buf[0], (DWORD)size.QuadPart, &got, NULL);
    CloseHandle(h);
    if (!ok) { SetError(L"Cannot read '%s'", fileName); return NULL; }
    return LoadFromMemory(outSize, &buf[0], got, fileName, module, major, minor,
                          createDefault != 0, flags != 0);
}

void* XmlConfigReaderImpl::LoadConfig(const wchar_t* fileName, const wchar_t* module, char flags) {
    size_t outSize = 0;
    return Load(&outSize, fileName, module, 0, (unsigned)-1, (unsigned)-1, 0, flags);
}

const wchar_t* XmlConfigReaderImpl::GetError() {
    return error_.empty() ? NULL : error_.c_str();
}

void XmlConfigReaderImpl::Destruct(bool freeObject) {
    FreeArrays();
    error_.clear();
    if (freeObject) delete this;
}

} // namespace xmlimpl

extern "C" __declspec(dllexport) blood::IXmlConfigReader*
CreateXmlConfigReader(blood::IXmlReader* reader) {
    if (!reader) return NULL;
    try { return new xmlimpl::XmlConfigReaderImpl(reader); }
    catch (...) { return NULL; }
}

extern "C" __declspec(dllexport) void
DestroyXmlConfigReader(blood::IXmlConfigReader* reader) {
    if (!reader) return;
    reader->Destruct(false);
    delete static_cast<xmlimpl::XmlConfigReaderImpl*>(reader);
}
