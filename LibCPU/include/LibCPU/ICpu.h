/** @file
  LibCPU C++ (COM) interface layer.

  These are the implementation-facing interfaces. The frontend↔backend boundary
  is the *opaque-handle builder* model: a frontend (ICpuArchitecture) emits an
  instruction's semantics by calling ICpuEmitter methods that return opaque value
  and block handles (ICpuValue / ICpuBlock); each backend implements ICpuEmitter
  in its own terms (the LLVM backend wraps LLVM Value and BasicBlock pointers,
  the interpreter backend wraps its own value objects). This is what decouples
  frontends from any particular code generator -- no frontend references LLVM.

  Public consumers may use these C++ interfaces directly, or the parallel
  CoreFoundation-style C API (LibCPU-C/LibCPU.h). C++20.

  Copyright (c) the LibCPU developers. Distributed under the 2-clause BSD license.
**/

#ifndef LIBCPU_ICPU_H
#define LIBCPU_ICPU_H

#include "LibCPU/PCom.h"

namespace LibCPU {

//
// Binary operations emitted by the builder.
//
typedef enum _CPU_BINOP {
    BinAdd, BinSub, BinMul,
    BinUDiv, BinSDiv, BinURem, BinSRem,
    BinAnd, BinOr, BinXor,
    BinShl, BinLShr, BinAShr,
    BinRol, BinRor
} CPU_BINOP;

//
// Unary operations.
//
typedef enum _CPU_UNOP {
    UnNeg,   // arithmetic negate (-x)
    UnCom,   // bitwise complement (~x)
    UnNot    // logical not (!x), producing an i1
} CPU_UNOP;

//
// Integer comparison predicates (U = unsigned, S = signed).
//
typedef enum _CPU_CMP {
    CmpEq, CmpNe,
    CmpULt, CmpULe, CmpUGt, CmpUGe,
    CmpSLt, CmpSLe, CmpSGt, CmpSGe
} CPU_CMP;

//
// Width-changing casts.
//
typedef enum _CPU_CAST {
    CastTrunc,   // narrow
    CastZExt,    // widen, zero-extend
    CastSExt     // widen, sign-extend
} CPU_CAST;

//
// Architectural condition flags (generic; a frontend maps its PSR bits onto these).
//
typedef enum _CPU_FLAG {
    FlagNegative,
    FlagOverflow,
    FlagZero,
    FlagCarry,
    FlagParity
} CPU_FLAG;

//
// Result of executing a translated unit.
//
typedef enum _CPU_EXEC_STATUS {
    ExecOk,
    ExecFuncNotFound,
    ExecSingleStep,
    ExecTrap,
    ExecSmc            // a self-modifying-code guard fired; re-translate from TrapPc
} CPU_EXEC_STATUS;

//
// Instruction classification bits returned by ICpuArchitecture::TagInstr.
//
typedef enum _CPU_TAG {
    TagContinue    = 1u << 0,
    TagCall        = 1u << 1,
    TagReturn      = 1u << 2,
    TagBranch      = 1u << 3,
    TagTrap        = 1u << 4,
    TagConditional = 1u << 5,
    TagDelaySlot   = 1u << 6
} CPU_TAG;

//
// Guest address type.
//
typedef UINT64 CPU_ADDR;

//
// Compact architecture description handed to the core at frontend init.
//
typedef struct _CPU_ARCH_INFO {
    CHAR8 CONST *pName;
    CHAR8 CONST *pFullName;
    UINT32       ByteSize;
    UINT32       WordSize;
    UINT32       AddressSize;
    UINT32       PsrSize;
    BOOLEAN      IsBigEndian;
    UINT32       GprCount;
    UINT32       GprBits;
} CPU_ARCH_INFO;

//
// Opaque handles produced by the builder. They are COM objects so they refcount
// and cross the C boundary as LCValueRef / LCBlockRef, but carry no public
// members -- only the producing backend knows their concrete representation.
//
DECLARE_INTERFACE_ (ICpuValue, IUnknown) { };
DECLARE_INTERFACE_ (ICpuBlock, IUnknown) { };

/**
  ICpuEmitter -- the architecture-neutral instruction builder.

  A frontend emits one instruction's semantics by calling these methods. Each
  backend supplies its own ICpuEmitter implementation. Value/block ownership
  follows COM rules: returned handles are owned by the caller (Release when done)
  unless documented otherwise; the emitter retains its own references as needed.
**/
DECLARE_INTERFACE_ (ICpuEmitter, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    //
    // Values.
    //
    STDMETHOD (ConstInt)(THIS_ UINT32 Bits, UINT64 Value, OUT ICpuValue **ppValue) PURE;
    STDMETHOD (GetRegister)(THIS_ UINT32 Index, UINT32 Bits, OUT ICpuValue **ppValue) PURE;
    STDMETHOD (PutRegister)(THIS_ UINT32 Index, IN ICpuValue *pValue, UINT32 Bits, BOOLEAN Sext) PURE;
    STDMETHOD (Load)(THIS_ IN ICpuValue *pAddr, UINT32 Bits, OUT ICpuValue **ppValue) PURE;
    STDMETHOD (Store)(THIS_ IN ICpuValue *pValue, IN ICpuValue *pAddr, UINT32 Bits) PURE;

    //
    // Arithmetic / logic / compares / casts / select.
    //
    STDMETHOD (BinaryOp)(THIS_ CPU_BINOP Op, IN ICpuValue *pA, IN ICpuValue *pB, OUT ICpuValue **ppValue) PURE;
    STDMETHOD (UnaryOp)(THIS_ CPU_UNOP Op, IN ICpuValue *pA, OUT ICpuValue **ppValue) PURE;
    STDMETHOD (Compare)(THIS_ CPU_CMP Pred, IN ICpuValue *pA, IN ICpuValue *pB, OUT ICpuValue **ppValue) PURE;
    STDMETHOD (Cast)(THIS_ CPU_CAST Op, IN ICpuValue *pA, UINT32 Bits, OUT ICpuValue **ppValue) PURE;
    STDMETHOD (Select)(THIS_ IN ICpuValue *pCond, IN ICpuValue *pTrue, IN ICpuValue *pFalse, OUT ICpuValue **ppValue) PURE;

    //
    // Architectural flags.
    //
    STDMETHOD (GetFlag)(THIS_ CPU_FLAG Flag, OUT ICpuValue **ppValue) PURE;
    STDMETHOD (SetFlag)(THIS_ CPU_FLAG Flag, IN ICpuValue *pValue) PURE;

    //
    // Control flow / basic blocks.
    //
    STDMETHOD (CreateBlock)(THIS_ CHAR8 CONST *pName, OUT ICpuBlock **ppBlock) PURE;
    STDMETHOD (SetInsertBlock)(THIS_ IN ICpuBlock *pBlock) PURE;
    STDMETHOD (GetInsertBlock)(THIS_ OUT ICpuBlock **ppBlock) PURE;
    STDMETHOD (Branch)(THIS_ IN ICpuBlock *pTarget) PURE;
    STDMETHOD (CondBranch)(THIS_ IN ICpuValue *pCond, IN ICpuBlock *pTrue, IN ICpuBlock *pFalse) PURE;

    //
    // Set the guest program counter to a constant (used by branch/call/ret lowering).
    //
    STDMETHOD (SetPC)(THIS_ CPU_ADDR Pc) PURE;
};

/**
  ICpuArchitecture -- a guest CPU frontend (replaces the old arch_func_t).

  Implemented by a frontend module (or, later, generated by UPCL). It knows how
  to classify, disassemble, and emit the semantics of one guest instruction,
  entirely in terms of ICpuEmitter -- never LLVM.
**/
DECLARE_INTERFACE_ (ICpuArchitecture, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (GetInfo)(THIS_ OUT CPU_ARCH_INFO *pInfo) PURE;
    //
    // Provide the guest code bytes the frontend decodes at translate time.
    //
    STDMETHOD (SetCodeMemory)(THIS_ IN UINT8 CONST *pBase, UINT64 Size) PURE;
    STDMETHOD (TagInstr)(THIS_ CPU_ADDR Pc, OUT UINT32 *pTag, OUT CPU_ADDR *pNewPc, OUT CPU_ADDR *pNextPc) PURE;
    STDMETHOD (Disassemble)(THIS_ CPU_ADDR Pc, OUT CHAR8 *pLine, UINT32 MaxLine) PURE;
    STDMETHOD (TranslateInstr)(THIS_ CPU_ADDR Pc, IN ICpuEmitter *pEmitter) PURE;
    STDMETHOD (TranslateCond)(THIS_ CPU_ADDR Pc, IN ICpuEmitter *pEmitter, OUT ICpuValue **ppCond) PURE;
};

/**
  ICpuCode -- a translated, runnable unit produced by a backend.
**/
DECLARE_INTERFACE_ (ICpuCode, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (CPU_EXEC_STATUS, Execute)(THIS_ IN VOID *pRAM, IN VOID *pGRF, IN VOID *pFRF) PURE;
};

/**
  ICpuBackend -- a code-generation backend (interpreter, llvm-22, ...).

  Creates an emitter to translate into, then turns the emitted unit into runnable
  ICpuCode. AOT-only backends instead expose ICpuBackendAOT (QueryInterface).
**/
DECLARE_INTERFACE_ (ICpuBackend, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD_ (CHAR8 CONST *, GetName)(THIS) PURE;
    STDMETHOD (CreateEmitter)(THIS_ IN ICpuArchitecture *pArch, OUT ICpuEmitter **ppEmitter) PURE;
    STDMETHOD (Compile)(THIS_ IN ICpuEmitter *pEmitter, OUT ICpuCode **ppCode) PURE;
};

/**
  ICpuSmcEmitter -- optional emitter capability for self-modifying code.

  An emitter that can guard against self-modifying code exposes this through
  QueryInterface (IID_ICpuSmcEmitter). The AOT driver, when building a CFG, calls
  EmitCodeGuard at each block's entry; the backend emits a check that -- when the
  watched code region (CPU_STATE.CodeStart/CodeEnd) has been written since the
  block was translated -- records this block's address in CPU_STATE.TrapPc and
  returns ExecSmc, so the host can re-translate from there. Backends that do not
  implement it simply do not return it from QueryInterface, and the AOT path then
  runs without SMC protection (correct only for non-self-modifying programs).
**/
DECLARE_INTERFACE_ (ICpuSmcEmitter, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (EmitCodeGuard)(THIS_ CPU_ADDR Pc) PURE;
};

//
// Interface identifiers.
//
// {1C9A0001-0001-4C50-9A00-000000000001}  base of the LibCPU interface family.
inline constexpr IID IID_ICpuValue =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 } };
inline constexpr IID IID_ICpuBlock =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x02 } };
inline constexpr IID IID_ICpuEmitter =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03 } };
inline constexpr IID IID_ICpuArchitecture =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04 } };
inline constexpr IID IID_ICpuCode =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x05 } };
inline constexpr IID IID_ICpuBackend =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x06 } };
inline constexpr IID IID_ICpuSmcEmitter =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x07 } };

} // namespace LibCPU

#endif // LIBCPU_ICPU_H
