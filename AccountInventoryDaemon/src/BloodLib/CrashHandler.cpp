// BloodLib - unhandled-exception reports and minidumps (original: CrashHandler.cpp).
#include "CrashHandler.h"
#include "Daemon.h"
#include "Log.h"
#include "Thread.h"

#include <dbghelp.h>
#include <time.h>

namespace blood {
namespace {

typedef BOOL (WINAPI *PFN_SymInitialize)(HANDLE, PSTR, BOOL);
typedef BOOL (WINAPI *PFN_SymCleanup)(HANDLE);
typedef BOOL (WINAPI *PFN_StackWalk64)(DWORD, HANDLE, HANDLE, STACKFRAME64*, void*, void*,
                                        void*, void*, void*);
typedef void* (WINAPI *PFN_SymFunctionTableAccess64)(HANDLE, DWORD64);
typedef DWORD64 (WINAPI *PFN_SymGetModuleBase64)(HANDLE, DWORD64);
typedef BOOL (WINAPI *PFN_SymGetSymFromAddr64)(HANDLE, DWORD64, PDWORD64, void*);
typedef BOOL (WINAPI *PFN_SymGetLineFromAddr64)(HANDLE, DWORD64, PDWORD, void*);
typedef DWORD (WINAPI *PFN_SymGetOptions)();
typedef DWORD (WINAPI *PFN_SymSetOptions)(DWORD);
typedef BOOL (WINAPI *PFN_MiniDumpWriteDump)(HANDLE, DWORD, HANDLE, MINIDUMP_TYPE,
                                              PMINIDUMP_EXCEPTION_INFORMATION, void*, void*);

struct DbgHelpApi {
    DWORD options;
    HMODULE module;
    PFN_SymInitialize SymInitialize;
    PFN_SymCleanup SymCleanup;
    PFN_StackWalk64 StackWalk64;
    PFN_SymFunctionTableAccess64 SymFunctionTableAccess64;
    PFN_SymGetModuleBase64 SymGetModuleBase64;
    PFN_SymGetSymFromAddr64 SymGetSymFromAddr64;
    PFN_SymGetLineFromAddr64 SymGetLineFromAddr64;
    PFN_SymGetOptions SymGetOptions;
    PFN_SymSetOptions SymSetOptions;
    PFN_MiniDumpWriteDump MiniDumpWriteDump;
};

DbgHelpApi g_dbg;
CRITICAL_SECTION g_lock;
bool g_lockReady = false;
LPTOP_LEVEL_EXCEPTION_FILTER g_oldFilter = NULL;
bool g_filterInstalled = false;
int g_reportFlags = 3;
int g_dumpFlags = 5;
bool g_enabled = false;
wchar_t g_fatalDirectory[MAX_PATH];
wchar_t g_fatalPrefix[MAX_PATH];

void EnsureLock() {
    if (!g_lockReady) {
        InitializeCriticalSectionAndSpinCount(&g_lock, 4000);
        g_lockReady = true;
    }
}

void CreateDirectoryTree(const wchar_t* path) {
    if (!path || !*path) return;
    wchar_t directory[MAX_PATH];
    wcscpy_s(directory, MAX_PATH, path);
    for (wchar_t* p = directory + 1; *p; ++p) {
        if (*p == L'\\' || *p == L'/') {
            wchar_t saved = *p;
            *p = L'\0';
            CreateDirectoryW(directory, NULL);
            *p = saved;
        }
    }
    CreateDirectoryW(directory, NULL);
}

const char* ExceptionName(DWORD code) {
    switch (code) {
    case 0x80000001: return "GUARD_PAGE";
    case 0x80000002: return "DATATYPE_MISALIGNMENT";
    case 0x80000003: return "BREAKPOINT";
    case 0x80000004: return "SINGLE_STEP";
    case 0xC0000005: return "ACCESS_VIOLATION";
    case 0xC0000006: return "IN_PAGE_ERROR";
    case 0xC0000008: return "INVALID_HANDLE";
    case 0xC000001D: return "ILLEGAL_INSTRUCTION";
    case 0xC0000025: return "NONCONTINUABLE_EXCEPTION";
    case 0xC0000026: return "INVALID_DISPOSITION";
    case 0xC000008C: return "ARRAY_BOUNDS_EXCEEDED";
    case 0xC000008D: return "FLT_DENORMAL_OPERAND";
    case 0xC000008E: return "FLT_DIVIDE_BY_ZERO";
    case 0xC000008F: return "FLT_INEXACT_RESULT";
    case 0xC0000090: return "FLT_INVALID_OPERATION";
    case 0xC0000091: return "FLT_OVERFLOW";
    case 0xC0000092: return "FLT_STACK_CHECK";
    case 0xC0000093: return "FLT_UNDERFLOW";
    case 0xC0000094: return "INT_DIVIDE_BY_ZERO";
    case 0xC0000095: return "INT_OVERFLOW";
    case 0xC0000096: return "PRIV_INSTRUCTION";
    case 0xC00000FD: return "STACK_OVERFLOW";
    case 0xC000FFFF: return "USER_ASSERT_FAILED";
    default: return "?";
    }
}

void WriteText(HANDLE file, const char* text, int length = -1) {
    if (!file || file == INVALID_HANDLE_VALUE || !text) return;
    DWORD written = 0;
    WriteFile(file, text, length >= 0 ? static_cast<DWORD>(length) : static_cast<DWORD>(strlen(text)),
              &written, NULL);
}

void WriteLine(HANDLE file, const char* format, ...) {
    char buffer[2048];
    va_list ap;
    va_start(ap, format);
    int length = _vsnprintf_s(buffer, sizeof(buffer), _TRUNCATE, format, ap);
    va_end(ap);
    if (length < 0) length = static_cast<int>(strlen(buffer));
    WriteText(file, buffer, length);
}

void WriteRegisters(HANDLE file, CONTEXT* context) {
    if (!context) return;
    WriteText(file, "\r\n\r\nRegisters:\r\n");
    char buffer[2048];
    sprintf_s(buffer, sizeof(buffer),
        "RAX=%016I64X  RBX=%016I64X  RCX=%016I64X  RDX=%016I64X\r\n"
        "RSI=%016I64X  RDI=%016I64X  RBP=%016I64X  RSP=%016I64X\r\n"
        "RIP=%016I64X  FLG=%016X   R8=%016I64X   R9=%016I64X\r\n"
        "R10=%016I64X  R11=%016I64X  R12=%016I64X  R13=%016I64X\r\n"
        "R14=%016I64X  R15=%016I64X\r\n",
        context->Rax, context->Rbx, context->Rcx, context->Rdx,
        context->Rsi, context->Rdi, context->Rbp, context->Rsp,
        context->Rip, context->EFlags, context->R8, context->R9,
        context->R10, context->R11, context->R12, context->R13,
        context->R14, context->R15);
    WriteText(file, buffer);
}

bool LoadDbgHelp(DbgHelpApi* api, const wchar_t* cwd) {
    memset(api, 0, sizeof(*api));
    api->module = LoadLibraryW(L"dbghelp.dll");
    if (!api->module) return false;
    api->SymInitialize = reinterpret_cast<PFN_SymInitialize>(GetProcAddress(api->module, "SymInitialize"));
    api->SymCleanup = reinterpret_cast<PFN_SymCleanup>(GetProcAddress(api->module, "SymCleanup"));
    api->StackWalk64 = reinterpret_cast<PFN_StackWalk64>(GetProcAddress(api->module, "StackWalk64"));
    api->SymFunctionTableAccess64 = reinterpret_cast<PFN_SymFunctionTableAccess64>(GetProcAddress(api->module, "SymFunctionTableAccess64"));
    api->SymGetModuleBase64 = reinterpret_cast<PFN_SymGetModuleBase64>(GetProcAddress(api->module, "SymGetModuleBase64"));
    api->SymGetSymFromAddr64 = reinterpret_cast<PFN_SymGetSymFromAddr64>(GetProcAddress(api->module, "SymGetSymFromAddr64"));
    api->SymGetLineFromAddr64 = reinterpret_cast<PFN_SymGetLineFromAddr64>(GetProcAddress(api->module, "SymGetLineFromAddr64"));
    api->SymGetOptions = reinterpret_cast<PFN_SymGetOptions>(GetProcAddress(api->module, "SymGetOptions"));
    api->SymSetOptions = reinterpret_cast<PFN_SymSetOptions>(GetProcAddress(api->module, "SymSetOptions"));
    api->MiniDumpWriteDump = reinterpret_cast<PFN_MiniDumpWriteDump>(GetProcAddress(api->module, "MiniDumpWriteDump"));
    if (!api->SymInitialize || !api->SymCleanup || !api->StackWalk64 ||
        !api->SymFunctionTableAccess64 || !api->SymGetModuleBase64 ||
        !api->SymGetSymFromAddr64 || !api->SymGetLineFromAddr64 ||
        !api->SymGetOptions || !api->SymSetOptions || !api->MiniDumpWriteDump) {
        FreeLibrary(api->module);
        memset(api, 0, sizeof(*api));
        return false;
    }
    api->options = api->SymGetOptions() | 0x14;
    api->SymSetOptions(api->options);
    if (cwd) SetCurrentDirectoryW(cwd);
    return api->SymInitialize(GetCurrentProcess(), NULL, TRUE) != FALSE;
}

void WriteCallStacks(HANDLE file, EXCEPTION_POINTERS* exception) {
    WriteText(file, "\r\n\r\nCall Stacks:\r\n");
    if (!g_dbg.StackWalk64 || !exception || !exception->ContextRecord) return;
    CONTEXT context = *exception->ContextRecord;
    STACKFRAME64 frame;
    memset(&frame, 0, sizeof(frame));
    frame.AddrPC.Offset = context.Rip;
    frame.AddrPC.Mode = AddrModeFlat;
    frame.AddrStack.Offset = context.Rsp;
    frame.AddrStack.Mode = AddrModeFlat;
    frame.AddrFrame.Offset = context.Rbp;
    frame.AddrFrame.Mode = AddrModeFlat;
    HANDLE process = GetCurrentProcess();
    for (int i = 0; i < 128; ++i) {
        HANDLE thread = GetCurrentThread();
        if (!g_dbg.StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, thread, &frame, &context,
                               NULL, g_dbg.SymFunctionTableAccess64,
                               g_dbg.SymGetModuleBase64, NULL))
            break;
        if (!frame.AddrFrame.Offset) break;
        WriteLine(file, "0x%016I64X", static_cast<u64>(frame.AddrPC.Offset));
        char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
        memset(symbolBuffer, 0, sizeof(symbolBuffer));
        SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
        symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
        symbol->MaxNameLen = 511;
        DWORD64 displacement = 0;
        if (g_dbg.SymGetSymFromAddr64(process, frame.AddrPC.Offset, &displacement, symbol)) {
            if (displacement) WriteLine(file, "  %s() + %I64d byte(s)", symbol->Name,
                                        static_cast<i64>(displacement));
            else WriteLine(file, "  %s", symbol->Name);
        }
        WriteLine(file, "\r\n\t[0x%016I64X 0x%016I64X 0x%016I64X 0x%016I64X]",
                  static_cast<u64>(frame.Params[0]), static_cast<u64>(frame.Params[1]),
                  static_cast<u64>(frame.Params[2]), static_cast<u64>(frame.Params[3]));
        IMAGEHLP_LINE64 line;
        memset(&line, 0, sizeof(line));
        line.SizeOfStruct = sizeof(line);
        DWORD lineDisplacement = 0;
        if (g_dbg.SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisplacement, &line)) {
            if (lineDisplacement)
                WriteLine(file, "\r\n\t{%s, line %d + %d byte(s)}", line.FileName,
                          line.LineNumber, lineDisplacement);
            else
                WriteLine(file, "\r\n\t{%s, line %d}", line.FileName, line.LineNumber);
        }
        WriteText(file, "\r\n\r\n");
    }
}

void WriteDump(const wchar_t* path, MINIDUMP_TYPE type, EXCEPTION_POINTERS* exception) {
    if (!g_dbg.MiniDumpWriteDump) return;
    HANDLE file = CreateFileW(path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                              FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN, NULL);
    if (file == INVALID_HANDLE_VALUE) return;
    MINIDUMP_EXCEPTION_INFORMATION info;
    info.ThreadId = GetCurrentThreadId();
    info.ExceptionPointers = exception;
    info.ClientPointers = FALSE;
    g_dbg.MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), file, type,
                            &info, NULL, NULL);
    CloseHandle(file);
}

void DescribeAddress(void* address, char* module, size_t moduleCapacity, u64& ordinal) {
    module[0] = '\0';
    ordinal = 0;
    MEMORY_BASIC_INFORMATION info;
    if (!VirtualQuery(address, &info, sizeof(info)) || !info.AllocationBase) return;
    wchar_t path[MAX_PATH];
    if (!GetModuleFileNameW(reinterpret_cast<HMODULE>(info.AllocationBase), path, MAX_PATH)) return;
    const wchar_t* name = wcsrchr(path, L'\\');
    name = name ? name + 1 : path;
    WideCharToMultiByte(CP_ACP, 0, name, -1, module, static_cast<int>(moduleCapacity), NULL, NULL);
    u8* base = static_cast<u8*>(info.AllocationBase);
    IMAGE_DOS_HEADER* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return;
    IMAGE_NT_HEADERS64* nt = reinterpret_cast<IMAGE_NT_HEADERS64*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return;
    IMAGE_DATA_DIRECTORY& directory = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (!directory.VirtualAddress || !directory.Size) return;
    IMAGE_EXPORT_DIRECTORY* exports = reinterpret_cast<IMAGE_EXPORT_DIRECTORY*>(base + directory.VirtualAddress);
    u32 rva = static_cast<u32>(reinterpret_cast<u8*>(address) - base);
    u32* functions = reinterpret_cast<u32*>(base + exports->AddressOfFunctions);
    u16* ordinals = reinterpret_cast<u16*>(base + exports->AddressOfNameOrdinals);
    for (u32 i = 0; i < exports->NumberOfFunctions; ++i) {
        if (rva >= functions[i]) {
            u32 end = i + 1 < exports->NumberOfFunctions && functions[i + 1] > functions[i] ?
                      functions[i + 1] : functions[i] + 0x1000;
            if (rva < end) { ordinal = exports->Base + i; return; }
        }
    }
    for (u32 i = 0; i < exports->NumberOfNames; ++i) {
        u32 function = functions[ordinals[i]];
        if (rva >= function) {
            u32 end = function + 0x1000;
            if (rva < end) { ordinal = exports->Base + ordinals[i]; return; }
        }
    }
}

LONG WINAPI CrashFilter(EXCEPTION_POINTERS* exception) {
    EnsureLock();
    EnterCriticalSection(&g_lock);
    if (g_oldFilter) SetUnhandledExceptionFilter(g_oldFilter);
    g_oldFilter = NULL;
    if (TheLog()) TheLog()->SignalFlush();
    DWORD code = exception && exception->ExceptionRecord ?
                 exception->ExceptionRecord->ExceptionCode : 0;
    if (code == STATUS_STACK_OVERFLOW) {
        OutputDebugStringW(L"***************************************\n");
        OutputDebugStringW(L"** EXCEPTION_STACK_OVERFLOW occurred **\n");
        OutputDebugStringW(L"***************************************\n");
    }

    wchar_t base[MAX_PATH];
    ThreadObject* thread = Thread_Current();
    if (thread) swprintf_s(base, MAX_PATH, L"%s%s(%d)", g_fatalPrefix, thread->Name(),
                           static_cast<int>(GetCurrentThreadId()));
    else swprintf_s(base, MAX_PATH, L"%sMain(%d)", g_fatalPrefix,
                    static_cast<int>(GetCurrentThreadId()));
    struct tm local;
    __time64_t now = _time64(NULL);
    _localtime64_s(&local, &now);
    size_t length = wcslen(base);
    wcsftime(base + length, MAX_PATH - length, L",%Y%m%d-%H%M%S", &local);
    wchar_t reportPath[MAX_PATH];
    wchar_t tinyPath[MAX_PATH];
    wchar_t dumpPath[MAX_PATH];
    swprintf_s(reportPath, MAX_PATH, L"%s.log", base);
    swprintf_s(tinyPath, MAX_PATH, L"%s,tiny.dmp", base);
    swprintf_s(dumpPath, MAX_PATH, L"%s.dmp", base);
    CreateDirectoryTree(g_fatalDirectory);

    if (!g_dbg.module) {
        wchar_t cwd[MAX_PATH];
        GetCurrentDirectoryW(MAX_PATH, cwd);
        LoadDbgHelp(&g_dbg, cwd);
    }
    if (g_reportFlags & 4) {
        if (g_dumpFlags & 2) WriteDump(tinyPath, MiniDumpNormal, exception);
        WriteDump(dumpPath, static_cast<MINIDUMP_TYPE>(g_dumpFlags), exception);
    }

    HANDLE file = CreateFileW(reportPath, GENERIC_WRITE, 0, NULL, OPEN_ALWAYS,
                              FILE_FLAG_WRITE_THROUGH, NULL);
    if (file != INVALID_HANDLE_VALUE) {
        SetFilePointer(file, 0, NULL, FILE_END);
        char timeText[64];
        strftime(timeText, sizeof(timeText), "%Y/%m/%d %H:%M:%S", &local);
        WriteLine(file, "Time: %s\r\n", timeText);
        WriteLine(file, "Code: %s [%08X]\r\n", ExceptionName(code), code);
        char module[MAX_PATH];
        u64 ordinal = 0;
        void* address = exception && exception->ExceptionRecord ?
                        exception->ExceptionRecord->ExceptionAddress : NULL;
        DescribeAddress(address, module, sizeof(module), ordinal);
        WriteLine(file, "Address: %016p %s [%04X:%016I64X]\r\n", address, module,
                  static_cast<unsigned>(ordinal), reinterpret_cast<u64>(address));
        if (code == kAssertExceptionCode && exception && exception->ExceptionRecord &&
            exception->ExceptionRecord->NumberParameters >= 1) {
            struct AssertInfo { const wchar_t* file; int line; wchar_t message[0x1000]; };
            const AssertInfo* assertInfo = reinterpret_cast<const AssertInfo*>(
                exception->ExceptionRecord->ExceptionInformation[0]);
            if (assertInfo) {
                WriteLine(file, "Assert Failed: %S, %d\r\n", assertInfo->file, assertInfo->line);
                WriteLine(file, "Assert Description: %S\r\n", assertInfo->message);
            }
        } else {
            WriteText(file, "Crashed at ");
            char symbolBuffer[sizeof(SYMBOL_INFO) + 512];
            memset(symbolBuffer, 0, sizeof(symbolBuffer));
            SYMBOL_INFO* symbol = reinterpret_cast<SYMBOL_INFO*>(symbolBuffer);
            symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
            symbol->MaxNameLen = 511;
            DWORD64 displacement = 0;
            if (g_dbg.SymGetSymFromAddr64 && address &&
                g_dbg.SymGetSymFromAddr64(GetCurrentProcess(), reinterpret_cast<DWORD64>(address),
                                          &displacement, symbol)) {
                if (displacement) WriteLine(file, "%s() + %I64d byte(s)", symbol->Name,
                                            static_cast<i64>(displacement));
                else WriteLine(file, "%s", symbol->Name);
            } else {
                WriteLine(file, "%s", module);
            }
            IMAGEHLP_LINE64 line;
            memset(&line, 0, sizeof(line));
            line.SizeOfStruct = sizeof(line);
            DWORD lineDisplacement = 0;
            if (g_dbg.SymGetLineFromAddr64 && address &&
                g_dbg.SymGetLineFromAddr64(GetCurrentProcess(), reinterpret_cast<DWORD64>(address),
                                           &lineDisplacement, &line)) {
                if (lineDisplacement)
                    WriteLine(file, " {%s, line %d + %d byte(s)}", line.FileName,
                              line.LineNumber, lineDisplacement);
                else
                    WriteLine(file, " {%s, line %d}", line.FileName, line.LineNumber);
            }
            WriteText(file, "\r\n");
            if (code == STATUS_ACCESS_VIOLATION && exception &&
                exception->ExceptionRecord->NumberParameters >= 2) {
                WriteLine(file, "Access Violataion: %s 0x%Ix\r\n",
                          exception->ExceptionRecord->ExceptionInformation[0] ? "Write to" : "Read from",
                          exception->ExceptionRecord->ExceptionInformation[1]);
            }
        }
        if (g_reportFlags & 1) WriteRegisters(file, exception ? exception->ContextRecord : NULL);
        if ((g_reportFlags & 2) && g_dbg.module) WriteCallStacks(file, exception);
        CloseHandle(file);
    }
    if (g_dbg.SymCleanup) g_dbg.SymCleanup(GetCurrentProcess());
    if (g_dbg.module) {
        FreeLibrary(g_dbg.module);
        memset(&g_dbg, 0, sizeof(g_dbg));
    }
    LeaveCriticalSection(&g_lock);
    return EXCEPTION_CONTINUE_SEARCH;
}

} // namespace

void CrashHandler_SetDumpType(int dumpType) {
    if (dumpType < 0) {
        g_reportFlags = 3;
        g_dumpFlags = 5;
    } else if (dumpType == 2) {
        g_reportFlags = 7;
        g_dumpFlags = 0x147;
    } else {
        g_reportFlags = 7;
        g_dumpFlags = 0x105;
    }
}

void CrashHandler_SetLogPath(const wchar_t* logPath) {
    wchar_t path[MAX_PATH];
    wcscpy_s(path, MAX_PATH, logPath ? logPath : L".\\log\\");
    size_t length = wcslen(path);
    if (!length || path[length - 1] != L'\\') wcsncat_s(path, MAX_PATH, L"\\", 1);
    wcsncat_s(path, MAX_PATH, L"fatal\\", _TRUNCATE);
    wcscpy_s(g_fatalDirectory, MAX_PATH, path);
    Daemon* daemon = Daemon::Instance();
    time_t start = daemon ? daemon->StartTime() : time(NULL);
    struct tm local;
    _localtime64_s(&local, &start);
    swprintf_s(g_fatalPrefix, MAX_PATH, L"%s%s(%04d%02d%02d-%02d%02d%02d,%05d),",
               path, daemon ? daemon->GetLogTag() : L"achievement-daemon",
               local.tm_year + 1900, local.tm_mon + 1, local.tm_mday,
               local.tm_hour, local.tm_min, local.tm_sec,
               static_cast<int>(GetCurrentProcessId()));
    CreateDirectoryTree(path);
}

bool CrashHandler_Init() {
    EnsureLock();
    if (!g_filterInstalled) {
        g_oldFilter = SetUnhandledExceptionFilter(&CrashFilter);
        g_filterInstalled = true;
    }
    g_enabled = true;
    g_reportFlags = 3;
    g_dumpFlags = 5;
    CrashHandler_SetLogPath(L".\\log\\");
    return true;
}

bool CrashHandler_Init(const wchar_t* logPath, int dumpType) {
    if (!CrashHandler_Init()) return false;
    CrashHandler_SetDumpType(dumpType);
    CrashHandler_SetLogPath(logPath);
    return true;
}

} // namespace blood
