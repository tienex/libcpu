/*
 * CoreKVM dirty-region accumulator implementation.
 */
#include "CoreKVM/KVMDirty.h"
#include "KVMRuntime.h"

#include <stdlib.h>

static const KVMTypeID kKVMDirtyTypeID = 0x44495254UL; /* 'DIRT' */

#define KVM_DIRTY_INITIAL_CAPACITY 8

struct _KVMDirty {
    KVMObjectHeader base;
    KVMRect        *rects;
    KVMIndex        count;
    KVMIndex        capacity;
};

static void
KVMDirtyDealloc(void *object)
{
    struct _KVMDirty *dirty = (struct _KVMDirty *)object;

    if (dirty->rects != NULL) {
        free(dirty->rects);
        dirty->rects = NULL;
    }
}

static KVMBool
KVMRectsTouchOrOverlap(const KVMRect *a, const KVMRect *b)
{
    /* Expand `a` by one pixel so edge-adjacent rects count as touching. */
    KVMInt32 ax0 = a->x - 1;
    KVMInt32 ay0 = a->y - 1;
    KVMInt32 ax1 = a->x + a->width + 1;
    KVMInt32 ay1 = a->y + a->height + 1;
    KVMInt32 bx0 = b->x;
    KVMInt32 by0 = b->y;
    KVMInt32 bx1 = b->x + b->width;
    KVMInt32 by1 = b->y + b->height;

    if (ax1 <= bx0 || bx1 <= ax0) {
        return KVM_FALSE;
    }
    if (ay1 <= by0 || by1 <= ay0) {
        return KVM_FALSE;
    }
    return KVM_TRUE;
}

static KVMRect
KVMRectUnion(const KVMRect *a, const KVMRect *b)
{
    KVMRect out;
    KVMInt32 x0 = (a->x < b->x) ? a->x : b->x;
    KVMInt32 y0 = (a->y < b->y) ? a->y : b->y;
    KVMInt32 ax1 = a->x + a->width;
    KVMInt32 ay1 = a->y + a->height;
    KVMInt32 bx1 = b->x + b->width;
    KVMInt32 by1 = b->y + b->height;
    KVMInt32 x1 = (ax1 > bx1) ? ax1 : bx1;
    KVMInt32 y1 = (ay1 > by1) ? ay1 : by1;

    out.x = x0;
    out.y = y0;
    out.width = x1 - x0;
    out.height = y1 - y0;
    return out;
}

KVMDirtyRef
KVMDirtyCreate(void)
{
    struct _KVMDirty *dirty;

    dirty = (struct _KVMDirty *)KVMRuntimeCreate(
        kKVMDirtyTypeID, (KVMIndex)sizeof(*dirty), KVMDirtyDealloc);
    if (dirty == NULL) {
        return NULL;
    }
    dirty->rects =
        (KVMRect *)malloc(sizeof(KVMRect) * KVM_DIRTY_INITIAL_CAPACITY);
    if (dirty->rects == NULL) {
        KVMRelease((KVMRef)dirty);
        return NULL;
    }
    dirty->capacity = KVM_DIRTY_INITIAL_CAPACITY;
    dirty->count = 0;
    return dirty;
}

void
KVMDirtyMark(KVMDirtyRef dirty, KVMRect rect)
{
    KVMIndex i;

    if (dirty == NULL || rect.width <= 0 || rect.height <= 0) {
        return;
    }

    /* Coalesce into the first rect it touches; repeat until stable. */
    for (i = 0; i < dirty->count; i++) {
        if (KVMRectsTouchOrOverlap(&dirty->rects[i], &rect)) {
            rect = KVMRectUnion(&dirty->rects[i], &rect);
            dirty->rects[i] = dirty->rects[dirty->count - 1];
            dirty->count--;
            i = -1; /* restart scan; ++ makes it 0 */
        }
    }

    if (dirty->count == dirty->capacity) {
        KVMIndex newCap = dirty->capacity * 2;
        KVMRect *grown =
            (KVMRect *)realloc(dirty->rects, sizeof(KVMRect) * (size_t)newCap);
        if (grown == NULL) {
            return; /* drop the mark rather than corrupt state */
        }
        dirty->rects = grown;
        dirty->capacity = newCap;
    }
    dirty->rects[dirty->count] = rect;
    dirty->count++;
}

KVMIndex
KVMDirtyGetCount(KVMDirtyRef dirty)
{
    return (dirty != NULL) ? dirty->count : 0;
}

KVMBool
KVMDirtyGetRect(KVMDirtyRef dirty, KVMIndex index, KVMRect *outRect)
{
    if (dirty == NULL || outRect == NULL) {
        return KVM_FALSE;
    }
    if (index < 0 || index >= dirty->count) {
        return KVM_FALSE;
    }
    *outRect = dirty->rects[index];
    return KVM_TRUE;
}

void
KVMDirtyClear(KVMDirtyRef dirty)
{
    if (dirty != NULL) {
        dirty->count = 0;
    }
}
