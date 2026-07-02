/*
 * CoreKVM public base: portable fixed-width types, status codes, and the
 * CoreFoundation-style object handle + memory management contract.
 */
#ifndef COREKVM_KVMBASE_H
#define COREKVM_KVMBASE_H

#if defined(__cplusplus)
extern "C" {
#endif

/*
 * Fixed-width types. C90 has no <stdint.h>, so they are defined here and a
 * compile-time size assertion below rejects targets where they do not hold.
 */
typedef unsigned char  KVMUInt8;
typedef signed char    KVMInt8;
typedef unsigned short KVMUInt16;
typedef short          KVMInt16;
typedef unsigned int   KVMUInt32;
typedef int            KVMInt32;

typedef long           KVMIndex;   /* signed length/offset/count */
typedef int            KVMBool;
typedef unsigned long  KVMTypeID;

#define KVM_TRUE  1
#define KVM_FALSE 0

/* Compile-time width checks (illegal negative array size on mismatch). */
typedef char KVMStaticAssertUInt8[(sizeof(KVMUInt8) == 1) ? 1 : -1];
typedef char KVMStaticAssertUInt16[(sizeof(KVMUInt16) == 2) ? 1 : -1];
typedef char KVMStaticAssertUInt32[(sizeof(KVMUInt32) == 4) ? 1 : -1];

typedef enum _KVMStatus {
    kKVMSuccess = 0,
    kKVMErrorNoMemory,
    kKVMErrorInvalidArgument,
    kKVMErrorProtocol,
    kKVMErrorWouldBlock,
    kKVMErrorClosed,
    kKVMErrorUnsupported
} KVMStatus;

/* Opaque handle common to every CoreKVM object. */
typedef void *KVMRef;

KVMRef    KVMRetain(KVMRef object);
void      KVMRelease(KVMRef object);
KVMIndex  KVMGetRetainCount(KVMRef object);
KVMTypeID KVMGetTypeID(KVMRef object);

#if defined(__cplusplus)
}
#endif

#endif /* COREKVM_KVMBASE_H */
