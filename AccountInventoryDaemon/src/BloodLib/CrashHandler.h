// BloodLib - unhandled-exception reports and minidumps (original: CrashHandler.h).
#pragma once
#include "BloodLib.h"

namespace blood {

bool CrashHandler_Init();
bool CrashHandler_Init(const wchar_t* logPath, int dumpType);
void CrashHandler_SetLogPath(const wchar_t* logPath);
void CrashHandler_SetDumpType(int dumpType);

} // namespace blood
