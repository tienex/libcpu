/*
 * CoreKVM emitted-event queue implementation: a growable ring buffer.
 */
#include "CoreKVM/KVMEvent.h"
#include "KVMRuntime.h"

#include <stdlib.h>

static const KVMTypeID kKVMEventQueueTypeID = 0x45564E54UL; /* 'EVNT' */

#define KVM_EVENT_QUEUE_INITIAL_CAPACITY 16

struct _KVMEventQueue {
    KVMObjectHeader base;
    KVMEvent       *events;
    KVMIndex        capacity;
    KVMIndex        head;
    KVMIndex        count;
};

static void
KVMEventQueueDealloc(void *object)
{
    struct _KVMEventQueue *queue = (struct _KVMEventQueue *)object;

    if (queue->events != NULL) {
        free(queue->events);
        queue->events = NULL;
    }
}

KVMEventQueueRef
KVMEventQueueCreate(void)
{
    struct _KVMEventQueue *queue;

    queue = (struct _KVMEventQueue *)KVMRuntimeCreate(
        kKVMEventQueueTypeID, (KVMIndex)sizeof(*queue), KVMEventQueueDealloc);
    if (queue == NULL) {
        return NULL;
    }
    queue->events =
        (KVMEvent *)malloc(sizeof(KVMEvent) * KVM_EVENT_QUEUE_INITIAL_CAPACITY);
    if (queue->events == NULL) {
        KVMRelease((KVMRef)queue);
        return NULL;
    }
    queue->capacity = KVM_EVENT_QUEUE_INITIAL_CAPACITY;
    queue->head = 0;
    queue->count = 0;
    return queue;
}

static KVMStatus
KVMEventQueueGrow(struct _KVMEventQueue *queue)
{
    KVMIndex newCap;
    KVMEvent *grown;
    KVMIndex i;

    if (queue->capacity > KVM_INDEX_MAX / 2) {
        return kKVMErrorNoMemory;
    }
    newCap = queue->capacity * 2;
    if ((size_t)newCap > (size_t)-1 / sizeof(KVMEvent)) {
        return kKVMErrorNoMemory;
    }
    grown = (KVMEvent *)malloc(sizeof(KVMEvent) * (size_t)newCap);
    if (grown == NULL) {
        return kKVMErrorNoMemory;
    }
    /* Copy in logical order so head resets to 0. */
    for (i = 0; i < queue->count; i++) {
        grown[i] = queue->events[(queue->head + i) % queue->capacity];
    }
    free(queue->events);
    queue->events = grown;
    queue->capacity = newCap;
    queue->head = 0;
    return kKVMSuccess;
}

KVMStatus
KVMEventQueuePush(KVMEventQueueRef queue, KVMEvent event)
{
    KVMIndex tail;

    if (queue == NULL) {
        return kKVMErrorInvalidArgument;
    }
    if (queue->count == queue->capacity) {
        KVMStatus status = KVMEventQueueGrow(queue);
        if (status != kKVMSuccess) {
            return status;
        }
    }
    tail = (queue->head + queue->count) % queue->capacity;
    queue->events[tail] = event;
    queue->count++;
    return kKVMSuccess;
}

KVMBool
KVMEventQueuePop(KVMEventQueueRef queue, KVMEvent *outEvent)
{
    if (queue == NULL || outEvent == NULL || queue->count == 0) {
        return KVM_FALSE;
    }
    *outEvent = queue->events[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    queue->count--;
    return KVM_TRUE;
}

KVMIndex
KVMEventQueueGetCount(KVMEventQueueRef queue)
{
    return (queue != NULL) ? queue->count : 0;
}
