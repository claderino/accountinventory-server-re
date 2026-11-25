// BloodLib - network log medium (original: LogNetwork.cpp).
#include "LogNetwork.h"
#include "Packet.h"
#include "PacketBuffer.h"
#include "Session.h"

#include <new>

namespace blood {
namespace {

alignas(8) unsigned char g_networkStorage[sizeof(LogNetwork)];
LogNetwork* g_network = NULL;

} // namespace

static_assert(offsetof(LogNetwork, connectionLock) == 0xC0, "LogNetwork ABI");
static_assert(offsetof(LogNetwork, sessions) == 0xE8, "LogNetwork ABI");
static_assert(sizeof(LogNetwork) == 0x168, "LogNetwork ABI");

LogNetwork::LogNetwork() : queuedCount(0), reservedBC(0) {
    InitializeCriticalSectionAndSpinCount(&connectionLock, 4000);
    memset(sessions, 0, sizeof(sessions));
    g_network = this;
}

LogNetwork::~LogNetwork() {
    EnterCriticalSection(&connectionLock);
    for (int i = 0; i < 16; ++i) {
        if (sessions[i])
            RefCnt_Release(reinterpret_cast<RefCntObject*>(sessions[i]),
                           L"src\\LogNetwork.cpp", 0x37);
        sessions[i] = NULL;
    }
    LeaveCriticalSection(&connectionLock);
    DeleteCriticalSection(&connectionLock);
    if (g_network == this) g_network = NULL;
}

LogNetwork* LogNetwork_Create() {
    if (g_network) return g_network;
    g_network = ::new (static_cast<void*>(g_networkStorage)) LogNetwork();
    return g_network;
}

void LogNetwork_ReleaseMedia() {
    if (!g_network) return;
    g_network->~LogNetwork();
    g_network = NULL;
}

bool LogNetwork::Attach(Session* session) {
    if (!session) return false;
    EnterCriticalSection(&connectionLock);
    int slot = -1;
    for (int i = 0; i < 16; ++i) {
        if (sessions[i] == session) { slot = -2; break; }
        if (!sessions[i] && slot < 0) slot = i;
    }
    if (slot >= 0) {
        sessions[slot] = session;
        RefCnt_AddRef(reinterpret_cast<RefCntObject*>(session), L"src\\LogNetwork.cpp", 0x43);
    }
    LeaveCriticalSection(&connectionLock);
    return slot >= 0;
}

void LogNetwork::Detach(Session* session) {
    EnterCriticalSection(&connectionLock);
    for (int i = 0; i < 16; ++i) {
        if (sessions[i] != session) continue;
        sessions[i] = NULL;
        RefCnt_Release(reinterpret_cast<RefCntObject*>(session),
                       L"src\\LogNetwork.cpp", 0x4D);
        break;
    }
    LeaveCriticalSection(&connectionLock);
}

void LogNetwork::WriteLog(LogBuffer* head) {
    static const wchar_t levelCharacters[] = L"EDTNRA12345";
    for (LogBuffer* record = head; record; record = record->next[index]) {
        if (!record->enabled) continue;
        PacketBuffer* packet = PacketBuffer::Acquire();
        if (!packet) continue;
        packet->size = 2;
        u32 header = 2; // u16 length placeholder=2, u16 command=0
        memcpy(packet->fields, &header, sizeof(header));
        wchar_t character = record->level >= 0 && record->level < 11 ?
                            levelCharacters[record->level] : L'?';
        wchar_t* text = reinterpret_cast<wchar_t*>(packet->fields + 4);
        int capacity = static_cast<int>((PacketBuffer::kCapacity - 4) / sizeof(wchar_t));
        swprintf_s(text, capacity, L"%c> %s", character, record->message);
        packet->size = static_cast<u16>(4 + wcslen(text) * sizeof(wchar_t));
        EnterCriticalSection(&connectionLock);
        for (int i = 0; i < 16; ++i)
            if (sessions[i]) Session_SendLogPacket(sessions[i], packet);
        LeaveCriticalSection(&connectionLock);
        packet->Release(L"src\\LogNetwork.cpp", 0x87);
    }
}

bool Session_SendLogPacket(Session* session, PacketBuffer* packet) {
    if (!session || !packet) return false;
    Packet holder;
    Packet_Init(&holder);
    holder.splitter = NULL;
    holder.current = packet;
    holder.first = packet;
    holder.last = packet;
    Session::SendPacketPtr(session, &holder);
    Packet_Release(&holder);
    return true;
}

} // namespace blood
