// BloodLib - outgoing send queue (original: source\SessionBuffer.cpp).
#include "NetworkInternal.h"
#include "Packet.h"

SOURCE_FILE("SessionBuffer.cpp");

namespace blood {

void SendBufferDescGrow(SendBufferDesc* d) {
    u32 oldCap = d->capacity;
    u32 newCap = oldCap < 2 ? 2 : oldCap * 2;
    WSABUF* buffers = new WSABUF[newCap];
    PacketBuffer** packets = new PacketBuffer*[newCap];
    memset(buffers, 0, sizeof(WSABUF) * newCap);
    memset(packets, 0, sizeof(PacketBuffer*) * newCap);
    if (d->buffers && d->count) memcpy(buffers, d->buffers, sizeof(WSABUF) * d->count);
    if (d->packets && d->count) memcpy(packets, d->packets, sizeof(PacketBuffer*) * d->count);
    delete[] d->buffers;
    delete[] d->packets;
    d->buffers = buffers;
    d->packets = packets;
    d->capacity = newCap;
    d->allocated = 1;
}

void SendBufferDescReleasePackets(SendBufferDesc* d) {
    for (u32 i = 0; i < d->count; ++i) {
        if (d->packets[i] != NULL) {
            d->packets[i]->Release(kSourceFile, 0);
            d->packets[i] = NULL;
        }
        memset(&d->buffers[i], 0, sizeof(WSABUF));
    }
    d->count = 0;
}

bool SessionSendBufferInitialize(SendContext* ctx, unsigned char* backingA, unsigned char* backingB) {
    (void)backingA;
    (void)backingB;
    for (int i = 0; i < 2; ++i) {
        SendBufferDesc& d = ctx->descs[i];
        d.allocated = 1;
        d.capacity = 42;
        d.count = 0;
        d.buffers = new WSABUF[42];
        d.packets = new PacketBuffer*[42];
        memset(d.buffers, 0, sizeof(WSABUF) * 42);
        memset(d.packets, 0, sizeof(PacketBuffer*) * 42);
        d.next = &ctx->descs[(i + 1) & 1];
    }
    ctx->queued = NULL;
    ctx->sending = NULL;
    ctx->highWater = 0;
    return true;
}

void SessionSendBufferClear(SendContext* ctx) {
    for (int i = 0; i < 2; ++i) {
        SendBufferDesc& d = ctx->descs[i];
        SendBufferDescReleasePackets(&d);
        delete[] d.buffers;
        delete[] d.packets;
        d.buffers = NULL;
        d.packets = NULL;
        d.capacity = 0;
        d.allocated = 0;
        d.next = NULL;
    }
    ctx->queued = NULL;
    ctx->sending = NULL;
}

SendBufferDesc* SessionSendBufferAdd(Session* session, Packet* packet) {
    (void)session;
    if (packet == NULL || packet->first == NULL) {
        CRASH(0x5E, L"== CRASH == [session], send parameter is invalid, count %d, pbufferList: %p",
              1, packet);
        return NULL;
    }
    SendContext* ctx = session->send;
    SendBufferDesc* d = ctx->queued != NULL ? ctx->queued : &ctx->descs[0];
    if (d->buffers == NULL) SessionSendBufferInitialize(ctx, NULL, NULL);
    for (PacketBuffer* b = packet->first; b != NULL; b = b->next) {
        if (d->count + 1 >= d->capacity) SendBufferDescGrow(d);
        b->AddRef();
        d->buffers[d->count].len = b->size;
        d->buffers[d->count].buf = reinterpret_cast<char*>(&b->size);
        d->packets[d->count] = b;
        ++d->count;
        if (d->count > ctx->highWater) ctx->highWater = d->count;
    }
    if (ctx->queued == NULL && d->count != 0) ctx->queued = d;
    return d->count != 0 ? d : NULL;
}

SendBufferDesc* SessionSendBufferRemoveSent(Session* session) {
    SendContext* ctx = session->send;
    if (ctx->queued == NULL) {
        CRASH(0xA3,
              L"== CRASH == [session], cannot remove send buffer; curSendingList is NULL, "
              L"session-id:%I64X, sent-size:%u, context-buffer:0x%p, context-buffer-count:%d",
              session->id, 0u, (void*)NULL, 0);
        return NULL;
    }
    SendBufferDesc* d = ctx->queued;
    SendBufferDescReleasePackets(d);
    SendBufferDesc* next = d->next;
    if (next == NULL || next->count == 0) {
        ctx->queued = NULL;
        return NULL;
    }
    ctx->queued = next;
    return next;
}

} // namespace blood
