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

int
main(void)
{
    TestFifoOrder();
    if (gTestsFailed) {
        printf("test_event: FAILED\n");
        return 1;
    }
    printf("test_event: OK\n");
    return 0;
}
