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
    FlagParity,
    FlagDirection,     // string-op direction (x86 DF): 0 = increment, 1 = decrement
    FlagAux            // auxiliary carry (x86 AF, bit 4): carry/borrow out of bit 3, for BCD
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

    // Indirect (computed) branch: store the runtime target in CPU_STATE.TrapPc and
    // terminate the block, so the host resume loop continues translation/execution
    // at that address. Used as the dispatcher's fallback when an indirect target is
    // not a known block. Reuses the same TrapPc + resume path as the SMC guard.
    STDMETHOD (IndirectBranch)(THIS_ IN ICpuValue *pTargetPc) PURE;

    // In-artifact dispatch: an indirect branch stores its runtime target into the
    // CPU_STATE.DispPc scratch (SetDispatchTarget) and branches to the artifact's
    // dispatcher, which reads it back (GetDispatchTarget -- a 64-bit value) and routes
    // to the matching block, resolving the transfer with no host round-trip.
    STDMETHOD (SetDispatchTarget)(THIS_ IN ICpuValue *pTargetPc) PURE;
    STDMETHOD (GetDispatchTarget)(THIS_ OUT ICpuValue **ppValue) PURE;

    // Position-independent code base: reads CPU_STATE.CodeBase (a 64-bit value), the unit's actual load
    // address set by the host before Execute. The AOT driver materializes every guest code address as
    // GetCodeBase() + (target - Entry), so the compiled artifact is valid at any load address and can be
    // cached/persisted by code CONTENT rather than by address.
    STDMETHOD (GetCodeBase)(THIS_ OUT ICpuValue **ppValue) PURE;
};

/**
  ICpuProfileEmitter -- optional emitter capability for runtime edge profiling.

  An instrumented (profiling) AOT build plants EmitEdgeCounter at each CALL block, so
  CPU_STATE.EdgeCount[Index] is incremented every time that call site executes. The
  host reads the counters after running to obtain TRUE per-edge frequencies (a call in
  a loop counts per iteration; a conditional call counts only when taken). Discovered
  via QueryInterface (IID_ICpuProfileEmitter).
**/
DECLARE_INTERFACE_ (ICpuProfileEmitter, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Increment CPU_STATE.EdgeCount[Index] (a 64-bit counter) when this point runs.
    STDMETHOD (EmitEdgeCounter)(THIS_ UINT32 Index) PURE;
};

/**
  ICpuClockEmitter -- optional emitter capability for a backend-independent time base.

  The AOT/CFG driver calls EmitTick once per translated guest instruction, so CPU_STATE.Cycles
  counts INSTRUCTIONS RETIRED uniformly across every backend -- the interpreter and each JIT advance
  the same clock the same way. A system-emulation host feeds Cycles to time-driven peripherals (the
  8253 PIT), so a guest's timing (a BIOS calibration loop, a delay) behaves identically whether it
  runs interpreted or JIT-compiled. A backend that does not implement this leaves Cycles untouched and
  the host falls back to a coarse per-burst estimate. Discovered via QueryInterface (IID_ICpuClockEmitter).
**/
DECLARE_INTERFACE_ (ICpuClockEmitter, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Advance CPU_STATE.Cycles by Count (the AOT driver passes 1 per guest instruction).
    STDMETHOD (EmitTick)(THIS_ UINT32 Count) PURE;
};

/**
  ICpuSyscallEmitter -- optional emitter capability for guest system calls.

  A frontend's INT/SVC/trap instruction, instead of being a black-box trap, records
  its vector (e.g. a DOS interrupt number) in CPU_STATE.SyscallVector and traps to
  pReturnPc (the instruction after it). The host resume loop, finding a pending
  vector, hands it to a knowledge-library dispatcher, which reads the guest registers
  and memory, performs the equivalent native call, writes results back, and resumes.
  This is what lets an in-line guest system call become a native host call.
  Discovered via QueryInterface (IID_ICpuSyscallEmitter).
**/
DECLARE_INTERFACE_ (ICpuSyscallEmitter, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Record Vector in CPU_STATE.SyscallVector and trap to pReturnPc: set TrapPc to
    // the return address and terminate the block (same resume path as IndirectBranch).
    STDMETHOD (EmitSyscall)(THIS_ UINT32 Vector, IN ICpuValue *pReturnPc) PURE;
};

/**
  ICpuCodeListing -- optional ICpuCode capability: a human-readable disassembly of
  the TRANSLATED artifact (what the backend actually produced -- the interpreter's
  op list, a backend's IR, etc.), distinct from the guest disassembly the frontend
  gives. A debugger uses it to show "real" guest code beside its translation.
  Discovered via QueryInterface (IID_ICpuCodeListing) on an ICpuCode.
**/
DECLARE_INTERFACE_ (ICpuCodeListing, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Write a newline-separated listing of the translated unit into pBuf (NUL-
    // terminated, truncated to BufSize). *pNeeded, if non-null, receives the full
    // length (excluding the NUL) so the caller can resize and retry.
    STDMETHOD (GetListing)(THIS_ OUT CHAR8 *pBuf, UINT32 BufSize, OUT UINT32 *pNeeded) PURE;
};

/**
  ICpuCodeSerialize / ICpuBackendCache -- optional capabilities enabling a disk
  translation cache. A compiled artifact serialises itself to an opaque byte blob
  (ICpuCodeSerialize on the ICpuCode); the producing backend reconstructs a runnable
  ICpuCode from that blob (ICpuBackendCache on the ICpuBackend). The cache hashes the
  guest bytes + arch + backend to a key, stores the blob, and on a later hit reloads
  it instead of re-translating. A backend that implements neither is simply never
  cached. Discovered via QueryInterface (IID_ICpuCodeSerialize / IID_ICpuBackendCache).
**/
DECLARE_INTERFACE_ (ICpuCodeSerialize, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Serialise the artifact into pBuf (truncated to BufSize). *pNeeded, if non-null,
    // receives the full byte length so the caller can size the buffer and retry.
    STDMETHOD (Serialize)(THIS_ OUT UINT8 *pBuf, UINT32 BufSize, OUT UINT32 *pNeeded) PURE;
};

DECLARE_INTERFACE_ (ICpuBackendCache, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Reconstruct a runnable ICpuCode from a blob previously produced by this
    // backend's ICpuCodeSerialize. Returns E_FAIL if the blob is unrecognised.
    STDMETHOD (LoadCode)(THIS_ IN UINT8 CONST *pBytes, UINT32 Len, OUT ICpuCode **ppCode) PURE;
};

// LC_OPT_DEFAULT: leave the backend's own optimization setting untouched (used by the one-tier
// SetHotBackend shorthand so legacy behavior is preserved).
#define LC_OPT_DEFAULT  ((UINT32) ~0u)

//
// ICpuBackendOptimize -- optional capability: a backend that can vary its optimization effort. The
// tiered driver cranks it up at higher tiers (e.g. LLVM / GCC JIT O0..O3, MIR generator levels) to
// trade compile time for code quality. Level is backend-defined but MONOTONE (higher = more optimized,
// slower to compile); subsequent compiles by this backend use the level until it is set again. Backends
// that do not optimize simply do not implement this interface, and the driver leaves them as-is.
//
DECLARE_INTERFACE_ (ICpuBackendOptimize, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    STDMETHOD (SetOptimization)(THIS_ UINT32 Level) PURE;
};

// Feature levels for ICpuBackendTarget::SetTargetFeatures.
#define LC_FEAT_BASELINE  ((UINT32) 0)   // the architecture's portable baseline (maximum compatibility)
#define LC_FEAT_NATIVE    ((UINT32) 1)   // everything the running host CPU supports (maximum performance)

//
// ICpuBackendTarget -- optional capability: a backend that emits HOST-SPECIFIC code reports a
// fingerprint of the target it compiles for, and can be asked to target a baseline or the native
// host. The fingerprint is the host half of the on-disk cache key: a cached artifact is reused only
// on a host whose fingerprint matches (or, for a baseline artifact, supersets) the artifact's -- so a
// blob is never run on a host missing an instruction it uses. Backends whose artifacts are host-
// independent (an interpreter's bytecode) report 0 and run anywhere. Discovered via QueryInterface
// (IID_ICpuBackendTarget). Orthogonal to ICpuBackendOptimize: feature level and optimization level are
// independent axes the tiered driver can vary, recompiling in the background and hot-swapping.
//
DECLARE_INTERFACE_ (ICpuBackendTarget, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // A stable fingerprint of the host target this backend currently emits for (host arch + CPU model +
    // enabled feature set + the requested feature level). Two backends yield the same value iff an
    // artifact built by one is safe to run where the other emits. 0 means host-independent (run anywhere).
    STDMETHOD_ (UINT64, GetTargetFingerprint)(THIS) PURE;

    // Request a feature level (LC_FEAT_*) for subsequent compiles; changes the fingerprint accordingly.
    // A backend that only ever targets the native host may accept LC_FEAT_NATIVE and return E_NOTIMPL
    // for others. Returns S_OK on success.
    STDMETHOD (SetTargetFeatures)(THIS_ UINT32 Level) PURE;
};

/**
  ICpuSystemEmitter -- optional emitter capability for SYSTEM-level emulation.

  Where a user-level guest reaches the host through system calls (ICpuSyscallEmitter),
  a system-level guest reaches an emulated machine through the DEVICE BUS (port I/O)
  and privileged control instructions. Each such instruction records its kind in
  CPU_STATE.IoCtrl (+ IoPort/IoData) and traps to the host machine (System), which
  drives the addressed device or performs the control action and resumes -- the same
  trap/resume path as the far-jump and syscall emitters. Discovered via
  QueryInterface (IID_ICpuSystemEmitter).
**/
DECLARE_INTERFACE_ (ICpuSystemEmitter, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Port write: record IoCtrl=CPU_IO_OUT|width, IoPort=pPort, IoData=pData, and trap.
    STDMETHOD (EmitPortOut)(THIS_ IN ICpuValue *pPort, IN ICpuValue *pData, UINT32 Width, IN ICpuValue *pReturnPc) PURE;
    // Port read: record IoCtrl=CPU_IO_IN|width, IoPort=pPort, and trap; the host reads
    // the device and writes the value back into AL/AX before resuming.
    STDMETHOD (EmitPortIn)(THIS_ IN ICpuValue *pPort, UINT32 Width, IN ICpuValue *pReturnPc) PURE;
    // Privileged control (CPU_IO_IRET/HLT/STI/CLI): record IoCtrl=Reason and trap.
    STDMETHOD (EmitSystemTrap)(THIS_ UINT32 Reason, IN ICpuValue *pReturnPc) PURE;
    // Like EmitSystemTrap but also records a runtime value in IoData (e.g. BOUND's out-of-range
    // condition), so the host can act on it. Optional: backends may forward to EmitSystemTrap.
    STDMETHOD (EmitSystemTrapValue)(THIS_ UINT32 Reason, IN ICpuValue *pValue, IN ICpuValue *pReturnPc) PURE;
};

/**
  ICpuSegmentedCode -- optional ARCHITECTURE capability for segmented code fetch.

  A segmented guest (e.g. 8086/V20) fetches instructions from a code segment base plus
  an offset; the AOT driver works in offset (IP) space. After any control transfer that
  reloads the code segment -- far JMP/CALL/RETF, an interrupt, or IRET -- the host
  re-points the frontend at the new segment with SetCodeSegment before re-translating,
  and SegmentBase reports the segment's linear base so the host can bound the window.
  Frontends with flat addressing do not expose this. Discovered via QueryInterface
  (IID_ICpuSegmentedCode) on the ICpuArchitecture.
**/
DECLARE_INTERFACE_ (ICpuSegmentedCode, IUnknown)
{
    STDMETHOD (QueryInterface)(THIS_ REFIID riid, OUT VOID **ppvObject) PURE;
    STDMETHOD_ (UINT32, AddRef)(THIS) PURE;
    STDMETHOD_ (UINT32, Release)(THIS) PURE;

    // Re-point the decode base at code segment Selector; subsequent translation fetches
    // from SegmentBase(Selector) + offset.
    STDMETHOD (SetCodeSegment)(THIS_ UINT32 Selector) PURE;
    // The linear base address of the given code segment (Selector << 4 on the 8086).
    STDMETHOD_ (UINT64, SegmentBase)(THIS_ UINT32 Selector) PURE;

    // Position-independent translation. When Enabled, the frontend materializes code-address values it
    // bakes (a CALL/INT pushed return, a trap's resume PC) RELATIVE to the unit's Entry, reconstructing
    // them at run time as GetCodeBase() + (value - Entry) -- so the compiled unit is valid at any load
    // address and can be cached/persisted by code content. Disabled (the default) emits absolute values.
    // The AOT driver sets this around a unit when the host requested PIC; it is a no-op for flat archs.
    STDMETHOD (SetPicTranslation)(THIS_ CPU_ADDR Entry, BOOLEAN Enabled) PURE;
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
inline constexpr IID IID_ICpuProfileEmitter =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x08 } };
inline constexpr IID IID_ICpuSyscallEmitter =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x09 } };
inline constexpr IID IID_ICpuCodeListing =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0A } };
inline constexpr IID IID_ICpuCodeSerialize =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0B } };
inline constexpr IID IID_ICpuBackendCache =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C } };
inline constexpr IID IID_ICpuSystemEmitter =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0D } };
inline constexpr IID IID_ICpuSegmentedCode =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0E } };
inline constexpr IID IID_ICpuClockEmitter =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0F } };
inline constexpr IID IID_ICpuBackendOptimize =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x10 } };
inline constexpr IID IID_ICpuBackendTarget =
    { 0x1C9A0001, 0x0001, 0x4C50, { 0x9A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x11 } };

} // namespace LibCPU

#endif // LIBCPU_ICPU_H
