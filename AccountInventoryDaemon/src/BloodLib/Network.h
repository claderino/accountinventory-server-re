// BloodLib - IOCP network core (original: source\Network.cpp, Port.cpp, Listener.cpp,
// Connection.cpp). This build's Network and Port are PLAIN objects (no vtable, no RTTI).
#pragma once
#include "Channel.h"
#include "Session.h"
#include "Thread.h"

namespace blood {

class Port;
class Listener;
class SessionPool;

typedef bool (*FailedEventObjectHandler)(EventObject* obj, OVERLAPPED* ov);

// The listening socket of a port (original: Listener.cpp; 0x6428 bytes, 128 AcceptEx slots).
class Listener : public NetEventObject {
public:
    explicit Listener(Port* port);
    virtual ~Listener();

    bool Initialize(HANDLE iocp, u16 portNumber);      // sub_30D00
    void CloseListenSocket();                          // sub_30FA0
    bool PostAccept(int slotIndex);                    // sub_31120

    u32 EventClass() override { return 2; }
    bool HandleEvent(OVERLAPPED* ov, u32 bytes) override;
    bool OnFailedEvent(OVERLAPPED* ov) override;       // sub_31090

    Port* port_;              // +0x18, non-owning
    SOCKET listenSocket;      // +0x20

private:
    Listener(const Listener&);
    Listener& operator=(const Listener&);
    bool CompleteAccept(OVERLAPPED* ov);               // sub_31240
    bool OnAccept(OVERLAPPED* ov);                     // sub_30FE0
    struct Impl;
    Impl* impl_;
};

// IOCP worker thread (original: IoWorkerThread.cpp).
class IoWorkerThread : public ThreadObject {
public:
    IoWorkerThread();
    ~IoWorkerThread() override;
    bool Init(u32 id, FailedEventObjectHandler failedHandler);   // sub_2F4A0
    bool Run() override;                                          // sub_2F560
    void Stop() override;                                         // sub_2F540
    const wchar_t* Name() const override { return L"IoWorker"; } // sub_2F490

    u32 id_;
    FailedEventObjectHandler failedHandler_;
};

// A port with its session pool (original: Port.cpp; 0x80 bytes).
class Port {
public:
    Port();
    virtual ~Port();

    bool Initialize(u32 id, u16 listenPort, u32 capacity, HANDLE iocp,
                    SessionHandler* handler, PacketBufferHandler* prototype);
    void Clear();
    bool Start();
    bool Stop();
    bool IsOpened() const { return opened; }

    Session* CreateSession(u64* id, SOCKET accepted, in_addr ip, u16 port, u32 option);
    Session* AllocSlot(u64* outId);
    bool RemoveBySession(Session* session);
    void RemoveSession(Session* session);
    void ReleaseSessionPool();
    void RecycleSession(Session* session);        // called by Session::Destroy()
    Listener* listener() const { return listener_; }
    SessionHandler* Handler() const { return handler_; }
    PacketBufferHandler* Prototype() const { return prototype_; }
    u32 optionValue() const { return activeFlag ? 1u : 0u; }

    u32 id_;                   // +0x00
    u16 listenPort_;           // +0x04
    HANDLE iocp_;              // +0x08
    long implicitRefs;         // +0x0C implicit port references held by sessions
    bool opened;               // +0x10
    bool initialized;          // +0x11
    bool noDelayDisabled;      // +0x12 always true in this build
    u32 activeFlag;            // +0x14
    u32 capacity_;             // +0x18, always 10000 from Network::OpenPort
    bool poolInitialized;      // +0x1C
    CRITICAL_SECTION lock;     // +0x28 protects the slot array
    Session** slots;           // +0x50
    SessionPool* pool;         // +0x58
    Listener* listener_;       // +0x60
    SessionHandler* handler_;  // +0x70 embedded, not AddRef'd
    PacketBufferHandler* prototype_;  // +0x78 per-port clone

    // SAFETY: cancel and await every session's outstanding I/O before tearing the pool down.
    void BeginTeardownSessions();

private:
    Port(const Port&);
    Port& operator=(const Port&);
    u32 nextSlot_;
    u32 liveCount_;
};

// Plain singleton (original: Network.cpp; 0xE0 bytes, no vtable).
class Network {
public:
    static Network* Instance();
    static Network* Create(ThreadManager* daemonThreads);   // sub_24C60
    bool Initialize(u32 workerCount);                        // sub_24400
    void Finalize();                                         // sub_24540
    // The original hard-codes AchievementDaemon::FailedEventObjectHandler (sub_02BB0) in the
    // worker. The reconstruction keeps it injectable so the application can install it.
    void SetFailedEventHandler(FailedEventObjectHandler handler) { failedHandler_ = handler; }
    bool OpenPort(u32 id, u16 listenPort, SessionHandler* handler,
                  PacketBufferHandler* prototype);           // sub_24830
    bool StartPort(u32 id);                                  // sub_24A80
    bool StopPort(u32 id);                                   // sub_24B50
    Port* FindPort(u32 id);

    HANDLE iocp() const { return iocp_; }
    bool IsTerminated() const { return terminated_; }
    u32 WorkerCount() const { return workerCount_; }
    ThreadManager* DaemonThreads() const { return daemonThreads_; }

private:
    Network();
    virtual ~Network();
    Network(const Network&);
    Network& operator=(const Network&);
    bool InitWorkers(u32 count);
    bool ResolveWinsockExtensions();

    u32 workerCount_;          // +0x00
    IoWorkerThread* workers_;  // +0x08
    Port* ports_[24];          // +0x10..+0x88
    void* auxiliaryThreadManager_;  // +0xD0: never assigned in this build
    HANDLE iocp_;              // +0xD8
    bool terminated_;
    ThreadManager* daemonThreads_;
    FailedEventObjectHandler failedHandler_;
};

// Original: IpToWideString (24BD0). Declared in BloodLib.h.
bool WideToInetAddr(const wchar_t* ip, in_addr* out);   // NOT present in this build's Network.cpp

} // namespace blood
