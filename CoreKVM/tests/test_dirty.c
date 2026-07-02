/*
 * Unit tests for the dirty-region accumulator.
 */
#include "CoreKVM/KVMDirty.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static KVMRect
MakeRect(KVMInt32 x, KVMInt32 y, KVMInt32 w, KVMInt32 h)
{
    KVMRect r;
    r.x = x;
    r.y = y;
    r.width = w;
    r.height = h;
    return r;
}

static void
TestDisjointStaySeparate(void)
{
    KVMDirtyRef dirty = KVMDirtyCreate();
    KVM_CHECK(dirty != NULL);
    KVMDirtyMark(dirty, MakeRect(0, 0, 2, 2));
    KVMDirtyMark(dirty, MakeRect(10, 10, 2, 2));
    KVM_CHECK(KVMDirtyGetCount(dirty) == 2);
    KVMRelease(dirty);
}

static void
TestOverlapCoalesces(void)
{
    KVMDirtyRef dirty = KVMDirtyCreate();
    KVMRect out;

    KVMDirtyMark(dirty, MakeRect(0, 0, 4, 4));
    KVMDirtyMark(dirty, MakeRect(2, 2, 4, 4));
    KVM_CHECK(KVMDirtyGetCount(dirty) == 1);
    KVM_CHECK(KVMDirtyGetRect(dirty, 0, &out) == KVM_TRUE);
    /* Union bounding box: (0,0) .. (6,6). */
    KVM_CHECK(out.x == 0 && out.y == 0 && out.width == 6 && out.height == 6);

    KVMDirtyClear(dirty);
    KVM_CHECK(KVMDirtyGetCount(dirty) == 0);
    KVMRelease(dirty);
}

int
main(void)
{
    TestDisjointStaySeparate();
    TestOverlapCoalesces();
    if (gTestsFailed) {
        printf("test_dirty: FAILED\n");
        return 1;
    }
    printf("test_dirty: OK\n");
    return 0;
}
