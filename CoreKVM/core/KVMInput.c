/*
 * CoreKVM input event constructors.
 */
#include "CoreKVM/KVMInput.h"

#include <string.h>

static KVMInputEvent
KVMInputZero(void)
{
    KVMInputEvent e;
    memset(&e, 0, sizeof(e));
    return e;
}

KVMInputEvent
KVMInputMakePointerMove(KVMInt32 x, KVMInt32 y, KVMUInt32 buttonMask)
{
    KVMInputEvent e = KVMInputZero();
    e.kind = kKVMInputPointerMove;
    e.x = x;
    e.y = y;
    e.buttonMask = buttonMask;
    return e;
}

KVMInputEvent
KVMInputMakeKey(KVMUInt32 keysym, KVMUInt32 scancode, KVMBool pressed)
{
    KVMInputEvent e = KVMInputZero();
    e.kind = kKVMInputKey;
    e.keysym = keysym;
    e.scancode = scancode;
    e.pressed = pressed;
    return e;
}

KVMInputEvent
KVMInputMakeRelative(KVMInt32 dx, KVMInt32 dy, KVMUInt32 buttonMask)
{
    KVMInputEvent e = KVMInputZero();
    e.kind = kKVMInputRelativePointer;
    e.x = dx;
    e.y = dy;
    e.buttonMask = buttonMask;
    return e;
}
