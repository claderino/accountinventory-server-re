// BloodLib - logging interface for AchievementDaemon (original: Log.h / LogBuffer / LogMedia).
#pragma once
#include "Thread.h"
#include <time.h>

namespace blood {

const wchar_t* LogLevelName(int level);

// Binary interface embedded at LogBuffer +0x18 and consumed by bsConsole_sv64.dll.
class ILogBufferRef {
public:
    virtual int AddRef() = 0;                         // +0x00
    virtual void Release() = 0;                       // +0x08
    virtual int GetLevel() = 0;                       // +0x10
    virtual bool IsEnabled() = 0;                     // +0x18
    virtual bool FormatTimestamp(wchar_t* buffer, int capacity) = 0; // +0x20
protected:
    ~ILogBufferRef() {}
};

// Exact 0x1000-byte pooled record. The first 0x8E bytes are the console DLL's binary interface.
class LogBuffer {
public:
    LogBuffer();
    ~LogBuffer();

    void AddRef();
    void Release();
    void SetHeader(int level, const wchar_t* file, int line);
    bool FormatTimestamp(wchar_t* buffer, int capacity) const;

    void* vt;                    // +0x00 object vtable
    i32 sizeTag;                 // +0x08 0x10A
    volatile LONG refCount;      // +0x0C
    u8 reserved10[8];            // +0x10
    void* refVt;                 // +0x18 embedded ILogBufferRef vtable
    LogBuffer* next[9];          // +0x20..0x67
    u8 reserved68[8];            // +0x68..0x6F (SYSTEMTIME low qword)
    u8 reserved70[8];            // +0x70..0x77 (SYSTEMTIME high qword)
    int level;                   // +0x78
    const wchar_t* file;         // +0x80
    int line;                    // +0x88
    bool enabled;                // +0x8C
    u8 reserved8D;               // +0x8D
    wchar_t message[0x7B8];      // +0x8E

private:
    LogBuffer(const LogBuffer&);
    LogBuffer& operator=(const LogBuffer&);
};

class LogMedia {
public:
    LogMedia();
    virtual ~LogMedia();

    virtual bool Supports();                                  // +0x08
    virtual void Flush();                                     // +0x10
    virtual const wchar_t* GetLastFileName() { return L""; }   // +0x18
    virtual int GetType() const { return 2; }                  // +0x20
    virtual void WriteLog(LogBuffer* head) = 0;                // +0x28

    bool AddRecord(LogBuffer* record);
    void FlushQueue();
    void SetLevelEnabled(int level, bool value);

    int index;                    // +0x08 (-1 until registered)
    LogMedia* next[12];           // +0x10..0x6F per-level registration links
    u8 levelEnabled[12];          // +0x70..0x7B
    u8 reserved7C[4];             // +0x7C
    CRITICAL_SECTION appendLock;  // +0x80
    LogBuffer* head;              // +0xA8
    LogBuffer* tail;              // +0xB0
};

class Log : public ThreadObject {
public:
    Log();
    ~Log() override;

    bool Init();
    bool AddMedia(LogMedia* media);
    void FlushAndStop();
    void SignalFlush();
    void Flush();
    void Dispatch(LogBuffer* record);

    const wchar_t* Name() const override { return L"Log"; }
    bool Run() override;
    void Stop() override { FlushAndStop(); }

    bool enabled;                // +0x30
    bool flushRequested;         // +0x31
    u8 reserved32[6];            // +0x32..0x37
    HANDLE event;                // +0x38
    LogMedia* media[9];          // +0x40..0x87
    u32 mediaCount;              // +0x88
    u8 reserved8C[4];            // +0x8C..0x8F
    LogMedia* levelHeads[12];    // +0x90..0xEF
    volatile LONG levelCount[12];// +0xF0..0x11F
};

bool Log_Create();
Log* TheLog();
LogBuffer* LogBuffer_Acquire();
void LogBuffer_InitTLS();

class LogFile : public LogMedia {
public:
    LogFile(const wchar_t* logPath, DWORD codePage);
    virtual ~LogFile();

    bool Initialize(const wchar_t* logPath);
    void Flush() override;
    const wchar_t* GetLastFileName() override { return lastWritten; }
    void WriteLog(LogBuffer* head) override;

    u32 requestedSize;           // +0xB8
    u32 capacity;                // +0xBC
    u32 used;                    // +0xC0
    u8* buffer;                  // +0xC8
    wchar_t* baseName;           // +0xD0
    DWORD codePage;              // +0xD8
    u32 rotationMode;            // +0xDC
    u32 instance;                // +0xE0
    i64 nextRotation;            // +0xE8
    u32 interval;                // +0xF0
    u32 reservedF4;              // +0xF4
    wchar_t lastName[0x104];     // +0xF8
    wchar_t lastWritten[0x104];  // +0x300

private:
    LogFile(const LogFile&);
    LogFile& operator=(const LogFile&);
    void MakeName(const struct tm* time, wchar_t* output);
    void FormatLine(LogBuffer* record);
    void AppendBytes(const void* data, size_t bytes);
    void AppendWide(const wchar_t* text);
    void EnsureCapacity(size_t bytes);
};

} // namespace blood
