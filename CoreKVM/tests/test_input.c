/*
 * Unit tests for the input event model constructors.
 */
#include "CoreKVM/KVMInput.h"

#include <stdio.h>

static int gTestsFailed = 0;

#define KVM_CHECK(cond)                                                        \
    do {                                                                       \
        if (!(cond)) {                                                         \
            printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);             \
            gTestsFailed = 1;                                                  \
        }                                                                      \
    } while (0)

static void
TestPointerMove(void)
{
    KVMInputEvent e = KVMInputMakePointerMove(100, 50, 0x1);
    KVM_CHECK(e.kind == kKVMInputPointerMove);
    KVM_CHECK(e.x == 100);
    KVM_CHECK(e.y == 50);
    KVM_CHECK(e.buttonMask == 0x1);
    KVM_CHECK(e.pressed == KVM_FALSE);
}

static void
TestKey(void)
{
    KVMInputEvent e = KVMInputMakeKey(0x0041U, 0x001EU, KVM_TRUE);
    KVM_CHECK(e.kind == kKVMInputKey);
    KVM_CHECK(e.keysym == 0x0041U);
    KVM_CHECK(e.scancode == 0x001EU);
    KVM_CHECK(e.pressed == KVM_TRUE);
}

static void
TestRelative(void)
{
    KVMInputEvent e = KVMInputMakeRelative(-3, 4, 0x2);
    KVM_CHECK(e.kind == kKVMInputRelativePointer);
    KVM_CHECK(e.x == -3);
    KVM_CHECK(e.y == 4);
    KVM_CHECK(e.buttonMask == 0x2);
}

int
main(void)
{
    TestPointerMove();
    TestKey();
    TestRelative();
    if (gTestsFailed) {
        printf("test_input: FAILED\n");
        return 1;
    }
    printf("test_input: OK\n");
    return 0;
}
