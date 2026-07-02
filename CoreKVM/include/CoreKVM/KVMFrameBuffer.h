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

typedef enum _KVMPixelFormat {
    kKVMPixelFormatBGRA8888 = 0,   /* canonical */
    kKVMPixelFormatRGB888,
    kKVMPixelFormatRGB565
} KVMPixelFormat;

/* Canonical surface is always BGRA8888, 4 bytes per pixel. */
#define KVM_FRAMEBUFFER_BYTES_PER_PIXEL 4

/* Fully opaque alpha for surfaces converted from formats without an alpha channel. */
#define KVM_ALPHA_OPAQUE 255

/* Upper bound per axis for a single canonical surface; keeps stride and the
 * total allocation well within size_t on 32-bit (LLP64/ILP32) targets. */
#define KVM_FRAMEBUFFER_MAX_DIMENSION 32767

KVMFrameBufferRef KVMFrameBufferCreate(KVMInt32 width, KVMInt32 height);
KVMInt32          KVMFrameBufferGetWidth(KVMFrameBufferRef fb);
KVMInt32          KVMFrameBufferGetHeight(KVMFrameBufferRef fb);

/*
 * Borrowed pointer to the BGRA pixel storage; *outStride receives the row
 * stride in bytes (width * 4). Storage lives as long as the framebuffer.
 */
const KVMUInt8 *KVMFrameBufferGetPixels(KVMFrameBufferRef fb, KVMIndex *outStride);

/*
 * Convert `src` (rows of `srcFormat`, `srcStride` bytes per row) into the
 * canonical BGRA surface at `rect`. Returns kKVMErrorInvalidArgument if `rect`
 * lies outside the framebuffer.
 */
KVMStatus KVMFrameBufferWriteRect(KVMFrameBufferRef fb, KVMRect rect,
                                  const void *src, KVMPixelFormat srcFormat,
                                  KVMIndex srcStride);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMFRAMEBUFFER_H */
