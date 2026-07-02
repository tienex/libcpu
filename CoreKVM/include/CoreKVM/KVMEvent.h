/*
 * CoreKVM emitted-event queue: protocol state machines push events here during
 * pump(); the embedder pops them to drive rendering, resizing, and channels.
 */
#ifndef COREKVM_KVMEVENT_H
#define COREKVM_KVMEVENT_H

#include "CoreKVM/KVMBase.h"
#include "CoreKVM/KVMFrameBuffer.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum _KVMEventKind {
    kKVMEventDamage = 0,
    kKVMEventResize,
    kKVMEventCursor,
    kKVMEventChannelData,
    kKVMEventStatus
} KVMEventKind;

typedef struct _KVMEvent {
    KVMEventKind kind;
    KVMRect      rect;      /* damage / cursor hotspot region */
    KVMInt32     width;     /* resize */
    KVMInt32     height;    /* resize */
    KVMStatus    status;    /* status change */
    KVMUInt32    channelId; /* channel data source */
} KVMEvent;

typedef struct _KVMEventQueue *KVMEventQueueRef;

KVMEventQueueRef KVMEventQueueCreate(void);
KVMStatus        KVMEventQueuePush(KVMEventQueueRef queue, KVMEvent event);
KVMBool          KVMEventQueuePop(KVMEventQueueRef queue, KVMEvent *outEvent);
KVMIndex         KVMEventQueueGetCount(KVMEventQueueRef queue);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMEVENT_H */
