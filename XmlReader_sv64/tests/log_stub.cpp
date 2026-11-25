// Minimal BloodLib logging stubs needed only to link XmlReaderLib.cpp into the test.
#include "XmlReader.h"

namespace blood {

void LogWriteV(int, const wchar_t*, int, const wchar_t*, va_list) {}
void LogWrite(int, const wchar_t*, int, const wchar_t*, const wchar_t*, const wchar_t*, ...) {}
void LogWrite0(int, const wchar_t*, int, const wchar_t*, const wchar_t*, const wchar_t*, ...) {}
void Assert_FailW(const wchar_t*, int, const wchar_t*, ...) {}
void Assert_FailA(const wchar_t*, int, const wchar_t*, ...) {}
void SetAssertFailHandler(AssertFailHandler) {}
const wchar_t* LogLevelName(int) { return L""; }

} // namespace blood
