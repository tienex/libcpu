/*
 * CoreKVM growable byte buffer: the sans-I/O plumbing that inbound and outbound
 * protocol bytes flow through. Consume drops bytes from the front.
 */
#ifndef COREKVM_KVMBUFFER_H
#define COREKVM_KVMBUFFER_H

#include "CoreKVM/KVMBase.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _KVMBuffer *KVMBufferRef;

KVMBufferRef    KVMBufferCreate(void);
KVMStatus       KVMBufferAppend(KVMBufferRef buffer, const void *data,
                                KVMIndex length);
KVMIndex        KVMBufferGetLength(KVMBufferRef buffer);
const KVMUInt8 *KVMBufferGetBytes(KVMBufferRef buffer);
KVMStatus       KVMBufferConsume(KVMBufferRef buffer, KVMIndex length);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMBUFFER_H */
