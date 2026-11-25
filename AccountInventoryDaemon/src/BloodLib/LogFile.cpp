// BloodLib - hourly rotating file log medium (original: LogFile.cpp).
#include "Daemon.h"
#include "Log.h"

#include <time.h>
#include <vector>

SOURCE_FILE("LogFile.cpp");

namespace blood {
namespace {

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

} // namespace

static_assert(offsetof(LogFile, requestedSize) == 0xB8, "LogFile ABI");
static_assert(offsetof(LogFile, capacity) == 0xBC, "LogFile ABI");
static_assert(offsetof(LogFile, used) == 0xC0, "LogFile ABI");
static_assert(offsetof(LogFile, buffer) == 0xC8, "LogFile ABI");
static_assert(offsetof(LogFile, baseName) == 0xD0, "LogFile ABI");
static_assert(offsetof(LogFile, codePage) == 0xD8, "LogFile ABI");
static_assert(offsetof(LogFile, rotationMode) == 0xDC, "LogFile ABI");
static_assert(offsetof(LogFile, nextRotation) == 0xE8, "LogFile ABI");
static_assert(offsetof(LogFile, lastName) == 0xF8, "LogFile ABI");
static_assert(offsetof(LogFile, lastWritten) == 0x300, "LogFile ABI");
static_assert(sizeof(LogFile) == 0x508, "LogFile ABI");

LogFile::LogFile(const wchar_t* logPath, DWORD newCodePage)
    : requestedSize(8000), capacity(0), used(0), buffer(NULL), baseName(NULL),
      codePage(newCodePage), rotationMode(2), instance(0), nextRotation(0), interval(3600),
      reservedF4(0) {
    memset(lastName, 0, sizeof(lastName));
    memset(lastWritten, 0, sizeof(lastWritten));
    // Initialize the file medium (base name, buffer, rotation) - without this the
    // daemon logs into a void because WriteLog/Flush have no file name.
    Initialize(logPath);
}

LogFile::~LogFile() {
    Flush();
    if (buffer) Memory_Free(buffer);
    if (baseName) Memory_Free(baseName);
    buffer = NULL;
    baseName = NULL;
}

bool LogFile::Initialize(const wchar_t* logPath) {
    rotationMode = 2;
    requestedSize = requestedSize < 8000 ? 8000 : requestedSize;
    wchar_t path[MAX_PATH];
    wcscpy_s(path, MAX_PATH, logPath ? logPath : L".");
    size_t length = wcslen(path);
    if (!length || path[length - 1] != L'\\') wcsncat_s(path, MAX_PATH, L"\\", 1);
    CreateDirectoryTree(path);

    Daemon* daemon = Daemon::Instance();
    time_t start = daemon ? daemon->StartTime() : time(NULL);
    struct tm startLocal;
    _localtime64_s(&startLocal, &start);
    wchar_t prefix[512];
    swprintf_s(prefix, 512, L"%s(%04d%02d%02d-%02d%02d%02d,%05d)",
               daemon ? daemon->GetLogTag() : L"achievement-daemon",
               startLocal.tm_year + 1900, startLocal.tm_mon + 1, startLocal.tm_mday,
               startLocal.tm_hour, startLocal.tm_min, startLocal.tm_sec,
               static_cast<int>(GetCurrentProcessId()));
    size_t bytes = (wcslen(path) + wcslen(prefix) + 1) * sizeof(wchar_t);
    baseName = static_cast<wchar_t*>(Memory_Allocate(bytes, L"src\\LogFile.cpp", 0xE1));
    if (!baseName) return false;
    swprintf_s(baseName, bytes / sizeof(wchar_t), L"%s%s", path, prefix);

    __time64_t now = _time64(NULL);
    struct tm local;
    _localtime64_s(&local, &now);
    local.tm_min = 0;
    local.tm_sec = 0;
    nextRotation = _mktime64(&local);
    interval = 3600;

    capacity = requestedSize;
    buffer = static_cast<u8*>(Memory_Allocate(capacity, L"src\\LogFile.cpp", 0x181));
    return buffer != NULL;
}

void LogFile::MakeName(const struct tm* time, wchar_t* output) {
    wcscpy_s(output, MAX_PATH, baseName ? baseName : L"");
    if (!time) {
        wcsncat_s(output, MAX_PATH, L".log", 4);
        return;
    }
    wchar_t suffix[64];
    if (rotationMode == 0 || rotationMode == 1) {
        int minute = (time->tm_min / (rotationMode == 0 ? 15 : 30)) *
                     (rotationMode == 0 ? 15 : 30);
        swprintf_s(suffix, 64, L"%04d%02d%02d-%02d_%02d", time->tm_year + 1900,
                   time->tm_mon + 1, time->tm_mday, time->tm_hour, minute);
    } else if (rotationMode == 4) {
        swprintf_s(suffix, 64, L"%04d%02d%02d-%02d%02d%02d", time->tm_year + 1900,
                   time->tm_mon + 1, time->tm_mday, time->tm_hour, time->tm_min,
                   time->tm_sec);
    } else {
        swprintf_s(suffix, 64, L"%04d%02d%02d-%02d", time->tm_year + 1900,
                   time->tm_mon + 1, time->tm_mday, time->tm_hour);
    }
    wcsncat_s(output, MAX_PATH, suffix, _TRUNCATE);
    if (wcscmp(output, lastName) != 0) instance = 0;
    if (instance > 0) swprintf_s(suffix, 64, L"_%03d.log", instance);
    else wcscpy_s(suffix, 64, L".log");
    wcsncat_s(output, MAX_PATH, suffix, _TRUNCATE);
    wcscpy_s(lastName, MAX_PATH, output);
}

void LogFile::EnsureCapacity(size_t required) {
    if (required <= capacity) return;
    u32 newCapacity = capacity ? capacity : 8000;
    while (newCapacity < required) newCapacity *= 2;
    u8* grown = static_cast<u8*>(Memory_Reallocate(buffer, newCapacity,
                                                   L"src\\LogFile.cpp", 0x181));
    if (!grown) return;
    buffer = grown;
    capacity = newCapacity;
}

void LogFile::AppendBytes(const void* data, size_t bytes) {
    if (!bytes) return;
    EnsureCapacity(used + bytes);
    if (used + bytes > capacity) return;
    memcpy(buffer + used, data, bytes);
    used += static_cast<u32>(bytes);
}

void LogFile::AppendWide(const wchar_t* text) {
    if (!text) return;
    int bytes = WideCharToMultiByte(codePage, 0, text, -1, NULL, 0, NULL, NULL);
    if (bytes <= 1) return;
    std::vector<char> converted(static_cast<size_t>(bytes));
    if (WideCharToMultiByte(codePage, 0, text, -1, converted.data(), bytes, NULL, NULL) > 0)
        AppendBytes(converted.data(), static_cast<size_t>(bytes - 1));
}

void LogFile::FormatLine(LogBuffer* record) {
    SYSTEMTIME stamp;
    memcpy(&stamp, reinterpret_cast<const u8*>(record) + 0x68, sizeof(stamp));
    wchar_t timestamp[128];
    swprintf_s(timestamp, 128, L"%04d/%02d/%02d-%02d:%02d:%02d.%03d%s%s",
               stamp.wYear, stamp.wMonth, stamp.wDay, stamp.wHour, stamp.wMinute,
               stamp.wSecond, stamp.wMilliseconds, L",", L"");

    wchar_t name[MAX_PATH];
    wchar_t extension[MAX_PATH];
    _wsplitpath_s(record->file ? record->file : L"", NULL, 0, NULL, 0,
                  name, MAX_PATH, extension, MAX_PATH);
    wchar_t line[64];
    swprintf_s(line, 64, L",%d,", record->line);   // original: File.cpp,123,(level),msg
    wchar_t level[64];
    swprintf_s(level, 64, L"(%s),", LogLevelName(record->level));

    AppendWide(timestamp);
    AppendWide(name);
    AppendWide(extension);
    AppendWide(line);
    AppendWide(level);
    AppendWide(record->message);
    static const char terminator[2] = { 0x0D, 0x0A };   // no NUL: the original writes CRLF only
    AppendBytes(terminator, sizeof(terminator));
}

void LogFile::WriteLog(LogBuffer* head) {
    __time64_t now = _time64(NULL);
    if (nextRotation + interval < now) {
        Flush();
        nextRotation += interval;
    }
    for (LogBuffer* record = head; record; record = record->next[index]) {
        if (!record->enabled) continue;
        FormatLine(record);
    }
    Flush();
}

void LogFile::Flush() {
    if (!used || !buffer) return;
    struct tm local;
    _localtime64_s(&local, &nextRotation);
    wchar_t name[MAX_PATH];
    MakeName(&local, name);
    HANDLE file = INVALID_HANDLE_VALUE;
    for (int attempt = 0; attempt < 100; ++attempt) {
        // OPEN_ALWAYS preserves earlier flushes; CREATE_ALWAYS truncated the file on every
        // flush, leaving only the last batch of log lines.
        file = CreateFileW(name, GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN,
                           NULL);
        if (file != INVALID_HANDLE_VALUE) break;
        Sleep(100);
    }
    if (file == INVALID_HANDLE_VALUE) {
        ++instance;
        Assert_FailW(L"source\\LogFile.cpp", 0x12D,
                     L"[log], file create error, fname:%s, error:%d, trycount:%d",
                     name, static_cast<int>(GetLastError()), 100);
        used = 0;
        return;
    }
    wcscpy_s(lastWritten, MAX_PATH, name);
    SetFilePointer(file, 0, NULL, FILE_END);
    DWORD written = 0;
    WriteFile(file, buffer, used, &written, NULL);
    CloseHandle(file);
    used = 0;
}

} // namespace blood
