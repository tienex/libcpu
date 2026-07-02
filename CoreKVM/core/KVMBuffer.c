/*
 * CoreKVM byte buffer implementation. Bytes are stored contiguously; Consume
 * advances a read cursor, and the buffer compacts (slides remaining bytes to
 * the front) whenever it must make room for an append.
 */
#include "CoreKVM/KVMBuffer.h"
#include "KVMRuntime.h"

#include <stdlib.h>
#include <string.h>

static const KVMTypeID kKVMBufferTypeID = 0x42554621UL; /* 'BUF!' */

#define KVM_BUFFER_INITIAL_CAPACITY 256

struct _KVMBuffer {
    KVMObjectHeader base;
    KVMUInt8       *data;
    KVMIndex        capacity;
    KVMIndex        head;   /* read cursor */
    KVMIndex        tail;   /* write cursor */
};

static void
KVMBufferDealloc(void *object)
{
    struct _KVMBuffer *buffer = (struct _KVMBuffer *)object;

    if (buffer->data != NULL) {
        free(buffer->data);
        buffer->data = NULL;
    }
}

KVMBufferRef
KVMBufferCreate(void)
{
    struct _KVMBuffer *buffer;

    buffer = (struct _KVMBuffer *)KVMRuntimeCreate(
        kKVMBufferTypeID, (KVMIndex)sizeof(*buffer), KVMBufferDealloc);
    if (buffer == NULL) {
        return NULL;
    }
    buffer->data = (KVMUInt8 *)malloc(KVM_BUFFER_INITIAL_CAPACITY);
    if (buffer->data == NULL) {
        KVMRelease((KVMRef)buffer);
        return NULL;
    }
    buffer->capacity = KVM_BUFFER_INITIAL_CAPACITY;
    buffer->head = 0;
    buffer->tail = 0;
    return buffer;
}

static KVMStatus
KVMBufferEnsureRoom(struct _KVMBuffer *buffer, KVMIndex extra)
{
    KVMIndex used = buffer->tail - buffer->head;
    KVMIndex needed;
    KVMIndex newCap;
    KVMUInt8 *grown;

    /* Compact first: slide contents to the front. */
    if (buffer->head > 0) {
        memmove(buffer->data, buffer->data + buffer->head, (size_t)used);
        buffer->head = 0;
        buffer->tail = used;
    }

    if (used > KVM_INDEX_MAX - extra) {
        return kKVMErrorNoMemory;
    }
    needed = used + extra;
    if (needed <= buffer->capacity) {
        return kKVMSuccess;
    }

    newCap = buffer->capacity;
    while (newCap < needed) {
        if (newCap > KVM_INDEX_MAX / 2) {
            /* Would overflow KVMIndex on doubling; clamp to `needed` if it
             * still fits, otherwise fail rather than wrap. */
            if (needed <= KVM_INDEX_MAX) {
                newCap = needed;
            } else {
                return kKVMErrorNoMemory;
            }
            break;
        }
        newCap *= 2;
    }
    grown = (KVMUInt8 *)realloc(buffer->data, (size_t)newCap);
    if (grown == NULL) {
        return kKVMErrorNoMemory;
    }
    buffer->data = grown;
    buffer->capacity = newCap;
    return kKVMSuccess;
}

KVMStatus
KVMBufferAppend(KVMBufferRef buffer, const void *data, KVMIndex length)
{
    KVMStatus status;

    if (buffer == NULL || (data == NULL && length > 0) || length < 0) {
        return kKVMErrorInvalidArgument;
    }
    if (length == 0) {
        return kKVMSuccess;
    }
    status = KVMBufferEnsureRoom(buffer, length);
    if (status != kKVMSuccess) {
        return status;
    }
    memcpy(buffer->data + buffer->tail, data, (size_t)length);
    buffer->tail += length;
    return kKVMSuccess;
}

KVMIndex
KVMBufferGetLength(KVMBufferRef buffer)
{
    return (buffer != NULL) ? (buffer->tail - buffer->head) : 0;
}

const KVMUInt8 *
KVMBufferGetBytes(KVMBufferRef buffer)
{
    if (buffer == NULL) {
        return NULL;
    }
    return buffer->data + buffer->head;
}

KVMStatus
KVMBufferConsume(KVMBufferRef buffer, KVMIndex length)
{
    if (buffer == NULL || length < 0) {
        return kKVMErrorInvalidArgument;
    }
    if (length > buffer->tail - buffer->head) {
        return kKVMErrorInvalidArgument;
    }
    buffer->head += length;
    if (buffer->head == buffer->tail) {
        buffer->head = 0;
        buffer->tail = 0;
    }
    return kKVMSuccess;
}
