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

    fb = (struct _KVMFrameBuffer *)KVMRuntimeCreate(
        kKVMFrameBufferTypeID, (KVMIndex)sizeof(*fb), KVMFrameBufferDealloc);
    if (fb == NULL) {
        return NULL;
    }

    stride = (KVMIndex)width * KVM_FRAMEBUFFER_BYTES_PER_PIXEL;
    fb->pixels = (KVMUInt8 *)calloc((size_t)(stride * height), 1);
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
