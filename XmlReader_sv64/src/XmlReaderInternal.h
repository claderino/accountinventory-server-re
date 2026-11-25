// Private implementation declarations of the XmlReader_sv64 reconstruction.
// The public ABI is the frozen header ..\RankingDaemon\src\BloodLib\XmlReader.h.
#pragma once
#include "XmlReader.h"
#include <string>
#include <vector>
#include <map>

namespace xmlimpl {

using blood::IXmlConfigReader;
using blood::IXmlDoc;
using blood::IXmlElement;
using blood::IXmlFileIO;
using blood::IXmlNode;
using blood::IXmlReader;
using blood::IXmlSaxBuilder;

// Node types. Only ELEMENT (1) is observable through the host's element walk.
enum NodeType { kNodeElement = 1, kNodeText = 3, kNodeCData = 4, kNodeComment = 8, kNodePI = 7 };

struct XmlAttr {
    std::wstring name;
    std::wstring value;
};

class XmlDocImpl;
class XmlElementImpl;

// ---------------------------------------------------------------------------
// DOM element/node.  Two adjacent interface subobjects: the element API at +0
// and the node interface at +8 (IXmlElement has a vptr, IXmlNode has a vptr).
//
// NOTE: the frozen XmlReader.h declares IXmlElement slots at 0x00..0x60 and
// then +0x70, i.e. it leaves a hole at +0x68 (the original vtable has a dummy
// there).  A compiler-generated vtable would put GetReserved70 in that hole,
// so this class installs an explicit 15-entry vtable array instead.  Every
// entry delegates to a normal overridden method.
// ---------------------------------------------------------------------------
class XmlElementImpl : public IXmlElement, public IXmlNode {
public:
    XmlElementImpl(XmlDocImpl* owner, int type, int line);
    ~XmlElementImpl();

    // IXmlElement
    int GetChildCount() override;
    IXmlElement* GetFirstChildElement() override;
    IXmlElement* GetNextSiblingElement() override;
    const wchar_t* GetName() override;
    int GetLine() override;
    int GetAttributeCount() override;
    const wchar_t* GetAttributeName(int index) override;
    IXmlElement* GetChildAt(int index) override;
    const wchar_t* GetAttribute(const wchar_t* name) override;
    int GetAttributeIndex(const wchar_t* name) override;
    const wchar_t* GetAttributeValue(int index) override;
    IXmlNode* GetNode() override;
    IXmlNode* GetNode2() override;
    int GetReserved70() override;

    // IXmlNode
    int GetType() override;
    bool IsValid() override;

    void SetName(const std::wstring& name) { name_ = name; }
    void SetText(const std::wstring& text) { text_ = text; }
    void AddAttribute(const std::wstring& name, const std::wstring& value);
    void AddChild(XmlElementImpl* child);
    void SetParent(XmlElementImpl* parent, size_t index) { parent_ = parent; indexInParent_ = index; }
    XmlDocImpl* Owner() const { return owner_; }
    int Type() const { return type_; }
    int Line() const { return line_; }
    const std::wstring& Name() const { return name_; }
    const std::wstring& Text() const { return text_; }
    const std::vector<XmlAttr>& Attributes() const { return attrs_; }
    const std::vector<XmlElementImpl*>& ElementChildren() const { return children_; }

    // Installs the explicit vtables. Called by the factory immediately after new.
    void InstallVTables();

private:
    XmlDocImpl* owner_;
    std::wstring name_;
    std::wstring text_;
    int type_;
    int line_;
    bool valid_;
    XmlElementImpl* parent_;
    size_t indexInParent_;
    std::vector<XmlAttr> attrs_;
    std::vector<XmlElementImpl*> children_;  // element nodes only (text/comments are not iterated)
};

// ---------------------------------------------------------------------------
// XmlDocImpl - owns every DOM node (the nodes are freed by the document).
// ---------------------------------------------------------------------------
class XmlDocImpl : public IXmlDoc {
public:
    XmlDocImpl();
    ~XmlDocImpl();

    bool IsValid() override;
    void* GetInternal() override;
    IXmlElement* GetRootElement() override;
    size_t GetSerializedSize() override;
    bool Serialize(void* buffer, size_t size) override;
    bool Deserialize(const void* buffer, size_t size) override;
    void Destroy() override;
    void Delete() override;

    XmlElementImpl* NewNode(int type, int line);
    void SetRoot(XmlElementImpl* root) { root_ = root; valid_ = root != NULL; }
    XmlElementImpl* Root() const { return root_; }
    const std::wstring& FileName() const { return fileName_; }
    void SetFileName(const wchar_t* name) { fileName_ = name ? name : L""; }
    void ReleaseNodes();

private:
    bool valid_;
    XmlElementImpl* root_;
    std::wstring fileName_;
    std::vector<XmlElementImpl*> nodes_;
};

// ---------------------------------------------------------------------------
// Parsing. Returns false and fills errorLine/errorText on a malformed document.
// ---------------------------------------------------------------------------
bool ParseDocument(const void* bytes, size_t byteCount, XmlDocImpl* doc,
                   int* errorLine, std::wstring* errorText);

// Report a reader diagnostic (error sink + thread-local last-error buffer).
void ReportXmlError(int backend, const wchar_t* fmt, ...);
const std::wstring& LastXmlError();

// ---------------------------------------------------------------------------
// Default file enumerator owned by each IXmlReader instance.
// ---------------------------------------------------------------------------
class XmlReaderFileIO : public IXmlFileIO {
public:
    XmlReaderFileIO();
    ~XmlReaderFileIO();

    int Init(const wchar_t* directory, const wchar_t* wildcard, int flags) override;
    int Read(void* buffer, int* capacity) override;
    int GetFileSize() override;
    const wchar_t* GetCurrentFileName() override;
    int OpenNext() override;
    void Close() override;
    void Destroy() override;

private:
    std::vector<std::wstring> files_;
    std::wstring currentName_;
    HANDLE handle_;
    int index_;
};

} // namespace xmlimpl
