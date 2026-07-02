/*
 * CoreKVM framebuffer: a canonical BGRA8888 surface. Foreign pixel formats are
 * converted into it (Task 3); damage is tracked as dirty rectangles (Task 4).
 */
#ifndef COREKVM_KVMFRAMEBUFFER_H
#define COREKVM_KVMFRAMEBUFFER_H

#include "CoreKVM/KVMBase.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _KVMFrameBuffer *KVMFrameBufferRef;

typedef struct _KVMRect {
    KVMInt32 x;
    KVMInt32 y;
    KVMInt32 width;
    KVMInt32 height;
} KVMRect;

/* Canonical surface is always BGRA8888, 4 bytes per pixel. */
#define KVM_FRAMEBUFFER_BYTES_PER_PIXEL 4

KVMFrameBufferRef KVMFrameBufferCreate(KVMInt32 width, KVMInt32 height);
KVMInt32          KVMFrameBufferGetWidth(KVMFrameBufferRef fb);
KVMInt32          KVMFrameBufferGetHeight(KVMFrameBufferRef fb);

/*
 * Borrowed pointer to the BGRA pixel storage; *outStride receives the row
 * stride in bytes (width * 4). Storage lives as long as the framebuffer.
 */
const KVMUInt8 *KVMFrameBufferGetPixels(KVMFrameBufferRef fb, KVMIndex *outStride);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMFRAMEBUFFER_H */
