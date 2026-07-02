/*
 * Unit tests for the CoreKVM object runtime.
 */
#include "CoreKVM/KVMBase.h"
#include "KVMRuntime.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static const KVMTypeID kDummyTypeID = 0x11110001UL;
static int gDeallocCount;

typedef struct _Dummy {
    KVMObjectHeader base;
    int value;
} Dummy;

static void
DummyDealloc(void *object)
{
    (void)object;
    gDeallocCount++;
}

static void
TestRetainReleaseLifecycle(void)
{
    Dummy *dummy;

    gDeallocCount = 0;
    dummy = (Dummy *)KVMRuntimeCreate(kDummyTypeID, (KVMIndex)sizeof(Dummy),
                                      DummyDealloc);
    KVM_CHECK(dummy != NULL);
    KVM_CHECK(KVMGetRetainCount((KVMRef)dummy) == 1);
    KVM_CHECK(KVMGetTypeID((KVMRef)dummy) == kDummyTypeID);

    KVMRetain((KVMRef)dummy);
    KVM_CHECK(KVMGetRetainCount((KVMRef)dummy) == 2);

    KVMRelease((KVMRef)dummy);
    KVM_CHECK(KVMGetRetainCount((KVMRef)dummy) == 1);
    KVM_CHECK(gDeallocCount == 0);

    KVMRelease((KVMRef)dummy);
    KVM_CHECK(gDeallocCount == 1);
}

static void
TestNullSafety(void)
{
    /* Retain/Release/getters must tolerate NULL like CoreFoundation. */
    KVMRelease(NULL);
    KVM_CHECK(KVMRetain(NULL) == NULL);
    KVM_CHECK(KVMGetRetainCount(NULL) == 0);
}

int
main(void)
{
    TestRetainReleaseLifecycle();
    TestNullSafety();
    if (gTestsFailed) {
        printf("test_base: FAILED\n");
        return 1;
    }
    printf("test_base: OK\n");
    return 0;
}
