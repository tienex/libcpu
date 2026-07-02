/*
 * Unit tests for the sans-I/O byte buffer.
 */
#include "CoreKVM/KVMBuffer.h"

#include <stdio.h>
#include <string.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static void
TestAppendConsume(void)
{
    KVMBufferRef buffer = KVMBufferCreate();
    const KVMUInt8 *bytes;

    KVM_CHECK(buffer != NULL);
    KVM_CHECK(KVMBufferAppend(buffer, "hello", 5) == kKVMSuccess);
    KVM_CHECK(KVMBufferAppend(buffer, "world", 5) == kKVMSuccess);
    KVM_CHECK(KVMBufferGetLength(buffer) == 10);

    KVM_CHECK(KVMBufferConsume(buffer, 3) == kKVMSuccess);
    KVM_CHECK(KVMBufferGetLength(buffer) == 7);

    bytes = KVMBufferGetBytes(buffer);
    KVM_CHECK(memcmp(bytes, "loworld", 7) == 0);

    KVMRelease(buffer);
}

static void
TestConsumeOverrunRejected(void)
{
    KVMBufferRef buffer = KVMBufferCreate();
    KVMBufferAppend(buffer, "ab", 2);
    KVM_CHECK(KVMBufferConsume(buffer, 5) == kKVMErrorInvalidArgument);
    KVM_CHECK(KVMBufferGetLength(buffer) == 2);
    KVMRelease(buffer);
}

int
main(void)
{
    TestAppendConsume();
    TestConsumeOverrunRejected();
    if (gTestsFailed) {
        printf("test_buffer: FAILED\n");
        return 1;
    }
    printf("test_buffer: OK\n");
    return 0;
}
