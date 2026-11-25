// SOURCE_FILE("XmlElement.cpp");
// XmlDocImpl, XmlElementImpl (element API + node interface), XmlReaderFileIO.
#include "XmlReaderInternal.h"

#include <cwchar>

namespace xmlimpl {
namespace {

// ---------------------------------------------------------------------------
// Explicit vtables. IXmlElement has a hole at +0x68 in the original DLL; the
// compiler-generated vtable of the C++ class cannot express that hole.
// ---------------------------------------------------------------------------
void* g_elementVTable[15];
void* g_nodeVTable[2];

// Element wrappers: 'this' arrives as the IXmlElement* subobject (offset 0).
// The calls must be qualified: an unqualified call to a virtual member would
// dispatch through the very vtable entry we are implementing (infinite recursion).
#define IMPL(e) static_cast<XmlElementImpl*>(e)->XmlElementImpl::
static int ChildCount_Thunk(IXmlElement* e) { return IMPL(e) GetChildCount(); }
static IXmlElement* FirstChild_Thunk(IXmlElement* e) { return IMPL(e) GetFirstChildElement(); }
static IXmlElement* NextSibling_Thunk(IXmlElement* e) { return IMPL(e) GetNextSiblingElement(); }
static const wchar_t* Name_Thunk(IXmlElement* e) { return IMPL(e) GetName(); }
static int Line_Thunk(IXmlElement* e) { return IMPL(e) GetLine(); }
static int AttrCount_Thunk(IXmlElement* e) { return IMPL(e) GetAttributeCount(); }
static const wchar_t* AttrName_Thunk(IXmlElement* e, int i) { return IMPL(e) GetAttributeName(i); }
static IXmlElement* ChildAt_Thunk(IXmlElement* e, int i) { return IMPL(e) GetChildAt(i); }
static const wchar_t* Attr_Thunk(IXmlElement* e, const wchar_t* n) { return IMPL(e) GetAttribute(n); }
static int AttrIndex_Thunk(IXmlElement* e, const wchar_t* n) { return IMPL(e) GetAttributeIndex(n); }
static const wchar_t* AttrValue_Thunk(IXmlElement* e, int i) { return IMPL(e) GetAttributeValue(i); }
static IXmlNode* GetNode_Thunk(IXmlElement* e) { return IMPL(e) GetNode(); }
static IXmlNode* GetNode2_Thunk(IXmlElement* e) { return IMPL(e) GetNode2(); }
static int Reserved_Thunk(IXmlElement* e) { return IMPL(e) GetReserved70(); }
static int NodeType_Thunk(IXmlNode* n) { return IMPL(n) GetType(); }
static bool NodeValid_Thunk(IXmlNode* n) { return IMPL(n) IsValid(); }
#undef IMPL

struct VTableInit {
    VTableInit() {
    g_elementVTable[0] = reinterpret_cast<void*>(&ChildCount_Thunk);
    g_elementVTable[1] = reinterpret_cast<void*>(&FirstChild_Thunk);
    g_elementVTable[2] = reinterpret_cast<void*>(&NextSibling_Thunk);
    g_elementVTable[3] = reinterpret_cast<void*>(&Name_Thunk);
    g_elementVTable[4] = reinterpret_cast<void*>(&Line_Thunk);
    g_elementVTable[5] = reinterpret_cast<void*>(&AttrCount_Thunk);
    g_elementVTable[6] = reinterpret_cast<void*>(&AttrName_Thunk);
    g_elementVTable[7] = reinterpret_cast<void*>(&ChildAt_Thunk);
    g_elementVTable[8] = reinterpret_cast<void*>(&Attr_Thunk);
    g_elementVTable[9] = reinterpret_cast<void*>(&AttrIndex_Thunk);
    g_elementVTable[10] = reinterpret_cast<void*>(&AttrValue_Thunk);
    g_elementVTable[11] = reinterpret_cast<void*>(&GetNode_Thunk);
    g_elementVTable[12] = reinterpret_cast<void*>(&GetNode2_Thunk);
    g_elementVTable[13] = reinterpret_cast<void*>(&Reserved_Thunk); // +0x68: dummy in the original
    g_elementVTable[14] = reinterpret_cast<void*>(&Reserved_Thunk); // +0x70
    g_nodeVTable[0] = reinterpret_cast<void*>(&NodeType_Thunk);
    g_nodeVTable[1] = reinterpret_cast<void*>(&NodeValid_Thunk);
    }
};

void EnsureVTables() {
    static VTableInit once;  // thread-safe one-time initialisation (C++ static local)
    (void)once;
}

} // namespace

// ===========================================================================
// XmlElementImpl
// ===========================================================================
XmlElementImpl::XmlElementImpl(XmlDocImpl* owner, int type, int line)
    : owner_(owner), type_(type), line_(line), valid_(true), parent_(NULL), indexInParent_(0) {
    InstallVTables();
}

XmlElementImpl::~XmlElementImpl() {}

void XmlElementImpl::InstallVTables() {
    EnsureVTables();
    // The primary base's vptr is at +0, the IXmlNode vptr at +8.
    *reinterpret_cast<void**>(static_cast<IXmlElement*>(this)) = g_elementVTable;
    *reinterpret_cast<void**>(static_cast<IXmlNode*>(this)) = g_nodeVTable;
}

void XmlElementImpl::AddAttribute(const std::wstring& name, const std::wstring& value) {
    XmlAttr a;
    a.name = name;
    a.value = value;
    attrs_.push_back(a);
}
void XmlElementImpl::AddChild(XmlElementImpl* child) { children_.push_back(child); }

int XmlElementImpl::GetChildCount() { return static_cast<int>(children_.size()); }
IXmlElement* XmlElementImpl::GetFirstChildElement() {
    return children_.empty() ? NULL : static_cast<IXmlElement*>(children_[0]);
}
IXmlElement* XmlElementImpl::GetNextSiblingElement() {
    if (!parent_) return NULL;
    size_t next = indexInParent_ + 1;
    return next < parent_->children_.size() ? static_cast<IXmlElement*>(parent_->children_[next]) : NULL;
}
const wchar_t* XmlElementImpl::GetName() { return name_.c_str(); }
int XmlElementImpl::GetLine() { return line_; }
int XmlElementImpl::GetAttributeCount() { return static_cast<int>(attrs_.size()); }
const wchar_t* XmlElementImpl::GetAttributeName(int index) {
    // The original's slot +0x30 is a (type,name) lookup, not an index getter:
    // called with a single integer it returns NULL. Keep that ABI behaviour.
    (void)index;
    return NULL;
}
IXmlElement* XmlElementImpl::GetChildAt(int index) {
    return index >= 0 && (size_t)index < children_.size() ? static_cast<IXmlElement*>(children_[index]) : NULL;
}
const wchar_t* XmlElementImpl::GetAttribute(const wchar_t* name) {
    if (!name) return NULL;
    for (size_t i = 0; i < attrs_.size(); ++i) {
        if (_wcsicmp(attrs_[i].name.c_str(), name) == 0) return attrs_[i].value.c_str();
    }
    return NULL;
}
int XmlElementImpl::GetAttributeIndex(const wchar_t* name) {
    // The original's slot +0x48 is not a real index lookup: every observed call
    // with a known name returns 0. Keep that observable behaviour.
    return name ? 0 : -1;
}
const wchar_t* XmlElementImpl::GetAttributeValue(int index) {
    // The original's slot +0x50 is not a by-index value getter (it returns an
    // unusable pointer for index calls); stay NULL-safe instead.
    (void)index;
    return NULL;
}
IXmlNode* XmlElementImpl::GetNode() { return static_cast<IXmlNode*>(this); }
IXmlNode* XmlElementImpl::GetNode2() { return static_cast<IXmlNode*>(this); }
int XmlElementImpl::GetReserved70() { return 0; }
int XmlElementImpl::GetType() { return type_; }
bool XmlElementImpl::IsValid() { return valid_; }

// ===========================================================================
// XmlDocImpl
// ===========================================================================
XmlDocImpl::XmlDocImpl() : valid_(false), root_(NULL) {}
XmlDocImpl::~XmlDocImpl() { ReleaseNodes(); }

void XmlDocImpl::ReleaseNodes() {
    for (size_t i = 0; i < nodes_.size(); ++i) delete nodes_[i];
    nodes_.clear();
    root_ = NULL;
    valid_ = false;
}

XmlElementImpl* XmlDocImpl::NewNode(int type, int line) {
    XmlElementImpl* node = new XmlElementImpl(this, type, line);
    nodes_.push_back(node);
    return node;
}
bool XmlDocImpl::IsValid() { return valid_; }
void* XmlDocImpl::GetInternal() { return root_; }
IXmlElement* XmlDocImpl::GetRootElement() { return static_cast<IXmlElement*>(root_); }
size_t XmlDocImpl::GetSerializedSize() { return 0; }
bool XmlDocImpl::Serialize(void*, size_t) { return false; }
bool XmlDocImpl::Deserialize(const void*, size_t) { return false; }
void XmlDocImpl::Destroy() {}
void XmlDocImpl::Delete() { delete this; }

// ===========================================================================
// XmlReaderFileIO - wildcard enumeration, file reading (report section 2.5.1).
// ===========================================================================
XmlReaderFileIO::XmlReaderFileIO() : handle_(INVALID_HANDLE_VALUE), index_(-1) {}
XmlReaderFileIO::~XmlReaderFileIO() { Close(); }

int XmlReaderFileIO::Init(const wchar_t* directory, const wchar_t* wildcard, int /*flags*/) {
    files_.clear();
    currentName_.clear();
    index_ = -1;
    if (!directory || !wildcard || !*directory || !*wildcard) return 5;
    std::wstring dir(directory);
    std::wstring pattern;
    if (!dir.empty() && (dir[dir.size() - 1] == L'\\' || dir[dir.size() - 1] == L'/')) pattern = dir + wildcard;
    else pattern = dir + L"\\" + wildcard;
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW(pattern.c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return GetLastError() == ERROR_FILE_NOT_FOUND ? 2 : 3;
    do {
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
        std::wstring full(dir);
        if (full.empty() || (full[full.size() - 1] != L'\\' && full[full.size() - 1] != L'/')) full += L'\\';
        full += fd.cFileName;
        files_.push_back(full);
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return files_.empty() ? 2 : 0;
}

int XmlReaderFileIO::OpenNext() {
    Close();
    ++index_;
    if (index_ < 0 || (size_t)index_ >= files_.size()) return 1;
    currentName_ = files_[index_];
    handle_ = CreateFileW(currentName_.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                          OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    return handle_ == INVALID_HANDLE_VALUE ? 3 : 0;
}

int XmlReaderFileIO::Read(void* buffer, int* capacity) {
    if (!buffer || !capacity || *capacity < 0 || handle_ == INVALID_HANDLE_VALUE) return 5;
    DWORD want = static_cast<DWORD>(*capacity);
    DWORD got = 0;
    if (!ReadFile(handle_, buffer, want, &got, NULL)) return 3;
    *capacity = static_cast<int>(got);
    return 0;
}

int XmlReaderFileIO::GetFileSize() {
    HANDLE h = handle_;
    bool temporary = false;
    if (h == INVALID_HANDLE_VALUE && !currentName_.empty()) {
        h = CreateFileW(currentName_.c_str(), GENERIC_READ, FILE_SHARE_READ, NULL,
                        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
        temporary = true;
    }
    if (h == INVALID_HANDLE_VALUE) return -1;
    LARGE_INTEGER size;
    int result = GetFileSizeEx(h, &size) ? (int)(size.QuadPart & 0xFFFFFFFF) : -1;
    if (temporary) CloseHandle(h);
    return result;
}

const wchar_t* XmlReaderFileIO::GetCurrentFileName() { return currentName_.c_str(); }
void XmlReaderFileIO::Close() {
    if (handle_ != INVALID_HANDLE_VALUE) { CloseHandle(handle_); handle_ = INVALID_HANDLE_VALUE; }
    index_ = -1;
}
void XmlReaderFileIO::Destroy() { delete this; }

} // namespace xmlimpl
