/*
 * CoreKVM dirty-region accumulator: collects damaged rectangles and coalesces
 * overlapping or edge-touching ones by bounding-box union.
 */
#ifndef COREKVM_KVMDIRTY_H
#define COREKVM_KVMDIRTY_H

#include "CoreKVM/KVMBase.h"
#include "CoreKVM/KVMFrameBuffer.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef struct _KVMDirty *KVMDirtyRef;

KVMDirtyRef KVMDirtyCreate(void);
void        KVMDirtyMark(KVMDirtyRef dirty, KVMRect rect);
KVMIndex    KVMDirtyGetCount(KVMDirtyRef dirty);
KVMBool     KVMDirtyGetRect(KVMDirtyRef dirty, KVMIndex index, KVMRect *outRect);
void        KVMDirtyClear(KVMDirtyRef dirty);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMDIRTY_H */
