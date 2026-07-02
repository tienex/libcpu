/*
 * CoreKVM framebuffer implementation.
 */
#include "CoreKVM/KVMFrameBuffer.h"
#include "KVMRuntime.h"

#include <stdlib.h>

static const KVMTypeID kKVMFrameBufferTypeID = 0x4642554FUL; /* 'FBUO' */

struct _KVMFrameBuffer {
    KVMObjectHeader base;
    KVMInt32        width;
    KVMInt32        height;
    KVMIndex        stride;
    KVMUInt8       *pixels;
};

static void
KVMFrameBufferDealloc(void *object)
{
    struct _KVMFrameBuffer *fb = (struct _KVMFrameBuffer *)object;

    if (fb->pixels != NULL) {
        free(fb->pixels);
        fb->pixels = NULL;
    }
}

KVMFrameBufferRef
KVMFrameBufferCreate(KVMInt32 width, KVMInt32 height)
{
    struct _KVMFrameBuffer *fb;
    KVMIndex stride;

    if (width <= 0 || height <= 0) {
        return NULL;
    }

    if (width > KVM_FRAMEBUFFER_MAX_DIMENSION ||
        height > KVM_FRAMEBUFFER_MAX_DIMENSION) {
        return NULL;
    }

    fb = (struct _KVMFrameBuffer *)KVMRuntimeCreate(
        kKVMFrameBufferTypeID, (KVMIndex)sizeof(*fb), KVMFrameBufferDealloc);
    if (fb == NULL) {
        return NULL;
    }

    stride = (KVMIndex)width * KVM_FRAMEBUFFER_BYTES_PER_PIXEL;
    fb->pixels = (KVMUInt8 *)calloc((size_t)height, (size_t)stride);
    if (fb->pixels == NULL) {
        KVMRelease((KVMRef)fb);
        return NULL;
    }

    fb->width = width;
    fb->height = height;
    fb->stride = stride;
    return fb;
}

KVMInt32
KVMFrameBufferGetWidth(KVMFrameBufferRef fb)
{
    return (fb != NULL) ? fb->width : 0;
}

KVMInt32
KVMFrameBufferGetHeight(KVMFrameBufferRef fb)
{
    return (fb != NULL) ? fb->height : 0;
}

const KVMUInt8 *
KVMFrameBufferGetPixels(KVMFrameBufferRef fb, KVMIndex *outStride)
{
    if (fb == NULL) {
        return NULL;
    }
    if (outStride != NULL) {
        *outStride = fb->stride;
    }
    return fb->pixels;
}

static void
KVMConvertPixel(const KVMUInt8 *src, KVMPixelFormat format, KVMUInt8 *outBgra)
{
    KVMUInt16 v;

    switch (format) {
    case kKVMPixelFormatBGRA8888:
        outBgra[0] = src[0];
        outBgra[1] = src[1];
        outBgra[2] = src[2];
        outBgra[3] = src[3];
        break;

    case kKVMPixelFormatRGB888:
        outBgra[0] = src[2]; /* B */
        outBgra[1] = src[1]; /* G */
        outBgra[2] = src[0]; /* R */
        outBgra[3] = 255;
        break;

    case kKVMPixelFormatRGB565:
    default:
        v = (KVMUInt16)(src[0] | (src[1] << 8));
        /* Expand 5/6/5 to 8 bits by replicating high bits into low. */
        outBgra[0] = (KVMUInt8)(((v & 0x001F) << 3) | ((v & 0x001F) >> 2));
        outBgra[1] = (KVMUInt8)(((v & 0x07E0) >> 3) | ((v & 0x07E0) >> 9));
        outBgra[2] = (KVMUInt8)(((v & 0xF800) >> 8) | ((v & 0xF800) >> 13));
        outBgra[3] = 255;
        break;
    }
}

static KVMIndex
KVMBytesPerSourcePixel(KVMPixelFormat format)
{
    switch (format) {
    case kKVMPixelFormatBGRA8888:
        return 4;
    case kKVMPixelFormatRGB888:
        return 3;
    case kKVMPixelFormatRGB565:
    default:
        return 2;
    }
}

KVMStatus
KVMFrameBufferWriteRect(KVMFrameBufferRef fb, KVMRect rect, const void *src,
                        KVMPixelFormat srcFormat, KVMIndex srcStride)
{
    const KVMUInt8 *srcBytes;
    KVMIndex srcBpp;
    KVMInt32 row;
    KVMInt32 col;

    if (fb == NULL || src == NULL) {
        return kKVMErrorInvalidArgument;
    }
    if (rect.x < 0 || rect.y < 0 || rect.width <= 0 || rect.height <= 0) {
        return kKVMErrorInvalidArgument;
    }
    if (rect.x + rect.width > fb->width || rect.y + rect.height > fb->height) {
        return kKVMErrorInvalidArgument;
    }

    srcBytes = (const KVMUInt8 *)src;
    srcBpp = KVMBytesPerSourcePixel(srcFormat);

    for (row = 0; row < rect.height; row++) {
        const KVMUInt8 *srcLine = srcBytes + (KVMIndex)row * srcStride;
        KVMUInt8 *dstLine =
            fb->pixels + (KVMIndex)(rect.y + row) * fb->stride +
            (KVMIndex)rect.x * KVM_FRAMEBUFFER_BYTES_PER_PIXEL;

        for (col = 0; col < rect.width; col++) {
            KVMConvertPixel(srcLine + (KVMIndex)col * srcBpp, srcFormat,
                            dstLine + (KVMIndex)col *
                                          KVM_FRAMEBUFFER_BYTES_PER_PIXEL);
        }
    }
    return kKVMSuccess;
}
