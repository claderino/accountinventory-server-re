// SOURCE_FILE("XmlParser.cpp");
// Hand-written strict XML parser (no third-party library). Builds the DOM used by
// XmlReader_sv64. Supports: XML declaration, UTF-8/UTF-16 (with/without BOM),
// elements/empty elements, single/double-quoted attributes, the five predefined
// entities, numeric character references, comments, PIs, CDATA, DOCTYPE (skipped,
// simple internal entities honoured), mixed content and whitespace text nodes.
#include "XmlReaderInternal.h"

#include <cstdlib>
#include <cwctype>

namespace xmlimpl {
namespace {

static void AppendUtf16(std::wstring& out, unsigned cp) {
    if (cp <= 0xFFFF) {
        out.push_back(static_cast<wchar_t>(cp));
    } else {
        cp -= 0x10000;
        out.push_back(static_cast<wchar_t>(0xD800 + (cp >> 10)));
        out.push_back(static_cast<wchar_t>(0xDC00 + (cp & 0x3FF)));
    }
}

// Converts the raw file bytes to wide text and normalises line endings.
static bool BytesToWide(const unsigned char* b, size_t n, std::wstring& out, std::wstring* err) {
    size_t i = 0;
    bool utf16 = false, bigEndian = false;
    if (n >= 3 && b[0] == 0xEF && b[1] == 0xBB && b[2] == 0xBF) {
        i = 3;
    } else if (n >= 2 && b[0] == 0xFF && b[1] == 0xFE) {
        utf16 = true; i = 2;
    } else if (n >= 2 && b[0] == 0xFE && b[1] == 0xFF) {
        utf16 = true; bigEndian = true; i = 2;
    } else if (n >= 2 && b[0] == 0 && b[1] == L'<') {
        utf16 = true; bigEndian = true;
    } else if (n >= 2 && b[0] == L'<' && b[1] == 0) {
        utf16 = true;
    }
    out.clear();
    if (utf16) {
        if ((n - i) & 1) { *err = L"odd-length UTF-16 input"; return false; }
        for (; i + 1 < n; i += 2) {
            unsigned cp = bigEndian ? ((unsigned)b[i] << 8) | b[i + 1]
                                    : ((unsigned)b[i + 1] << 8) | b[i];
            out.push_back(static_cast<wchar_t>(cp));
        }
    } else {
        while (i < n) {
            unsigned char c = b[i];
            if (c == 0) { *err = L"NUL byte in UTF-8 input"; return false; }
            if (c < 0x80) { out.push_back(static_cast<wchar_t>(c)); ++i; continue; }
            unsigned need = 0, cp = 0, minCp = 0;
            if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; minCp = 0x80; }
            else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; minCp = 0x800; }
            else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; minCp = 0x10000; }
            else { *err = L"invalid UTF-8 lead byte"; return false; }
            if (i + need >= n) { *err = L"truncated UTF-8 sequence"; return false; }
            for (unsigned k = 1; k <= need; ++k) {
                unsigned char d = b[i + k];
                if ((d & 0xC0) != 0x80) { *err = L"invalid UTF-8 continuation byte"; return false; }
                cp = (cp << 6) | (d & 0x3F);
            }
            if (cp < minCp || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF)) {
                *err = L"invalid UTF-8 code point"; return false;
            }
            AppendUtf16(out, cp);
            i += need + 1;
        }
    }
    // XML line-ending normalisation: CRLF and lone CR become LF.
    std::wstring norm;
    norm.reserve(out.size());
    for (size_t k = 0; k < out.size(); ++k) {
        if (out[k] == L'\r') {
            norm.push_back(L'\n');
            if (k + 1 < out.size() && out[k + 1] == L'\n') ++k;
        } else {
            norm.push_back(out[k]);
        }
    }
    out.swap(norm);
    return true;
}

static bool IsNameStart(wchar_t c) {
    return (c >= L'a' && c <= L'z') || (c >= L'A' && c <= L'Z') || c == L'_' || c == L':' || c >= 0x80;
}
static bool IsNameChar(wchar_t c) {
    return IsNameStart(c) || (c >= L'0' && c <= L'9') || c == L'-' || c == L'.' || c == 0xB7;
}

class Parser {
public:
    Parser(const std::wstring& text, XmlDocImpl* doc)
        : s_(text), p_(0), line_(1), doc_(doc), errorLine_(1) {}

    bool Run(int* errorLine, std::wstring* errorText) {
        bool ok = ParseDocumentBody();
        if (!ok) {
            if (errorLine) *errorLine = errorLine_;
            if (errorText) *errorText = errorText_;
        }
        return ok;
    }

private:
    const std::wstring& s_;
    size_t p_;
    int line_;
    XmlDocImpl* doc_;
    int errorLine_;
    std::wstring errorText_;
    std::map<std::wstring, std::wstring> entities_;

    bool Fail(const wchar_t* what) {
        if (errorText_.empty()) {
            wchar_t buf[256];
            swprintf(buf, 256, L"%s at line %d", what, line_);
            errorText_ = buf;
            errorLine_ = line_;
        }
        return false;
    }
    bool Eof() const { return p_ >= s_.size(); }
    wchar_t Peek(size_t ahead = 0) const { return p_ + ahead < s_.size() ? s_[p_ + ahead] : L'\0'; }
    void Bump() { if (p_ < s_.size()) { if (s_[p_] == L'\n') ++line_; ++p_; } }
    bool StartsWith(const wchar_t* lit) const {
        size_t n = wcslen(lit);
        return p_ + n <= s_.size() && s_.compare(p_, n, lit) == 0;
    }
    void SkipWs() { while (!Eof() && (Peek() == L' ' || Peek() == L'\t' || Peek() == L'\n')) Bump(); }
    bool Expect(const wchar_t* lit) {
        if (!StartsWith(lit)) return Fail(L"unexpected token");
        for (size_t k = 0; k < wcslen(lit); ++k) Bump();
        return true;
    }
    bool ParseName(std::wstring& name) {
        if (Eof() || !IsNameStart(Peek())) return Fail(L"invalid name");
        name.clear();
        name.push_back(Peek()); Bump();
        while (!Eof() && IsNameChar(Peek())) { name.push_back(Peek()); Bump(); }
        return true;
    }

    bool Decode(const std::wstring& in, std::wstring& out, bool inAttribute) {
        out.clear();
        for (size_t i = 0; i < in.size(); ++i) {
            wchar_t c = in[i];
            if (c != L'&') {
                if (inAttribute && (c == L'\t' || c == L'\n')) c = L' ';
                out.push_back(c);
                continue;
            }
            size_t semi = in.find(L';', i + 1);
            if (semi == std::wstring::npos || semi - i > 32) return Fail(L"unterminated entity");
            std::wstring ent = in.substr(i + 1, semi - i - 1);
            unsigned cp = 0;
            if (ent == L"lt") cp = L'<';
            else if (ent == L"gt") cp = L'>';
            else if (ent == L"amp") cp = L'&';
            else if (ent == L"apos") cp = L'\'';
            else if (ent == L"quot") cp = L'"';
            else if (ent.size() > 1 && ent[0] == L'#') {
                const wchar_t* digits = ent.c_str() + 1;
                int base = 10;
                if (*digits == L'x' || *digits == L'X') { base = 16; ++digits; }
                if (!*digits) return Fail(L"invalid character reference");
                for (const wchar_t* d = digits; *d; ++d) {
                    unsigned v;
                    if (*d >= L'0' && *d <= L'9') v = (unsigned)(*d - L'0');
                    else if (base == 16 && *d >= L'a' && *d <= L'f') v = (unsigned)(*d - L'a' + 10);
                    else if (base == 16 && *d >= L'A' && *d <= L'F') v = (unsigned)(*d - L'A' + 10);
                    else return Fail(L"invalid character reference");
                    if (cp > (0x10FFFFu - v) / (unsigned)base) return Fail(L"character reference out of range");
                    cp = cp * (unsigned)base + v;
                }
                if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
                    return Fail(L"character reference out of range");
            } else {
                std::map<std::wstring, std::wstring>::const_iterator it = entities_.find(ent);
                if (it == entities_.end()) return Fail(L"undefined entity");
                std::wstring decoded;
                if (!Decode(it->second, decoded, inAttribute)) return false;
                out += decoded;
                i = semi;
                continue;
            }
            AppendUtf16(out, cp);
            i = semi;
        }
        return true;
    }

    bool ParseComment() {
        if (!Expect(L"<!--")) return false;
        std::wstring text;
        while (!Eof() && !StartsWith(L"-->")) {
            if (StartsWith(L"--")) return Fail(L"invalid comment");
            if (Peek() == L'\n') text.push_back(L'\n');
            Bump();
        }
        if (Eof()) return Fail(L"unterminated comment");
        Bump(); Bump(); Bump();
        XmlElementImpl* node = doc_->NewNode(kNodeComment, line_);
        node->SetText(text);
        return true;
    }

    bool ParsePI() {
        if (!Expect(L"<?")) return false;
        std::wstring target;
        if (!ParseName(target)) return false;
        while (!Eof() && !StartsWith(L"?>")) Bump();
        if (Eof()) return Fail(L"unterminated processing instruction");
        Bump(); Bump();
        XmlElementImpl* node = doc_->NewNode(kNodePI, line_);
        node->SetName(target);
        return true;
    }

    bool ParseCData() {
        if (!Expect(L"<![CDATA[")) return false;
        std::wstring text;
        while (!Eof() && !StartsWith(L"]]>")) { text.push_back(Peek()); Bump(); }
        if (Eof()) return Fail(L"unterminated CDATA section");
        Bump(); Bump(); Bump();
        XmlElementImpl* node = doc_->NewNode(kNodeCData, line_);
        node->SetText(text);
        return true;
    }

    // Skips <!DOCTYPE ...>, collecting simple internal entity declarations.
    bool ParseDoctype() {
        if (!Expect(L"<!DOCTYPE")) return false;
        int brackets = 0;
        wchar_t quote = 0;
        std::wstring raw;
        while (!Eof()) {
            wchar_t c = Peek();
            if (quote) {
                raw.push_back(c);
                if (c == quote) quote = 0;
                Bump();
                continue;
            }
            if (c == L'\'' || c == L'"') { quote = c; raw.push_back(c); Bump(); continue; }
            if (c == L'[') ++brackets;
            if (c == L']' && brackets > 0) --brackets;
            if (c == L'>' && brackets == 0) { Bump(); break; }
            raw.push_back(c);
            Bump();
        }
        if (brackets != 0 || quote) return Fail(L"unterminated DOCTYPE");
        size_t at = 0;
        while ((at = raw.find(L"<!ENTITY", at)) != std::wstring::npos) {
            size_t q = at + 8;
            while (q < raw.size() && (raw[q] == L' ' || raw[q] == L'\t' || raw[q] == L'\n')) ++q;
            size_t ns = q;
            while (q < raw.size() && IsNameChar(raw[q])) ++q;
            std::wstring name = raw.substr(ns, q - ns);
            while (q < raw.size() && raw[q] != L'\'' && raw[q] != L'"') ++q;
            if (q < raw.size() && !name.empty()) {
                wchar_t qc = raw[q++];
                size_t vs = q;
                while (q < raw.size() && raw[q] != qc) ++q;
                if (q < raw.size()) entities_[name] = raw.substr(vs, q - vs);
            }
            at += 8;
        }
        return true;
    }

    bool ParseDeclaration() {
        // XML declaration is only legal at the very beginning of the document.
        if (!StartsWith(L"<?xml")) return Fail(L"invalid processing instruction");
        wchar_t after = Peek(5);
        if (after != L' ' && after != L'\t' && after != L'\r' && after != L'\n' && after != L'?')
            return Fail(L"invalid processing instruction");
        while (!Eof() && !StartsWith(L"?>")) Bump();
        if (Eof()) return Fail(L"unterminated XML declaration");
        Bump(); Bump();
        return true;
    }

    bool ParseMisc(bool beforeRoot) {
        bool any = false;
        for (;;) {
            if (!Eof() && (Peek() == L' ' || Peek() == L'\t' || Peek() == L'\n')) { SkipWs(); any = true; continue; }
            if (StartsWith(L"<!--")) { if (!ParseComment()) return false; any = true; continue; }
            if (StartsWith(L"<?")) { if (!ParsePI()) return false; any = true; continue; }
            if (beforeRoot && StartsWith(L"<!DOCTYPE")) {
                if (!ParseDoctype()) return false; any = true; continue;
            }
            return any;
        }
    }

    bool ParseDocumentBody() {
        if (StartsWith(L"<?xml") && !ParseDeclaration()) return false;
        ParseMisc(true);
        if (Eof() || Peek() != L'<') return Fail(L"no root element");
        XmlElementImpl* root = NULL;
        if (!ParseElement(NULL, root)) return false;
        ParseMisc(false);
        if (!Eof()) return Fail(L"content after root element");
        return true;
    }

    bool ParseElement(XmlElementImpl* parent, XmlElementImpl*& out) {
        int startLine = line_;
        if (Peek() != L'<') return Fail(L"expected element");
        Bump();
        std::wstring name;
        if (!ParseName(name)) return false;
        XmlElementImpl* node = doc_->NewNode(kNodeElement, startLine);
        node->SetName(name);

        bool selfClosing = false;
        for (;;) {
            SkipWs();
            if (StartsWith(L"/>")) { Bump(); Bump(); selfClosing = true; break; }
            if (Peek() == L'>') { Bump(); break; }
            if (Eof()) return Fail(L"unterminated start tag");
            std::wstring attrName;
            if (!ParseName(attrName)) return false;
            SkipWs();
            if (Peek() != L'=') return Fail(L"attribute has no value");
            Bump();
            SkipWs();
            wchar_t quote = Peek();
            if (quote != L'\'' && quote != L'"') return Fail(L"unquoted attribute value");
            Bump();
            std::wstring raw;
            while (!Eof() && Peek() != quote) {
                if (Peek() == L'<') return Fail(L"'<' in attribute value");
                raw.push_back(Peek()); Bump();
            }
            if (Eof()) return Fail(L"unterminated attribute value");
            Bump();
            for (size_t i = 0; i < node->Attributes().size(); ++i) {
                if (node->Attributes()[i].name == attrName)
                    return Fail(L"duplicate attribute");
            }
            std::wstring value;
            if (!Decode(raw, value, true)) return false;
            node->AddAttribute(attrName, value);
        }

        if (!selfClosing) {
            for (;;) {
                if (Eof()) return Fail(L"unexpected end of file");
                if (StartsWith(L"</")) {
                    Bump(); Bump();
                    std::wstring endName;
                    if (!ParseName(endName)) return false;
                    SkipWs();
                    if (Peek() != L'>') return Fail(L"unterminated end tag");
                    Bump();
                    if (endName != name) return Fail(L"mismatched end tag");
                    break;
                }
                if (StartsWith(L"<!--")) { if (!ParseComment()) return false; continue; }
                if (StartsWith(L"<![CDATA[")) { if (!ParseCData()) return false; continue; }
                if (StartsWith(L"<?")) { if (!ParsePI()) return false; continue; }
                if (Peek() == L'<') {
                    XmlElementImpl* child = NULL;
                    if (!ParseElement(node, child)) return false;
                    continue;
                }
                std::wstring raw;
                while (!Eof() && Peek() != L'<') { raw.push_back(Peek()); Bump(); }
                std::wstring text;
                if (!Decode(raw, text, false)) return false;
                XmlElementImpl* textNode = doc_->NewNode(kNodeText, line_);
                textNode->SetText(text);
            }
        }

        if (parent) {
            parent->AddChild(node);
            node->SetParent(parent, parent->ElementChildren().size() - 1);
        } else {
            doc_->SetRoot(node);
        }
        out = node;
        return true;
    }
};

} // namespace

bool ParseDocument(const void* bytes, size_t byteCount, XmlDocImpl* doc,
                   int* errorLine, std::wstring* errorText) {
    if (!bytes || byteCount == 0) {
        if (errorLine) *errorLine = 1;
        if (errorText) *errorText = L"buffer is empty";
        return false;
    }
    std::wstring text;
    std::wstring convError;
    if (!BytesToWide(static_cast<const unsigned char*>(bytes), byteCount, text, &convError)) {
        if (errorLine) *errorLine = 1;
        if (errorText) *errorText = convError;
        return false;
    }
    Parser parser(text, doc);
    return parser.Run(errorLine, errorText);
}

} // namespace xmlimpl
