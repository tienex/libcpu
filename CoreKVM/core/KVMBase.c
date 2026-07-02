/*
 * CoreKVM object runtime implementation.
 */
#include "CoreKVM/KVMBase.h"
#include "KVMRuntime.h"

#include <stdlib.h>
#include <string.h>

void *
KVMRuntimeCreate(KVMTypeID typeID, KVMIndex size, KVMDeallocFn dealloc)
{
    KVMObjectHeader *header;

    if (size < (KVMIndex)sizeof(KVMObjectHeader)) {
        return NULL;
    }

    header = (KVMObjectHeader *)malloc((size_t)size);
    if (header == NULL) {
        return NULL;
    }

    memset(header, 0, (size_t)size);
    header->typeID = typeID;
    header->retainCount = 1;
    header->dealloc = dealloc;
    return header;
}

KVMRef
KVMRetain(KVMRef object)
{
    KVMObjectHeader *header;

    if (object == NULL) {
        return NULL;
    }
    header = (KVMObjectHeader *)object;
    header->retainCount++;
    return object;
}

void
KVMRelease(KVMRef object)
{
    KVMObjectHeader *header;

    if (object == NULL) {
        return;
    }
    header = (KVMObjectHeader *)object;
    header->retainCount--;
    if (header->retainCount <= 0) {
        if (header->dealloc != NULL) {
            header->dealloc(object);
        }
        free(header);
    }
}

KVMIndex
KVMGetRetainCount(KVMRef object)
{
    if (object == NULL) {
        return 0;
    }
    return ((KVMObjectHeader *)object)->retainCount;
}

KVMTypeID
KVMGetTypeID(KVMRef object)
{
    if (object == NULL) {
        return 0;
    }
    return ((KVMObjectHeader *)object)->typeID;
}
