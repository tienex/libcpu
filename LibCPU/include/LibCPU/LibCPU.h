/** @file
  LibCPU public C API -- the CoreFoundation-style, "LC"-prefixed flat C surface.

  This is offered in parallel with the C++ (COM) interfaces of ICpu.h, not as a
  wrapper that hides them: C++ consumers may use the COM interfaces directly,
  while C consumers (and FFI bindings) use this. Opaque reference types follow
  the CoreFoundation idiom (LC<Type>Ref), reference counting uses LCRetain /
  LCRelease (mapping to COM AddRef/Release), and ownership follows the
  Create/Copy = owned, Get = unowned rule. Compiles as C23 and C++20.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_LIBCPU_H
#define LIBCPU_LIBCPU_H

#include "LibCPU/Base.h"

#ifdef __cplusplus
extern "C" {
#endif

//
// Opaque reference types (CoreFoundation style).
//
typedef CONST VOID *LCTypeRef;
typedef struct _LCValue        *LCValueRef;
typedef struct _LCBlock        *LCBlockRef;
typedef struct _LCEmitter      *LCEmitterRef;
typedef struct _LCArchitecture *LCArchitectureRef;
typedef struct _LCBackend      *LCBackendRef;
typedef struct _LCCode         *LCCodeRef;

//
// Mirror of the C++ enums (kept in sync with ICpu.h CPU_* values).
//
typedef enum _LC_BINOP {
    LCBinAdd, LCBinSub, LCBinMul,
    LCBinUDiv, LCBinSDiv, LCBinURem, LCBinSRem,
    LCBinAnd, LCBinOr, LCBinXor,
    LCBinShl, LCBinLShr, LCBinAShr,
    LCBinRol, LCBinRor
} LC_BINOP;

typedef enum _LC_CMP {
    LCCmpEq, LCCmpNe,
    LCCmpULt, LCCmpULe, LCCmpUGt, LCCmpUGe,
    LCCmpSLt, LCCmpSLe, LCCmpSGt, LCCmpSGe
} LC_CMP;

typedef enum _LC_EXEC_STATUS {
    LCExecOk,
    LCExecFuncNotFound,
    LCExecSingleStep,
    LCExecTrap
} LC_EXEC_STATUS;

typedef UINT64 LC_ADDR;

//
// Reference counting (CoreFoundation retain/release; maps to COM AddRef/Release).
//
LCTypeRef LCRetain  (IN LCTypeRef Ref);
VOID      LCRelease (IN LCTypeRef Ref);

//
// Backend -- create from a registered backend name (e.g. "interpreter", "llvm-22").
// Returned reference is owned by the caller (Create rule).
//
LCBackendRef LCBackendCreate      (IN CONST CHAR8 *pName);
CONST CHAR8 *LCBackendGetName     (IN LCBackendRef Backend);
LCEmitterRef LCBackendCreateEmitter (IN LCBackendRef Backend, IN LCArchitectureRef Arch); // owned
LCCodeRef    LCBackendCompile     (IN LCBackendRef Backend, IN LCEmitterRef Emitter);     // owned

//
// Emitter builder ops. Returned LCValueRef/LCBlockRef are owned by the emitter
// (Get rule) and live until the emitter is released.
//
LCValueRef LCEmitterConstInt    (IN LCEmitterRef E, UINT32 Bits, UINT64 Value);
LCValueRef LCEmitterGetRegister (IN LCEmitterRef E, UINT32 Index, UINT32 Bits);
VOID       LCEmitterPutRegister (IN LCEmitterRef E, UINT32 Index, IN LCValueRef Value, UINT32 Bits, BOOLEAN Sext);
LCValueRef LCEmitterLoad        (IN LCEmitterRef E, IN LCValueRef Addr, UINT32 Bits);
VOID       LCEmitterStore       (IN LCEmitterRef E, IN LCValueRef Value, IN LCValueRef Addr, UINT32 Bits);
LCValueRef LCEmitterBinaryOp    (IN LCEmitterRef E, LC_BINOP Op, IN LCValueRef A, IN LCValueRef B);
LCValueRef LCEmitterCompare     (IN LCEmitterRef E, LC_CMP Pred, IN LCValueRef A, IN LCValueRef B);
LCValueRef LCEmitterSelect      (IN LCEmitterRef E, IN LCValueRef Cond, IN LCValueRef T, IN LCValueRef F);
LCBlockRef LCEmitterCreateBlock (IN LCEmitterRef E, IN CONST CHAR8 *pName);
VOID       LCEmitterBranch      (IN LCEmitterRef E, IN LCBlockRef Target);
VOID       LCEmitterCondBranch  (IN LCEmitterRef E, IN LCValueRef Cond, IN LCBlockRef T, IN LCBlockRef F);

//
// Run a translated unit.
//
LC_EXEC_STATUS LCCodeExecute (IN LCCodeRef Code, IN VOID *pRAM, IN VOID *pGRF, IN VOID *pFRF);

#ifdef __cplusplus
}
#endif

#endif // LIBCPU_LIBCPU_H
