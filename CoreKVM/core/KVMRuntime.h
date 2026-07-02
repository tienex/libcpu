/*
 * CoreKVM internal object runtime. Every object embeds KVMObjectHeader as its
 * first member so a KVMRef can be reinterpreted as its header.
 */
#ifndef COREKVM_KVMRUNTIME_H
#define COREKVM_KVMRUNTIME_H

#include "CoreKVM/KVMBase.h"

typedef void (*KVMDeallocFn)(void *object);

typedef struct _KVMObjectHeader {
    KVMTypeID    typeID;
    KVMIndex     retainCount;
    KVMDeallocFn dealloc;
} KVMObjectHeader;

/*
 * Allocate a zero-filled object of `size` bytes (>= sizeof(KVMObjectHeader)),
 * with retain count 1. Returns NULL on allocation failure or bad arguments.
 */
void *KVMRuntimeCreate(KVMTypeID typeID, KVMIndex size, KVMDeallocFn dealloc);

#endif /* COREKVM_KVMRUNTIME_H */
