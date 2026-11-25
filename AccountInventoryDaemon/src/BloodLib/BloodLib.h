// BloodLib - common definitions for the reconstructed PlatformBinary server framework.
//
// AchievementDaemon flavour: LogWrite has only (level, file, line, format, args...). The tag is
// baked into each format string and there is no __FUNCTION__ argument. SOURCE_FILE preserves the
// original relative "src\\<File.cpp" literals.
#pragma once

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mstcpip.h>

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>

#include "MemoryManager.h"

namespace blood {

class TrustedPacketBufferHandler;
// The network/port/session layer refers to the per-port framing prototype by this name.
using PacketBufferHandler = TrustedPacketBufferHandler;

typedef uint8_t u8;
typedef uint16_t u16;
typedef uint32_t u32;
typedef uint64_t u64;
typedef int8_t i8;
typedef int16_t i16;
typedef int32_t i32;
typedef int64_t i64;

const int kMaxPacketSize = 4000;
const int kPacketHeaderSize = 2;
const int kMaxFieldCapacity = kMaxPacketSize - kPacketHeaderSize;

class LogBuffer;
class LogMedia;
class LogFile;
class LogNetwork;
class ThreadObject;
class ThreadManager;
class TimerQueue;
class Session;
class Daemon;

} // namespace blood

#define SOURCE_FILE(name) static const wchar_t* const kSourceFile = L"src\\" L##name
#define LOG(level, line, ...) blood::LogWrite((level), kSourceFile, (line), __VA_ARGS__)
#define LOG0(level, line, ...) blood::LogWrite0((level), kSourceFile, (line), __VA_ARGS__)

#define CRASH(line, ...)                                                              \
    do {                                                                              \
        blood::LogWrite(blood::LogError, kSourceFile, (line), __VA_ARGS__);           \
        blood::Assert_FailW(kSourceFile, (line), __VA_ARGS__);                        \
        blood::Assert_FailA(kSourceFile, (line), __VA_ARGS__);                        \
    } while (0)
#define CRASH_NOLOG(line, ...)                                                       \
    do {                                                                              \
        blood::Assert_FailW(kSourceFile, (line), __VA_ARGS__);                        \
        blood::Assert_FailA(kSourceFile, (line), __VA_ARGS__);                        \
    } while (0)

namespace blood {

enum LogLevel {
    LogError = 0,
    LogDebug = 1,
    LogTrace = 2,
    LogNotice = 3,
    LogResource = 4,
    LogArchive = 5,
    LogUndef1 = 6,
    LogUndef2 = 7,
    LogUndef3 = 8,
    LogUndef4 = 9,
    LogUndef5 = 10,
    LogUndef6 = 11,
    LogBlank = 12,
    LogLevelCount = 12
};

void LogWrite(int level, const wchar_t* file, int line, const wchar_t* fmt, ...);
void LogWriteV(int level, const wchar_t* file, int line, const wchar_t* fmt, va_list args);
void LogWriteA(const wchar_t* file, int line, DWORD errorCode, const wchar_t* api,
               va_list* args);
void LogWrite0(const wchar_t* file, int line, DWORD errorCode, const wchar_t* api, ...);

void Assert_FailW(const wchar_t* file, int line, const wchar_t* fmt, ...);
void Assert_FailA(const wchar_t* file, int line, const wchar_t* fmt, ...);
const u32 kAssertExceptionCode = 0xC000FFFF;
typedef void (*AssertFailHandler)(const wchar_t* file, int line, const wchar_t* fmt,
                                  va_list args);
void SetAssertFailHandler(AssertFailHandler handler);

// RefCntObject binary prefix: vptr +0x00, tag +0x08, reference count +0x0c, history +0x10.
class RefCntObject {
public:
    virtual ~RefCntObject() {}
    virtual void Release(const wchar_t* file, int line);
    virtual void Destroy();
    virtual void ReleaseDelayed(const wchar_t* file, int line);

    u32 tag;
    volatile LONG refCount;
    void* history;

    static void operator delete(void* memory) noexcept;

protected:
    RefCntObject();
};

int RefCnt_AddRef(RefCntObject* object, const wchar_t* file, int line);
int RefCnt_Release(RefCntObject* object, const wchar_t* file, int line);
void RefCntHistory_Add(void* history, u8 opcode, i32 newRef, const wchar_t* file, int line);
void RefCntHistory_Dump(void* history, RefCntObject* object);
void RefCnt_ReleaseDelayedObject(RefCntObject* object, const wchar_t* file, int line);

u64 DaemonMillis();
const wchar_t* IpToWideString(u32 ipv4NetworkOrder);

// Session owner provides this va_list entry point. The template is the C++17 replacement for
// sub_2A5D0's two-register-varargs plus caller-stack forwarding.
void Session_SendPacket(Session* session, const char* fmt, va_list* args);

template <class... Args>
void Session_SendFmtImpl(Session* session, const char* fmt, Args... args) {
    va_list ap;
    va_start(ap, fmt);
    Session_SendPacket(session, fmt, &ap);
    va_end(ap);
}

template <class... Args>
void Session_SendFmt(Session* session, const char* fmt, Args... args) {
    Session_SendFmtImpl(session, fmt, args...);
}

} // namespace blood
