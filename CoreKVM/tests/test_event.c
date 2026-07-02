/*
 * Unit tests for the emitted-event queue (FIFO order).
 */
#include "CoreKVM/KVMEvent.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static void
TestFifoOrder(void)
{
    KVMEventQueueRef queue = KVMEventQueueCreate();
    KVMEvent a;
    KVMEvent b;
    KVMEvent out;

    KVM_CHECK(queue != NULL);

    a.kind = kKVMEventResize;
    a.width = 640;
    a.height = 480;
    b.kind = kKVMEventStatus;
    b.status = kKVMErrorClosed;

    KVM_CHECK(KVMEventQueuePush(queue, a) == kKVMSuccess);
    KVM_CHECK(KVMEventQueuePush(queue, b) == kKVMSuccess);
    KVM_CHECK(KVMEventQueueGetCount(queue) == 2);

    KVM_CHECK(KVMEventQueuePop(queue, &out) == KVM_TRUE);
    KVM_CHECK(out.kind == kKVMEventResize);
    KVM_CHECK(out.width == 640 && out.height == 480);

    KVM_CHECK(KVMEventQueuePop(queue, &out) == KVM_TRUE);
    KVM_CHECK(out.kind == kKVMEventStatus);
    KVM_CHECK(out.status == kKVMErrorClosed);

    KVM_CHECK(KVMEventQueuePop(queue, &out) == KVM_FALSE);
    KVMRelease(queue);
}

static void
TestGrowthPreservesFifo(void)
{
    KVMEventQueueRef queue = KVMEventQueueCreate();
    KVMEvent e;
    KVMInt32 i;

    KVM_CHECK(queue != NULL);

    /* 40 pushes exceed the initial capacity of 16, forcing growth. */
    for (i = 0; i < 40; i++) {
        e.kind = kKVMEventResize;
        e.width = i;
        e.height = i * 2;
        KVM_CHECK(KVMEventQueuePush(queue, e) == kKVMSuccess);
    }
    KVM_CHECK(KVMEventQueueGetCount(queue) == 40);

    /* Pop all and confirm FIFO order was preserved across the realloc. */
    for (i = 0; i < 40; i++) {
        KVM_CHECK(KVMEventQueuePop(queue, &e) == KVM_TRUE);
        KVM_CHECK(e.width == i);
        KVM_CHECK(e.height == i * 2);
    }
    KVM_CHECK(KVMEventQueuePop(queue, &e) == KVM_FALSE);
    KVMRelease(queue);
}

int
main(void)
{
    TestFifoOrder();
    TestGrowthPreservesFifo();
    if (gTestsFailed) {
        printf("test_event: FAILED\n");
        return 1;
    }
    printf("test_event: OK\n");
    return 0;
}
