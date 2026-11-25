// BloodLib - session channels (original: source\Channel.cpp).
#include "Channel.h"
#include "Session.h"
#include "NetworkInternal.h"
#include "Log.h"

SOURCE_FILE("Channel.cpp");

namespace blood {

Channel::Channel()
    : session(NULL), sessionInvalid(false), useDelayedRelease_(true) {
    InitializeCriticalSectionAndSpinCount(&lock_, 4000);
}

Channel::~Channel() {
    EnterCriticalSection(&lock_);
    Session* old = session;
    session = NULL;
    sessionInvalid = true;
    bool clearBackPointer = old != NULL && old->channel == this;
    if (clearBackPointer) {
        // SAFETY: clear the back-pointer BEFORE releasing the channel reference. The original
        // released with the pointer still installed, allowing a re-entrant double release.
        old->channel = NULL;
    }
    LeaveCriticalSection(&lock_);
    if (old != NULL) {
        if (clearBackPointer) Release();   // the session's reference to this channel
        old->Release();                    // the channel's reference to the session
    }
    DeleteCriticalSection(&lock_);
}

void Channel::Bind(Session* s) {
    if (s == NULL) {
        CRASH(0x35, L"== CRASH == ASSERT CRASH");
        return;
    }
    EnterCriticalSection(&lock_);
    if (session != NULL) {
        LeaveCriticalSection(&lock_);
        CRASH(0x35, L"== CRASH == ASSERT CRASH");
        return;
    }
    s->AddRef();
    session = s;
    sessionInvalid = false;
    if (s->channel == NULL) {
        s->channel = this;
        AddRef();
    }
    LeaveCriticalSection(&lock_);
}

void Channel::Detach(Session* s, bool immediate) {
    if (s != NULL && s != session) {
        LOG(0, 0x44,
            L"Channel(%p, %I64X), session(%p, %I64X), removing session(%p, %I64X)",
            this, GetChannelValue(), s, s->id, session, session ? session->id : (u64)0);
    }
    if (!immediate && useDelayedRelease_ && s != NULL) {
        // The original defers the unbind so the session is not touched from inside a completion.
        sessionInvalid = true;
        s->unbound = true;
        TheDelayedRelease().Enqueue(DaemonMillis(), this, s);
        return;
    }
    EnterCriticalSection(&lock_);
    Session* old = session;
    session = NULL;
    sessionInvalid = true;
    if (old != NULL) {
        if (old->channel == this) {
            old->channel = NULL;
            // Release the session's channel reference outside the lock: it may destroy us.
            LeaveCriticalSection(&lock_);
            Release();
            EnterCriticalSection(&lock_);
        }
        LeaveCriticalSection(&lock_);
        old->Release();
        return;
    }
    LeaveCriticalSection(&lock_);
}

// Fixed callback expected by DelayedRelease.cpp (sub_25CC0).
void Channel_UnbindIfEqual(RefCntObject* channel, RefCntObject* s) {
    Channel::UnbindIfEqual(static_cast<Channel*>(channel), static_cast<Session*>(s));
}

void Channel::UnbindIfEqual(Channel* ch, Session* s) {
    if (ch == NULL) return;
    EnterCriticalSection(&ch->lock_);
    Session* bound = ch->session;
    // SAFETY: the original's 25CC0 ignores its Session argument entirely, so a stale delayed
    // node could unbind a *new* session after a rebind. Compare identities first.
    if (s != NULL && bound != s) {
        LeaveCriticalSection(&ch->lock_);
        return;
    }
    ch->session = NULL;
    ch->sessionInvalid = true;
    if (bound != NULL) {
        if (bound->channel == ch) {
            bound->channel = NULL;
            LeaveCriticalSection(&ch->lock_);
            ch->Release();
            EnterCriticalSection(&ch->lock_);
        }
    }
    LeaveCriticalSection(&ch->lock_);
    if (bound != NULL) bound->Release();
}

Session* Channel::GetSession() {
    EnterCriticalSection(&lock_);
    Session* s = session;
    bool valid = !sessionInvalid && s != NULL;
    if (valid) s->AddRef();
    LeaveCriticalSection(&lock_);
    return valid ? s : NULL;
}

} // namespace blood
