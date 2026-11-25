// BloodLib - raw TCP alert sender (original: AlertSender.cpp).
#include "AlertSender.h"
#include "Daemon.h"

#include <string.h>
#include <new>

#pragma comment(lib, "ws2_32.lib")

namespace blood {
namespace {

#pragma pack(push, 1)
struct AlertRecord {
    AlertRecord* next;
    i32 start;
    i32 end;
    u8 bytes[1];
};
#pragma pack(pop)

const int kMaxQueued = 1000;
const wchar_t* const kPrefix1 = L"ACHIEVEMENTD";
const wchar_t* const kPrefix2 = L"0.0.210.567";

alignas(8) unsigned char g_senderStorage[sizeof(AlertSender)];
AlertSender* g_sender = NULL;
bool g_enabled = false;

void Put16(u8*& output, u16 value) { memcpy(output, &value, 2); output += 2; }
void Put32(u8*& output, u32 value) { memcpy(output, &value, 4); output += 4; }
void PutText(u8*& output, const wchar_t* text, size_t bytes) {
    if (bytes) memcpy(output, text, bytes);
    output += bytes;
}

const wchar_t* BaseName(const wchar_t* file) {
    const wchar_t* base = file ? file : L"";
    for (const wchar_t* p = base; *p; ++p)
        if (*p == L'\\' || *p == L'/') base = p + 1;
    return base;
}

void FreeRecord(AlertRecord* record) {
    if (record) Memory_Free(record);
}

void DrainQueue(AlertSender* sender) {
    EnterCriticalSection(&sender->queueLock);
    AlertRecord* record = static_cast<AlertRecord*>(sender->queueHead);
    sender->queueHead = sender->queueTail = NULL;
    sender->queueCount = 0;
    LeaveCriticalSection(&sender->queueLock);
    while (record) {
        AlertRecord* next = record->next;
        FreeRecord(record);
        record = next;
    }
}

void AssertHandler(const wchar_t* file, int line, const wchar_t* format, va_list arguments) {
    if (g_sender) g_sender->EnqueueAssert(file, line, format, &arguments);
}

} // namespace

static_assert(offsetof(AlertRecord, bytes) == 0x10, "AlertRecord ABI");
static_assert(offsetof(AlertSender, socket) == 0x30, "AlertSender ABI");
static_assert(offsetof(AlertSender, socketEvent) == 0x38, "AlertSender ABI");
static_assert(offsetof(AlertSender, queueEvent) == 0x40, "AlertSender ABI");
static_assert(offsetof(AlertSender, address) == 0x56, "AlertSender ABI");
static_assert(offsetof(AlertSender, queueLock) == 0x170, "AlertSender ABI");
static_assert(offsetof(AlertSender, queueHead) == 0x198, "AlertSender ABI");
static_assert(offsetof(AlertSender, queueCount) == 0x1A8, "AlertSender ABI");
static_assert(sizeof(AlertSender) == 0x1B0, "AlertSender ABI");

AlertSender::AlertSender()
    : socket(INVALID_SOCKET), socketEvent(NULL), queueEvent(NULL), threadName(L"AlertSender"),
      maxQueue(kMaxQueued), reserved54(0), flag6C(0), queueHead(NULL), queueTail(NULL),
      queueCount(0) {
    memset(address, 0, sizeof(address));
    memset(reserved6D, 0, sizeof(reserved6D));
    InitializeCriticalSectionAndSpinCount(&queueLock, 4000);
}

bool AlertSender::OpenSocket() {
    if (socket != INVALID_SOCKET) closesocket(socket);
    socket = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket == INVALID_SOCKET) return false;
    if (!socketEvent) socketEvent = WSACreateEvent();
    if (!socketEvent) return false;
    return WSAEventSelect(socket, socketEvent, FD_READ | FD_WRITE | FD_CLOSE | FD_CONNECT) == 0;
}

bool AlertSender::Initialize(const wchar_t* destination) {
    if (!g_sender) return false;
    INT addressLength = sizeof(address);
    memset(address, 0, sizeof(address));
    if (!destination || WSAStringToAddressW(const_cast<wchar_t*>(destination), AF_INET, NULL,
                                            reinterpret_cast<sockaddr*>(&Address()),
                                            &addressLength) == SOCKET_ERROR)
        return false;
    if (!queueEvent) queueEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (!queueEvent || !OpenSocket()) return false;
    if (connect(socket, reinterpret_cast<sockaddr*>(&Address()), sizeof(Address())) == SOCKET_ERROR &&
        WSAGetLastError() != WSAEWOULDBLOCK)
        return false;
    Daemon* daemon = Daemon::Instance();
    if (daemon && daemon->Threads() && !running)
        daemon->Threads()->Start(this, NULL);
    g_enabled = true;
    return true;
}

void AlertSender::EnqueueAssert(const wchar_t* file, int line, const wchar_t* format,
                                va_list* arguments) {
    if (!g_enabled || !arguments) return;
    wchar_t message[2000];
    vswprintf_s(message, 2000, format ? format : L"", *arguments);
    wchar_t fileName[MAX_PATH];
    wchar_t extension[MAX_PATH];
    _wsplitpath_s(file ? file : L"", NULL, 0, NULL, 0, fileName, MAX_PATH,
                  extension, MAX_PATH);
    wchar_t sourceName[MAX_PATH * 2];
    swprintf_s(sourceName, MAX_PATH * 2, L"%s%s", fileName, extension);
    wchar_t lineText[64];
    swprintf_s(lineText, 64, L"%d,", line);

    u16 prefix1Bytes = static_cast<u16>(wcslen(kPrefix1) * sizeof(wchar_t));
    u16 prefix2Bytes = static_cast<u16>(wcslen(kPrefix2) * sizeof(wchar_t));
    u16 fileBytes = static_cast<u16>(wcslen(sourceName) * sizeof(wchar_t));
    u16 messageBytes = static_cast<u16>(wcslen(message) * sizeof(wchar_t));
    u16 lineBytes = static_cast<u16>(wcslen(lineText) * sizeof(wchar_t));
    // Original formula omits lineTextBytes even though the record contains that field.
    u16 total = static_cast<u16>(prefix1Bytes + prefix2Bytes + fileBytes + messageBytes + 0x14);
    u32 actual = sizeof(AlertRecord) - 1 + total + lineBytes;
    AlertRecord* record = static_cast<AlertRecord*>(Memory_Allocate(actual,
                                                                     L"src\\AlertSender.cpp", 0x18F));
    if (!record) return;
    record->next = NULL;
    record->start = 0;
    u8* output = record->bytes;
    Put16(output, total);
    Put16(output, 2);
    Put16(output, prefix1Bytes); PutText(output, kPrefix1, prefix1Bytes);
    Put32(output, 0);
    Put16(output, prefix2Bytes); PutText(output, kPrefix2, prefix2Bytes);
    Put16(output, fileBytes); PutText(output, sourceName, fileBytes);
    Put16(output, lineBytes); PutText(output, lineText, lineBytes);
    Put32(output, static_cast<u32>(line));
    Put16(output, messageBytes); PutText(output, message, messageBytes);
    record->end = static_cast<i32>(output - record->bytes);

    EnterCriticalSection(&queueLock);
    if (queueCount < static_cast<LONG>(maxQueue)) {
        if (queueTail) static_cast<AlertRecord*>(queueTail)->next = record;
        else queueHead = record;
        queueTail = record;
        InterlockedIncrement(&queueCount);
        if (queueEvent) SetEvent(queueEvent);
    } else {
        FreeRecord(record);
    }
    LeaveCriticalSection(&queueLock);
}

bool AlertSender::Run() {
    HANDLE handles[2] = { socketEvent, queueEvent };
    while (running) {
        DWORD count = handles[1] ? 2 : 1;
        DWORD wait = WaitForMultipleObjects(count, handles, FALSE, 1000);
        if (wait == WAIT_FAILED) break;
        if (wait == WAIT_OBJECT_0 && socketEvent) {
            WSANETWORKEVENTS events;
            memset(&events, 0, sizeof(events));
            if (WSAEnumNetworkEvents(socket, socketEvent, &events) == SOCKET_ERROR) break;
            if (events.lNetworkEvents & (FD_CLOSE | FD_CONNECT)) {
                if (events.iErrorCode[FD_CONNECT_BIT] || events.iErrorCode[FD_CLOSE_BIT]) {
                    Sleep(3000);
                    if (!OpenSocket()) continue;
                    connect(socket, reinterpret_cast<sockaddr*>(&Address()), sizeof(Address()));
                } else if (events.lNetworkEvents & (FD_READ | FD_WRITE | FD_CONNECT)) {
                    ResetEvent(queueEvent);
                    EnterCriticalSection(&queueLock);
                    AlertRecord* head = static_cast<AlertRecord*>(queueHead);
                    queueHead = queueTail = NULL;
                    queueCount = 0;
                    LeaveCriticalSection(&queueLock);
                    while (head) {
                        AlertRecord* next = head->next;
                        WSABUF buffer;
                        buffer.buf = reinterpret_cast<char*>(head->bytes + head->start);
                        buffer.len = static_cast<ULONG>(head->end - head->start);
                        DWORD sent = 0;
                        if (WSASend(socket, &buffer, 1, &sent, 0, NULL, NULL) == SOCKET_ERROR) {
                            if (WSAGetLastError() == WSAEWOULDBLOCK) continue;
                            FreeRecord(head);
                            head = next;
                            continue;
                        }
                        head->start += static_cast<i32>(sent);
                        if (head->start < head->end) continue;
                        FreeRecord(head);
                        head = next;
                    }
                }
            }
        } else if (wait == WAIT_OBJECT_0 + 1 && queueEvent) {
            ResetEvent(queueEvent);
            EnterCriticalSection(&queueLock);
            AlertRecord* head = static_cast<AlertRecord*>(queueHead);
            queueHead = queueTail = NULL;
            queueCount = 0;
            LeaveCriticalSection(&queueLock);
            while (head) {
                AlertRecord* next = head->next;
                WSABUF buffer;
                buffer.buf = reinterpret_cast<char*>(head->bytes + head->start);
                buffer.len = static_cast<ULONG>(head->end - head->start);
                DWORD sent = 0;
                if (WSASend(socket, &buffer, 1, &sent, 0, NULL, NULL) == 0) {
                    head->start += static_cast<i32>(sent);
                    if (head->start >= head->end) {
                        FreeRecord(head);
                        head = next;
                        continue;
                    }
                } else if (WSAGetLastError() != WSAEWOULDBLOCK) {
                    FreeRecord(head);
                    head = next;
                    continue;
                }
                // Preserve an unsent record for the next writable notification.
                EnterCriticalSection(&queueLock);
                head->next = static_cast<AlertRecord*>(queueHead);
                queueHead = head;
                if (!queueTail) queueTail = head;
                InterlockedIncrement(&queueCount);
                LeaveCriticalSection(&queueLock);
                head = next;
            }
        }
    }
    return true;
}

void AlertSender::Stop() { running = false; }

void AlertSender::Shutdown() {
    running = false;
    Daemon* daemon = Daemon::Instance();
    if (daemon && daemon->Threads() && threadId)
        daemon->Threads()->Stop(this, 2000, true, true);
    if (socket != INVALID_SOCKET) closesocket(socket);
    socket = INVALID_SOCKET;
    if (socketEvent) WSACloseEvent(socketEvent);
    socketEvent = NULL;
    if (queueEvent) CloseHandle(queueEvent);
    queueEvent = NULL;
    memset(address, 0, sizeof(address));
    DrainQueue(this);
    g_enabled = false;
    SetAssertFailHandler(NULL);
}

void AlertSender_Startup() {
    if (g_sender) return;
    g_sender = new (g_senderStorage) AlertSender();
    g_sender->queueEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    SetAssertFailHandler(&AssertHandler);
}

void AlertSender_Shutdown() {
    if (!g_sender) return;
    g_sender->Shutdown();
    g_sender->~AlertSender();
    g_sender = NULL;
}

bool AlertSender_Initialize(const wchar_t* address) {
    if (!g_sender) AlertSender_Startup();
    return g_sender && g_sender->Initialize(address);
}

} // namespace blood
