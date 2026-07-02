/*
 * CoreKVM unified input event model: absolute + relative pointer, wheel, and
 * key events carrying both an X keysym and a hardware scancode.
 */
#ifndef COREKVM_KVMINPUT_H
#define COREKVM_KVMINPUT_H

#include "CoreKVM/KVMBase.h"

#if defined(__cplusplus)
extern "C" {
#endif

typedef enum _KVMInputKind {
    kKVMInputPointerMove = 0,
    kKVMInputPointerButton,
    kKVMInputWheel,
    kKVMInputKey,
    kKVMInputRelativePointer
} KVMInputKind;

typedef struct _KVMInputEvent {
    KVMInputKind kind;
    KVMInt32     x;          /* absolute position, or dx for relative */
    KVMInt32     y;          /* absolute position, or dy for relative */
    KVMUInt32    buttonMask; /* one bit per pointer button */
    KVMInt32     wheelDelta;
    KVMUInt32    keysym;     /* X11 keysym */
    KVMUInt32    scancode;   /* hardware scancode (extended key events) */
    KVMBool      pressed;    /* button/key press vs release */
} KVMInputEvent;

KVMInputEvent KVMInputMakePointerMove(KVMInt32 x, KVMInt32 y,
                                      KVMUInt32 buttonMask);
KVMInputEvent KVMInputMakeKey(KVMUInt32 keysym, KVMUInt32 scancode,
                              KVMBool pressed);
KVMInputEvent KVMInputMakeRelative(KVMInt32 dx, KVMInt32 dy,
                                   KVMUInt32 buttonMask);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMINPUT_H */
