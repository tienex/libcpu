/*
 * Unit tests for pixel-format conversion into the canonical BGRA surface.
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
TestWriteRGB565Red(void)
{
    KVMFrameBufferRef fb;
    const KVMUInt8 *pixels;
    KVMIndex stride;
    KVMRect rect;
    KVMUInt16 srcRow[2];
    KVMStatus status;

    /* Two RGB565 red pixels: R=0x1F, G=0, B=0 -> 0xF800. */
    srcRow[0] = (KVMUInt16)0xF800;
    srcRow[1] = (KVMUInt16)0xF800;

    fb = KVMFrameBufferCreate(2, 1);
    KVM_CHECK(fb != NULL);

    rect.x = 0;
    rect.y = 0;
    rect.width = 2;
    rect.height = 1;
    status = KVMFrameBufferWriteRect(fb, rect, srcRow, kKVMPixelFormatRGB565,
                                     (KVMIndex)sizeof(srcRow));
    KVM_CHECK(status == kKVMSuccess);

    pixels = KVMFrameBufferGetPixels(fb, &stride);
    /* BGRA: B=0, G=0, R=255, A=255. */
    KVM_CHECK(pixels[0] == 0);
    KVM_CHECK(pixels[1] == 0);
    KVM_CHECK(pixels[2] == 255);
    KVM_CHECK(pixels[3] == 255);

    KVMRelease(fb);
}

static void
TestRejectsOutOfBounds(void)
{
    KVMFrameBufferRef fb;
    KVMRect rect;
    KVMUInt8 src[4];
    KVMStatus status;

    fb = KVMFrameBufferCreate(2, 2);
    rect.x = 1;
    rect.y = 1;
    rect.width = 2; /* extends to x=3 > 2 */
    rect.height = 1;
    status = KVMFrameBufferWriteRect(fb, rect, src, kKVMPixelFormatBGRA8888, 8);
    KVM_CHECK(status == kKVMErrorInvalidArgument);
    KVMRelease(fb);
}

static void
TestRejectsBadArguments(void)
{
    KVMFrameBufferRef fb;
    KVMRect rect;
    KVMUInt8 src[16];

    fb = KVMFrameBufferCreate(2, 2);
    rect.x = 0;
    rect.y = 0;
    rect.width = 2;
    rect.height = 2;
    /* Unknown pixel format (out of enum range). */
    KVM_CHECK(KVMFrameBufferWriteRect(fb, rect, src, (KVMPixelFormat)99,
                                      2 * 4) == kKVMErrorInvalidArgument);
    /* srcStride too small to hold a row (2 px * 4 bytes = 8, give 4). */
    KVM_CHECK(KVMFrameBufferWriteRect(fb, rect, src, kKVMPixelFormatBGRA8888,
                                      4) == kKVMErrorInvalidArgument);
    /* Zero stride. */
    KVM_CHECK(KVMFrameBufferWriteRect(fb, rect, src, kKVMPixelFormatBGRA8888,
                                      0) == kKVMErrorInvalidArgument);
    KVMRelease(fb);
}

int
main(void)
{
    TestWriteRGB565Red();
    TestRejectsOutOfBounds();
    TestRejectsBadArguments();
    if (gTestsFailed) {
        printf("test_pixelconvert: FAILED\n");
        return 1;
    }
    printf("test_pixelconvert: OK\n");
    return 0;
}
