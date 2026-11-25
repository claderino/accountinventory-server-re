// BloodLib - channels and per-port session handlers (original: Channel.h / Channel.cpp).
//
// A Channel is the object a Session is attached to once the peer authenticated. Unlike the
// ranking reconstruction this build's base Channel has ONLY Bind / Detach / GetSession and an
// abstract slot 7 (the application's channel value). There is no Channel::SendFmt, SendPacketPtr
// or CloseSession in this image; application channels implement their own send helpers.
//
// The per-port dispatcher is a SessionHandler (vtable 0x74DD8):
//   +0x18 OnAccept(session)  +0x20 OnConnect(session)
//   +0x28 OnClose(session, bool)  +0x30 PacketHandler(session, packet)
#pragma once
#include "BloodLib.h"
#include "Thread.h"

namespace blood {

class Session;
struct Packet;

// ---------------------------------------------------------------------------------------------
// Compatibility mixins for this build's RefCntObject ABI. The shared header declares
// RefCnt_AddRef/Release(obj,file,line) and a virtual Release(file,line) that deletes the object;
// the network files use the ranking-style AddRef()/Release() spelling. These bridge the two and
// let pooled classes intercept the last release (OnLastRelease returns true when the object
// disposed of/recycled itself).
// ---------------------------------------------------------------------------------------------
#ifndef BLOOD_NET_REF_MIXIN_DEFINED
#define BLOOD_NET_REF_MIXIN_DEFINED
class NetRefObject : public RefCntObject {
public:
    void AddRef() { RefCnt_AddRef(this, L"src\\Network.cpp", 0); }
    void Release() { RefCnt_Release(this, L"src\\Network.cpp", 0); }
    using RefCntObject::Release;
    void Release(const wchar_t* file, int line) override {
        if (refCount == 0 && OnLastRelease()) return;
        RefCntObject::Release(file, line);
    }
protected:
    NetRefObject() {}
    virtual bool OnLastRelease() { return false; }
};

class NetEventObject : public EventObject {
public:
    void AddRef() { RefCnt_AddRef(this, L"src\\Network.cpp", 0); }
    void Release() { RefCnt_Release(this, L"src\\Network.cpp", 0); }
    using EventObject::Release;
    void Release(const wchar_t* file, int line) override {
        if (refCount == 0 && OnLastRelease()) return;
        EventObject::Release(file, line);
    }
protected:
    NetEventObject() {}
    virtual bool OnLastRelease() { return false; }
};
#endif // BLOOD_NET_REF_MIXIN_DEFINED

class Channel : public NetRefObject {
public:
    Channel();
    virtual ~Channel();

    // Attaches `session` to this channel (vtable +0x20). Asserts no session is bound.
    void Bind(Session* session);
    // Removes the binding (vtable +0x28). With immediate == false the real unbind is deferred
    // through DelayedRelease so the Session is not touched inside its completion callback.
    void Detach(Session* session, bool immediate);
    // Returns the bound session with an added reference, or NULL when unbound (vtable +0x30).
    Session* GetSession();

    // vtable +0x38: game channel -> cluster id, manage channel -> session id.
    virtual u64 GetChannelValue() = 0;

    // Delayed-release callback (sub_25CC0). Identity-checked; SAFETY fix vs. the original.
    static void UnbindIfEqual(Channel* channel, Session* session);

    Session* session;          // +0x18 bound session (one reference held while bound)
    bool sessionInvalid;       // +0x21 GetSession returns NULL while set
    bool useDelayedRelease_;   // +0x28

private:
    Channel(const Channel&);
    Channel& operator=(const Channel&);
    // SAFETY: the original has no lock here, which makes rebind vs. delayed unbind racy.
    CRITICAL_SECTION lock_;
};

// bool Handler(Session*, Packet*) - one entry of a port's handler table.
typedef bool (*PacketHandlerFn)(Session* session, Packet* packet);

class SessionHandler : public NetRefObject {
public:
    SessionHandler() {}
    virtual ~SessionHandler() {}
    virtual bool OnAccept(Session* session) { (void)session; return true; }
    virtual bool OnConnect(Session* session) { (void)session; return false; }
    virtual void OnClose(Session* session, bool closeAll = false) { (void)session; (void)closeAll; }
    virtual bool PacketHandler(Session* session, Packet* packet) = 0;
};

// Compatibility alias for code written against the ranking headers.
typedef SessionHandler ChannelManager;

} // namespace blood
