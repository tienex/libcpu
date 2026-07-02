/*
 * Unit tests for KVMFrameBuffer allocation and access.
 */
#include "CoreKVM/KVMFrameBuffer.h"

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
TestCreateAndAccess(void)
{
    KVMFrameBufferRef fb;
    const KVMUInt8 *pixels;
    KVMIndex stride;
    KVMIndex i;
    KVMBool allZero;

    fb = KVMFrameBufferCreate(4, 3);
    KVM_CHECK(fb != NULL);
    KVM_CHECK(KVMFrameBufferGetWidth(fb) == 4);
    KVM_CHECK(KVMFrameBufferGetHeight(fb) == 3);

    stride = 0;
    pixels = KVMFrameBufferGetPixels(fb, &stride);
    KVM_CHECK(pixels != NULL);
    KVM_CHECK(stride == 4 * 4);

    allZero = KVM_TRUE;
    for (i = 0; i < stride * 3; i++) {
        if (pixels[i] != 0) {
            allZero = KVM_FALSE;
        }
    }
    KVM_CHECK(allZero == KVM_TRUE);

    KVMRelease(fb);
}

static void
TestRejectsBadSize(void)
{
    KVM_CHECK(KVMFrameBufferCreate(0, 10) == NULL);
    KVM_CHECK(KVMFrameBufferCreate(10, -1) == NULL);
}

int
main(void)
{
    TestCreateAndAccess();
    TestRejectsBadSize();
    if (gTestsFailed) {
        printf("test_framebuffer: FAILED\n");
        return 1;
    }
    printf("test_framebuffer: OK\n");
    return 0;
}
