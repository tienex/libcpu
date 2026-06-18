/** @file
  AsmJit backend (in-process JIT). See AsmjitBackend.h.

  Lowering model (mirrors the SLJIT backend, on EVERY host architecture): every
  ICpuValue is a slot in a stack-local temp frame at [sp, slot*8]. Each op loads
  its operand slots into scratch registers, computes, masks to the value's bit
  width, and stores the result into a fresh slot. Slots reset at every block entry
  -- an instruction's temps never outlive its block.

  We use the low-level Assembler (a64::Assembler / x86::Assembler), NOT the
  Compiler: physical registers + a hand-written prologue/epilogue, with NO register
  allocator. The Compiler's RA pass cannot reconcile an externally-built CFG
  carrying thousands of short-lived virtual registers (its build_liveness faults),
  so we manage registers ourselves -- exactly like SLJIT and GNU Lightning. AsmJit
  still resolves forward label references at serialization, so blocks need no
  deferred-jump patch list.

  One AjEmitter is compiled per host architecture (selected by ASMJIT_ARCH_*).
  The shared scaffolding (value/block/code handles, the backend object) is
  architecture-neutral; only the emitter's codegen differs:
    - AArch64: x19=RAM, x20=GRF, x9/x10/x11 scratch; manual stp/sub prologue.
    - x86 / x86-64: portable across SysV / Win64 / cdecl (Linux, macOS, Windows,
      UEFI) via AsmJit's FuncFrame + FuncArgsAssignment, which derive the calling
      convention from the runtime Environment. RAM/GRF latched in callee-saved
      registers (chosen word-neutrally so the same code serves IA-32 and x86-64).
**/
#include "AsmjitBackend.h"
#include "LibCPU/CpuState.h"

#include <cstdint>
#include <vector>

extern "C++" {
#include <asmjit/core.h>
#if ASMJIT_ARCH_ARM >= 64
#include <asmjit/a64.h>
#elif ASMJIT_ARCH_X86
#include <asmjit/x86.h>
#endif
}

namespace LibCPU {
namespace {

using namespace asmjit;

// Slots are reset at every block entry (an instruction's temps never outlive its block -- the
// frontend materialises all state to CPU_STATE at instruction boundaries), so this need only cover
// the widest single instruction, not a whole window.
static CONST UINT32 NUM_SLOTS  = 256;
static CONST INT32  FRAME_SIZE = (INT32) (NUM_SLOTS * 8);   // 16-byte aligned

static UINT64 Mask (UINT64 V, UINT32 Bits) { return (Bits >= 64) ? V : (V & (((UINT64) 1 << Bits) - 1)); }

// Latches any AsmJit encode error (e.g. an immediate that won't fit). a64::Assembler has no sticky
// error flag, so we observe failures through this handler and fail the whole window cleanly -- the
// host then re-translates on another tier rather than running miscompiled code.
struct AjErrHandler final : public ErrorHandler {
    bool Failed = false;
    void handle_error (Error, char CONST *, BaseEmitter *) override { Failed = true; }
};

class AjValue final : public ComObject<ICpuValue> {
public:
    AjValue (UINT32 Slot, UINT32 Bits) : m_Slot (Slot), m_Bits (Bits) {}
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuValue, ppvObject); }
    UINT32 m_Slot;
    UINT32 m_Bits;
};
class AjBlock final : public ComObject<ICpuBlock> {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuBlock, ppvObject); }
    Label m_Label;            // created at CreateBlock; asmjit resolves forward refs at serialization
    bool  m_Bound = false;    // set when SetInsertBlock binds it (else Build binds it to the exit)
};
static UINT32 SlotOf (ICpuValue *pV) { return static_cast<AjValue *> (pV)->m_Slot; }
static UINT32 BitsOf (ICpuValue *pV) { return static_cast<AjValue *> (pV)->m_Bits; }

typedef int (*JittedFn) (void *pRAM, void *pGRF, void *pFRF);

class AjCode final : public ComObject<ICpuCode> {
public:
    AjCode (JitRuntime *Rt, JittedFn Fn) : m_Rt (Rt), m_Fn (Fn) {}
    ~AjCode () override { if (m_Rt) { m_Rt->release (m_Fn); delete m_Rt; } }
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override { return DefaultQuery (riid, IID_ICpuCode, ppvObject); }
    CPU_EXEC_STATUS STDMETHODCALLTYPE Execute (VOID *pRAM, VOID *pGRF, VOID *pFRF) override {
        return (CPU_EXEC_STATUS) m_Fn (pRAM, pGRF, pFRF);
    }
private:
    JitRuntime *m_Rt;
    JittedFn    m_Fn;
};

#if ASMJIT_ARCH_ARM >= 64
// ===========================================================================
//  AArch64 emitter
// ===========================================================================
class AjEmitter final : public ComObject<ICpuEmitter>, public ICpuSmcEmitter,
                        public ICpuSystemEmitter, public ICpuSyscallEmitter, public ICpuClockEmitter {
public:
    AjEmitter () {
        m_Rt = new JitRuntime ();
        m_Code.init (m_Rt->environment (), m_Rt->cpu_features ());
        m_Code.set_error_handler (&m_Err);
        m_a = new a64::Assembler (&m_Code);
        m_Epilog = m_a->new_label ();
        // Prologue: save the callee-saved registers we clobber, carve the temp-slot frame, latch the
        // RAM/GRF argument pointers into x19/x20. sp stays 16-byte aligned (FRAME_SIZE is a multiple
        // of 16 and the pre-index is -16).
        m_a->stp (a64::x19, a64::x20, a64::ptr_pre (a64::sp, -16));
        m_a->sub (a64::sp, a64::sp, FRAME_SIZE);
        m_a->mov (m_Ram, a64::x0);
        m_a->mov (m_Grf, a64::x1);
    }
    ~AjEmitter () override {
        for (AjBlock *pB : m_Blocks) { pB->Release (); }   // m_Blocks holds a retained reference per block
        delete m_a;
        if (m_Rt) { delete m_Rt; }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_ICpuSmcEmitter))     { *ppvObject = static_cast<ICpuSmcEmitter *> (this);     AddRef (); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuSystemEmitter))  { *ppvObject = static_cast<ICpuSystemEmitter *> (this);  AddRef (); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuSyscallEmitter)) { *ppvObject = static_cast<ICpuSyscallEmitter *> (this); AddRef (); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuClockEmitter))   { *ppvObject = static_cast<ICpuClockEmitter *> (this);   AddRef (); return S_OK; }
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return ComObject<ICpuEmitter>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuEmitter>::Release (); }

    // ---- values -----------------------------------------------------------
    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        m_a->mov (a64::x9, Mask (Value, Bits));
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        m_a->ldr (a64::x9, a64::ptr (m_Grf, CPU_STATE_REG_OFFSET + Index * 8));
        MaskReg (a64::x9, Bits);
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 RegBits, BOOLEAN Sext) override {
        LoadReg (a64::x9, SlotOf (pValue));
        if (Sext) { SignExtend (a64::x9, BitsOf (pValue)); }
        MaskReg (a64::x9, RegBits);
        m_a->str (a64::x9, a64::ptr (m_Grf, CPU_STATE_REG_OFFSET + Index * 8));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        LoadReg (a64::x10, SlotOf (pAddr));
        a64::Mem m = a64::ptr (m_Ram, a64::x10);             // RAM + guest address (register-offset)
        switch (Bits) {
        case 8:  m_a->ldrb (a64::w9, m); break;
        case 16: m_a->ldrh (a64::w9, m); break;
        case 32: m_a->ldr  (a64::w9, m); break;
        default: m_a->ldr  (a64::x9, m); break;
        }
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        LoadReg (a64::x9, SlotOf (pValue));
        LoadReg (a64::x10, SlotOf (pAddr));
        a64::Mem m = a64::ptr (m_Ram, a64::x10);
        switch (Bits) {
        case 8:  m_a->strb (a64::w9, m); break;
        case 16: m_a->strh (a64::w9, m); break;
        case 32: m_a->str  (a64::w9, m); break;
        default: m_a->str  (a64::x9, m); break;
        }
        return S_OK;
    }

    // ---- arithmetic / compare / cast --------------------------------------
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        LoadReg (a64::x9, SlotOf (pA));
        LoadReg (a64::x10, SlotOf (pB));
        switch (Op) {
        case BinAdd:  m_a->add  (a64::x9, a64::x9, a64::x10); break;
        case BinSub:  m_a->sub  (a64::x9, a64::x9, a64::x10); break;
        case BinMul:  m_a->mul  (a64::x9, a64::x9, a64::x10); break;
        case BinAnd:  m_a->and_ (a64::x9, a64::x9, a64::x10); break;
        case BinOr:   m_a->orr  (a64::x9, a64::x9, a64::x10); break;
        case BinXor:  m_a->eor  (a64::x9, a64::x9, a64::x10); break;
        case BinShl:  m_a->lslv (a64::x9, a64::x9, a64::x10); break;
        case BinLShr: m_a->lsrv (a64::x9, a64::x9, a64::x10); break;
        case BinAShr: SignExtend (a64::x9, Bits); m_a->asrv (a64::x9, a64::x9, a64::x10); break;
        case BinRol:  m_a->neg  (a64::x11, a64::x10); m_a->rorv (a64::x9, a64::x9, a64::x11); break;  // rol n = ror (-n)
        case BinRor:  m_a->rorv (a64::x9, a64::x9, a64::x10); break;
        case BinUDiv: case BinSDiv: case BinURem: case BinSRem:
            EmitDivide (Op, Bits); break;
        default:      m_a->add  (a64::x9, a64::x9, a64::x10); break;
        }
        MaskReg (a64::x9, Bits);
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        LoadReg (a64::x9, SlotOf (pA));
        UINT32 OutBits = Bits;
        switch (Op) {
        case UnNeg: m_a->neg (a64::x9, a64::x9); break;
        case UnCom: m_a->mvn (a64::x9, a64::x9); break;
        case UnNot: m_a->cmp (a64::x9, 0); m_a->cset (a64::x9, arm::CondCode::kEQ); OutBits = 1; break;
        default:    return E_INVALIDARG;
        }
        MaskReg (a64::x9, OutBits);
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, OutBits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        LoadReg (a64::x9, SlotOf (pA));
        LoadReg (a64::x10, SlotOf (pB));
        arm::CondCode cc;
        bool Signed = false;
        switch (Pred) {
        case CmpEq:  cc = arm::CondCode::kEQ; break;
        case CmpNe:  cc = arm::CondCode::kNE; break;
        case CmpULt: cc = arm::CondCode::kLO; break;
        case CmpULe: cc = arm::CondCode::kLS; break;
        case CmpUGt: cc = arm::CondCode::kHI; break;
        case CmpUGe: cc = arm::CondCode::kHS; break;
        case CmpSLt: cc = arm::CondCode::kLT; Signed = true; break;
        case CmpSLe: cc = arm::CondCode::kLE; Signed = true; break;
        case CmpSGt: cc = arm::CondCode::kGT; Signed = true; break;
        case CmpSGe: cc = arm::CondCode::kGE; Signed = true; break;
        default:     cc = arm::CondCode::kEQ; break;
        }
        // Values are stored zero-extended to their width, so unsigned compares work directly; signed
        // compares need both operands sign-extended into the full 64-bit register first.
        if (Signed) { SignExtend (a64::x9, Bits); SignExtend (a64::x10, Bits); }
        m_a->cmp (a64::x9, a64::x10);
        m_a->cset (a64::x9, cc);
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        LoadReg (a64::x9, SlotOf (pA));
        switch (Op) {
        case CastTrunc: MaskReg (a64::x9, Bits); break;
        case CastZExt:  MaskReg (a64::x9, SrcBits); break;   // already masked; no-op
        case CastSExt:  SignExtend (a64::x9, SrcBits); MaskReg (a64::x9, Bits); break;
        }
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        LoadReg (a64::x9, SlotOf (pCond));
        LoadReg (a64::x10, SlotOf (pTrue));
        LoadReg (a64::x11, SlotOf (pFalse));
        m_a->cmp (a64::x9, 0);
        m_a->csel (a64::x9, a64::x10, a64::x11, arm::CondCode::kNE);   // cond != 0 ? true : false
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, BitsOf (pTrue), ppValue);
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        m_a->ldrb (a64::w9, a64::ptr (m_Grf, CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        m_a->and_ (a64::x9, a64::x9, 1);
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        LoadReg (a64::x9, SlotOf (pValue));
        m_a->and_ (a64::x9, a64::x9, 1);
        m_a->strb (a64::w9, a64::ptr (m_Grf, CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        return S_OK;
    }

    // ---- control flow -----------------------------------------------------
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        m_a->mov (a64::x9, (UINT64) Pc);
        m_a->str (a64::x9, a64::ptr (m_Grf, CPU_STATE_PC_OFFSET));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        AjBlock *pB = new AjBlock ();            // refcount 1: the reference returned to the caller
        pB->m_Label = m_a->new_label ();         // asmjit resolves forward references, so no patching list
        pB->AddRef ();                           // refcount 2: m_Blocks retains its own reference for the
        m_Blocks.push_back (pB);                 // exit pass in Build -- the caller (frontend/generator)
        *ppBlock = pB;                           // owns blocks via ComPtr and may release them before Build
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        AjBlock *pB = static_cast<AjBlock *> (pBlock);
        m_a->bind (pB->m_Label);                 // this block begins at the current position
        pB->m_Bound = true;
        m_Cur      = pB;
        m_NextSlot = 0;                          // an instruction's temps don't outlive its block
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override {
        if (m_Cur != nullptr) { m_Cur->AddRef (); }
        *ppBlock = m_Cur;
        return m_Cur != nullptr ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        m_a->b (static_cast<AjBlock *> (pTarget)->m_Label);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        LoadReg (a64::x9, SlotOf (pCond));
        m_a->cbnz (a64::x9, static_cast<AjBlock *> (pTrue)->m_Label);   // cond != 0 -> true
        m_a->b (static_cast<AjBlock *> (pFalse)->m_Label);
        return S_OK;
    }

    // ---- self-modifying-code / dispatch (ICpuSmcEmitter) ------------------
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR Pc) override {
        UINT32 Page = (UINT32) ((Pc >> CPU_SMC_PAGE_SHIFT) & (CPU_SMC_PAGE_COUNT - 1));
        m_a->ldrb (a64::w9, a64::ptr (m_Grf, CPU_STATE_CODEDIRTY_OFFSET + (Page >> 3)));
        m_a->and_ (a64::x9, a64::x9, (UINT64) (1u << (Page & 7)));
        Label Skip = m_a->new_label ();
        m_a->cbz (a64::x9, Skip);                            // clean -> skip the trap
        m_a->mov (a64::x9, (UINT64) Pc);
        m_a->str (a64::x9, a64::ptr (m_Grf, CPU_STATE_TRAPPC_OFFSET));
        TrapRet ();
        m_a->bind (Skip);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override {
        LoadReg (a64::x9, SlotOf (pTargetPc));
        m_a->str (a64::x9, a64::ptr (m_Grf, CPU_STATE_TRAPPC_OFFSET));
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        LoadReg (a64::x9, SlotOf (pTargetPc));
        m_a->str (a64::x9, a64::ptr (m_Grf, CPU_STATE_DISPPC_OFFSET));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        m_a->ldr (a64::x9, a64::ptr (m_Grf, CPU_STATE_DISPPC_OFFSET));
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, 64, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetCodeBase (ICpuValue **ppValue) override {
        m_a->ldr (a64::x9, a64::ptr (m_Grf, CPU_STATE_CODEBASE_OFFSET));
        UINT32 S = NewSlot ();
        StoreReg (S, a64::x9);
        return Make (S, 64, ppValue);
    }

    // ---- port I/O + system traps (ICpuSystemEmitter / ICpuSyscallEmitter) -
    HRESULT STDMETHODCALLTYPE EmitPortOut (ICpuValue *pPort, ICpuValue *pData, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (UINT64) CPU_IO_MAKE (CPU_IO_OUT, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, SlotOf (pPort));
        StoreField (CPU_STATE_IODATA_OFFSET, SlotOf (pData));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitPortIn (ICpuValue *pPort, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (UINT64) CPU_IO_MAKE (CPU_IO_IN, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, SlotOf (pPort));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrap (UINT32 Reason, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (UINT64) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrapValue (UINT32 Reason, ICpuValue *pValue, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (UINT64) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_IODATA_OFFSET, SlotOf (pValue));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSyscall (UINT32 Vector, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_SYSCALL_OFFSET, (UINT64) Vector);
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }

    // ---- time base (ICpuClockEmitter): Cycles += Count, once per guest instruction --------
    HRESULT STDMETHODCALLTYPE EmitTick (UINT32 Count) override {
        m_a->ldr (a64::x9, a64::ptr (m_Grf, CPU_STATE_CYCLES_OFFSET));
        m_a->add (a64::x9, a64::x9, Count);
        m_a->str (a64::x9, a64::ptr (m_Grf, CPU_STATE_CYCLES_OFFSET));
        return S_OK;
    }

    ICpuCode *Build () {
        // Any block created but never inserted (the CFG's exit/fallback target) is bound here; control
        // falls into the shared "return ExecOk" tail and the single epilog. Real blocks always end with
        // a branch or a trap-return, so they never fall through into this section.
        for (AjBlock *pB : m_Blocks) {
            if (!pB->m_Bound) { m_a->bind (pB->m_Label); pB->m_Bound = true; }
        }
        m_a->mov (a64::w0, ExecOk);
        m_a->bind (m_Epilog);                    // trap/exit paths branch here with w0 already set
        m_a->add (a64::sp, a64::sp, FRAME_SIZE);
        m_a->ldp (a64::x19, a64::x20, a64::ptr_post (a64::sp, 16));
        m_a->ret (a64::x30);
        if (m_Err.Failed) { return nullptr; }                // an op failed to encode -> host re-tries
        JittedFn Fn = nullptr;
        if (m_Rt->add (&Fn, &m_Code) != Error::kOk) { return nullptr; }
        JitRuntime *Rt = m_Rt;
        m_Rt = nullptr;   // ownership transferred to AjCode
        return new AjCode (Rt, Fn);
    }

private:
    a64::Mem Slot (UINT32 S) { return a64::ptr (a64::sp, (INT32) (S * 8)); }
    void LoadReg (a64::Gp Reg, UINT32 S)  { m_a->ldr (Reg, Slot (S)); }
    void StoreReg (UINT32 S, a64::Gp Reg) { m_a->str (Reg, Slot (S)); }
    UINT32 NewSlot () { return m_NextSlot++; }
    void MaskReg (a64::Gp Reg, UINT32 Bits) { if (Bits < 64) { m_a->and_ (Reg, Reg, Mask (~UINT64_C (0), Bits)); } }
    void SignExtend (a64::Gp Reg, UINT32 Bits) {
        if (Bits >= 64) { return; }
        if (Bits == 8)       { m_a->sxtb (Reg, Reg.w ()); }
        else if (Bits == 16) { m_a->sxth (Reg, Reg.w ()); }
        else if (Bits == 32) { m_a->sxtw (Reg, Reg.w ()); }
        else { m_a->lsl (Reg, Reg, 64 - Bits); m_a->asr (Reg, Reg, 64 - Bits); }
    }
    HRESULT Make (UINT32 Slot, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new AjValue (Slot, Bits); return S_OK; }
    // x9 = x9 (op) x10 for divide/remainder. A zero divisor would fault on the host, so guard it to
    // yield 0 (matching the interpreter; a real CPU raises #DE, which the frontend models separately).
    void EmitDivide (CPU_BINOP Op, UINT32 Bits) {
        bool Signed = (Op == BinSDiv || Op == BinSRem);
        bool Rem    = (Op == BinURem || Op == BinSRem);
        if (Signed) { SignExtend (a64::x9, Bits); SignExtend (a64::x10, Bits); }
        Label NonZero = m_a->new_label ();
        Label Done    = m_a->new_label ();
        m_a->cbnz (a64::x10, NonZero);
        m_a->mov (a64::x9, 0);                            // divisor 0 -> 0
        m_a->b (Done);
        m_a->bind (NonZero);
        if (Signed) { m_a->sdiv (a64::x11, a64::x9, a64::x10); } else { m_a->udiv (a64::x11, a64::x9, a64::x10); }
        if (Rem) { m_a->msub (a64::x9, a64::x11, a64::x10, a64::x9); }   // x9 = x9 - quotient*divisor
        else     { m_a->mov  (a64::x9, a64::x11); }
        m_a->bind (Done);
    }
    void StoreField (UINT32 Off, UINT32 S) {                 // CPU_STATE[Off] = slot value (full word)
        LoadReg (a64::x9, S);
        m_a->str (a64::x9, a64::ptr (m_Grf, Off));
    }
    void StoreFieldImm (UINT32 Off, UINT64 Imm) {
        m_a->mov (a64::x9, Imm);
        m_a->str (a64::x9, a64::ptr (m_Grf, Off));
    }
    void TrapRet () {                                        // hand control back to the host machine loop
        m_a->mov (a64::w0, ExecSmc);
        m_a->b (m_Epilog);
    }

    JitRuntime    *m_Rt;
    CodeHolder     m_Code;
    AjErrHandler   m_Err;
    a64::Assembler *m_a;
    a64::Gp        m_Ram = a64::x19;   // RAM base (callee-saved, latched in the prologue)
    a64::Gp        m_Grf = a64::x20;   // register-file base
    Label          m_Epilog;          // the function's single ret point
    UINT32         m_NextSlot = 0;
    AjBlock       *m_Cur = nullptr;
    std::vector<AjBlock *> m_Blocks;
};

#elif ASMJIT_ARCH_X86
// ===========================================================================
//  x86 / x86-64 emitter
//
//  Portable across the SysV (Linux/macOS), Microsoft x64 (Windows, UEFI x64) and
//  cdecl (IA-32 Windows/Linux/macOS, UEFI IA-32) ABIs: AsmJit's FuncFrame +
//  FuncArgsAssignment derive the calling convention from the runtime Environment,
//  emit the matching prologue/epilogue, save exactly the callee-saved registers we
//  touch, and shuffle the incoming argument registers into ours. Registers are
//  chosen word-neutrally (zax/zcx/.. = eXX on IA-32, rXX on x86-64) so one body
//  serves both. Slots and CPU_STATE fields are 8 bytes; on IA-32 (where the word
//  is 4 bytes) every store also clears the high dword, keeping 64-bit fields sane
//  for the values the current frontends produce (V20 <=20-bit, 6502/CHIP-8 less).
// ===========================================================================
class AjEmitter final : public ComObject<ICpuEmitter>, public ICpuSmcEmitter,
                        public ICpuSystemEmitter, public ICpuSyscallEmitter, public ICpuClockEmitter {
public:
    AjEmitter () {
        m_Rt = new JitRuntime ();
        m_Code.init (m_Rt->environment (), m_Rt->cpu_features ());
        m_Code.set_error_handler (&m_Err);
        m_a   = new x86::Assembler (&m_Code);
        m_R0  = m_a->zax ();   // scratch / divide dividend (RDX:RAX) / return value
        m_R1  = m_a->zcx ();   // scratch / shift count (CL) / divisor
        m_R2  = m_a->zdx ();   // scratch / divide high half
        m_Ram = m_a->zbx ();   // RAM base       (callee-saved on every supported ABI)
        m_Grf = m_a->zdi ();   // register-file base
        m_Epilog = m_a->new_label ();

        // Build the ABI-correct frame for the host environment, then emit the prologue and move the
        // incoming argument registers into m_Ram / m_Grf (arg2 = FRF is unused by the frontends).
        FuncDetail Fd;
        Fd.init (FuncSignature::build<int, void *, void *, void *> (), m_Code.environment ());
        m_Frame.init (Fd);
        m_Frame.set_local_stack_size (NUM_SLOTS * 8);
        m_Frame.set_local_stack_alignment (16);
        m_Frame.add_dirty_regs (m_R0, m_R1, m_R2, m_Ram, m_Grf);
        FuncArgsAssignment FArgs (&Fd);
        FArgs.assign_all (m_Ram, m_Grf);
        FArgs.update_func_frame (m_Frame);
        m_Frame.finalize ();
        m_a->emit_prolog (m_Frame);
        m_a->emit_args_assignment (m_Frame, FArgs);
        m_LocalOff = (INT32) m_Frame.local_stack_offset ();
    }
    ~AjEmitter () override {
        for (AjBlock *pB : m_Blocks) { pB->Release (); }   // m_Blocks holds a retained reference per block
        delete m_a;
        if (m_Rt) { delete m_Rt; }
    }

    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_ICpuSmcEmitter))     { *ppvObject = static_cast<ICpuSmcEmitter *> (this);     AddRef (); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuSystemEmitter))  { *ppvObject = static_cast<ICpuSystemEmitter *> (this);  AddRef (); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuSyscallEmitter)) { *ppvObject = static_cast<ICpuSyscallEmitter *> (this); AddRef (); return S_OK; }
        if (CompareGuid (&riid, &IID_ICpuClockEmitter))   { *ppvObject = static_cast<ICpuClockEmitter *> (this);   AddRef (); return S_OK; }
        return DefaultQuery (riid, IID_ICpuEmitter, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return ComObject<ICpuEmitter>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuEmitter>::Release (); }

    // ---- values -----------------------------------------------------------
    HRESULT STDMETHODCALLTYPE ConstInt (UINT32 Bits, UINT64 Value, ICpuValue **ppValue) override {
        m_a->mov (m_R0, (unsigned long long) Mask (Value, Bits));
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetRegister (UINT32 Index, UINT32 Bits, ICpuValue **ppValue) override {
        LoadField (m_R0, CPU_STATE_REG_OFFSET + Index * 8);
        MaskReg (m_R0, Bits);
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE PutRegister (UINT32 Index, ICpuValue *pValue, UINT32 RegBits, BOOLEAN Sext) override {
        LoadReg (m_R0, SlotOf (pValue));
        if (Sext) { SignExtend (m_R0, BitsOf (pValue)); }
        MaskReg (m_R0, RegBits);
        StoreWordAt (m_Grf, CPU_STATE_REG_OFFSET + Index * 8, m_R0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE Load (ICpuValue *pAddr, UINT32 Bits, ICpuValue **ppValue) override {
        LoadReg (m_R1, SlotOf (pAddr));                      // R1 = guest address
        switch (Bits) {
        case 8:  m_a->movzx (m_R0.r32 (), x86::byte_ptr (m_Ram, m_R1)); break;
        case 16: m_a->movzx (m_R0.r32 (), x86::word_ptr (m_Ram, m_R1)); break;
        case 32: m_a->mov   (m_R0.r32 (), x86::dword_ptr (m_Ram, m_R1)); break;
        default: m_a->mov   (m_R0, x86::ptr (m_Ram, m_R1)); break;
        }
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Store (ICpuValue *pValue, ICpuValue *pAddr, UINT32 Bits) override {
        LoadReg (m_R0, SlotOf (pValue));
        LoadReg (m_R1, SlotOf (pAddr));
        switch (Bits) {
        case 8:  m_a->mov (x86::byte_ptr (m_Ram, m_R1),  m_R0.r8 ());  break;
        case 16: m_a->mov (x86::word_ptr (m_Ram, m_R1),  m_R0.r16 ()); break;
        case 32: m_a->mov (x86::dword_ptr (m_Ram, m_R1), m_R0.r32 ()); break;
        default: m_a->mov (x86::ptr (m_Ram, m_R1),       m_R0);        break;
        }
        return S_OK;
    }

    // ---- arithmetic / compare / cast --------------------------------------
    HRESULT STDMETHODCALLTYPE BinaryOp (CPU_BINOP Op, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        LoadReg (m_R0, SlotOf (pA));
        LoadReg (m_R1, SlotOf (pB));                         // R1 = zcx, so its low byte (CL) is the shift count
        switch (Op) {
        case BinAdd:  m_a->add  (m_R0, m_R1); break;
        case BinSub:  m_a->sub  (m_R0, m_R1); break;
        case BinMul:  m_a->imul (m_R0, m_R1); break;
        case BinAnd:  m_a->and_ (m_R0, m_R1); break;
        case BinOr:   m_a->or_  (m_R0, m_R1); break;
        case BinXor:  m_a->xor_ (m_R0, m_R1); break;
        case BinShl:  m_a->shl  (m_R0, m_R1.r8 ()); break;
        case BinLShr: m_a->shr  (m_R0, m_R1.r8 ()); break;
        case BinAShr: SignExtend (m_R0, Bits); m_a->sar (m_R0, m_R1.r8 ()); break;
        case BinRol:  m_a->rol  (m_R0, m_R1.r8 ()); break;
        case BinRor:  m_a->ror  (m_R0, m_R1.r8 ()); break;
        case BinUDiv: case BinSDiv: case BinURem: case BinSRem:
            EmitDivide (Op, Bits); break;
        default:      m_a->add  (m_R0, m_R1); break;
        }
        MaskReg (m_R0, Bits);
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE UnaryOp (CPU_UNOP Op, ICpuValue *pA, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        LoadReg (m_R0, SlotOf (pA));
        UINT32 OutBits = Bits;
        switch (Op) {
        case UnNeg: m_a->neg (m_R0); break;
        case UnCom: m_a->not_ (m_R0); break;
        case UnNot: m_a->test (m_R0, m_R0); m_a->sete (m_R0.r8 ()); m_a->movzx (m_R0.r32 (), m_R0.r8 ()); OutBits = 1; break;
        default:    return E_INVALIDARG;
        }
        MaskReg (m_R0, OutBits);
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, OutBits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Compare (CPU_CMP Pred, ICpuValue *pA, ICpuValue *pB, ICpuValue **ppValue) override {
        UINT32 Bits = BitsOf (pA);
        LoadReg (m_R0, SlotOf (pA));
        LoadReg (m_R1, SlotOf (pB));
        bool Signed = (Pred == CmpSLt || Pred == CmpSLe || Pred == CmpSGt || Pred == CmpSGe);
        if (Signed) { SignExtend (m_R0, Bits); SignExtend (m_R1, Bits); }
        m_a->cmp (m_R0, m_R1);
        x86::Gp b = m_R0.r8 ();
        switch (Pred) {
        case CmpEq:  m_a->sete  (b); break;
        case CmpNe:  m_a->setne (b); break;
        case CmpULt: m_a->setb  (b); break;
        case CmpULe: m_a->setbe (b); break;
        case CmpUGt: m_a->seta  (b); break;
        case CmpUGe: m_a->setae (b); break;
        case CmpSLt: m_a->setl  (b); break;
        case CmpSLe: m_a->setle (b); break;
        case CmpSGt: m_a->setg  (b); break;
        case CmpSGe: m_a->setge (b); break;
        default:     m_a->sete  (b); break;
        }
        m_a->movzx (m_R0.r32 (), b);
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Cast (CPU_CAST Op, ICpuValue *pA, UINT32 Bits, ICpuValue **ppValue) override {
        UINT32 SrcBits = BitsOf (pA);
        LoadReg (m_R0, SlotOf (pA));
        switch (Op) {
        case CastTrunc: MaskReg (m_R0, Bits); break;
        case CastZExt:  MaskReg (m_R0, SrcBits); break;       // already masked; no-op
        case CastSExt:  SignExtend (m_R0, SrcBits); MaskReg (m_R0, Bits); break;
        }
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, Bits, ppValue);
    }
    HRESULT STDMETHODCALLTYPE Select (ICpuValue *pCond, ICpuValue *pTrue, ICpuValue *pFalse, ICpuValue **ppValue) override {
        LoadReg (m_R0, SlotOf (pFalse));                     // default
        LoadReg (m_R1, SlotOf (pTrue));
        LoadReg (m_R2, SlotOf (pCond));
        m_a->test (m_R2, m_R2);
        m_a->cmovne (m_R0, m_R1);                            // cond != 0 ? true : false
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, BitsOf (pTrue), ppValue);
    }

    // ---- flags ------------------------------------------------------------
    HRESULT STDMETHODCALLTYPE GetFlag (CPU_FLAG Flag, ICpuValue **ppValue) override {
        m_a->movzx (m_R0.r32 (), x86::byte_ptr (m_Grf, CPU_STATE_FLAG_OFFSET + (UINT32) Flag));
        m_a->and_ (m_R0, 1);
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, 1, ppValue);
    }
    HRESULT STDMETHODCALLTYPE SetFlag (CPU_FLAG Flag, ICpuValue *pValue) override {
        LoadReg (m_R0, SlotOf (pValue));
        m_a->and_ (m_R0, 1);
        m_a->mov (x86::byte_ptr (m_Grf, CPU_STATE_FLAG_OFFSET + (UINT32) Flag), m_R0.r8 ());
        return S_OK;
    }

    // ---- control flow -----------------------------------------------------
    HRESULT STDMETHODCALLTYPE SetPC (CPU_ADDR Pc) override {
        m_a->mov (m_R0, (unsigned long long) Pc);
        StoreWordAt (m_Grf, CPU_STATE_PC_OFFSET, m_R0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CreateBlock (CHAR8 CONST *, ICpuBlock **ppBlock) override {
        AjBlock *pB = new AjBlock ();            // refcount 1: the reference returned to the caller
        pB->m_Label = m_a->new_label ();         // asmjit resolves forward references, so no patching list
        pB->AddRef ();                           // refcount 2: m_Blocks retains its own reference (the caller
        m_Blocks.push_back (pB);                 // owns blocks via ComPtr and may release them before Build --
        *ppBlock = pB;                           // e.g. a REP prefix's helper blocks)
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetInsertBlock (ICpuBlock *pBlock) override {
        AjBlock *pB = static_cast<AjBlock *> (pBlock);
        m_a->bind (pB->m_Label);
        pB->m_Bound = true;
        m_Cur      = pB;
        m_NextSlot = 0;                          // an instruction's temps don't outlive its block
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetInsertBlock (ICpuBlock **ppBlock) override {
        if (m_Cur != nullptr) { m_Cur->AddRef (); }
        *ppBlock = m_Cur;
        return m_Cur != nullptr ? S_OK : E_FAIL;
    }
    HRESULT STDMETHODCALLTYPE Branch (ICpuBlock *pTarget) override {
        m_a->jmp (static_cast<AjBlock *> (pTarget)->m_Label);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE CondBranch (ICpuValue *pCond, ICpuBlock *pTrue, ICpuBlock *pFalse) override {
        LoadReg (m_R0, SlotOf (pCond));
        m_a->test (m_R0, m_R0);
        m_a->jnz (static_cast<AjBlock *> (pTrue)->m_Label);   // cond != 0 -> true
        m_a->jmp (static_cast<AjBlock *> (pFalse)->m_Label);
        return S_OK;
    }

    // ---- self-modifying-code / dispatch (ICpuSmcEmitter) ------------------
    HRESULT STDMETHODCALLTYPE EmitCodeGuard (CPU_ADDR Pc) override {
        UINT32 Page = (UINT32) ((Pc >> CPU_SMC_PAGE_SHIFT) & (CPU_SMC_PAGE_COUNT - 1));
        m_a->movzx (m_R0.r32 (), x86::byte_ptr (m_Grf, CPU_STATE_CODEDIRTY_OFFSET + (Page >> 3)));
        m_a->and_ (m_R0, (UINT64) (1u << (Page & 7)));
        Label Skip = m_a->new_label ();
        m_a->test (m_R0, m_R0);
        m_a->jz (Skip);                                      // clean -> skip the trap
        m_a->mov (m_R0, (unsigned long long) Pc);
        StoreWordAt (m_Grf, CPU_STATE_TRAPPC_OFFSET, m_R0);
        TrapRet ();
        m_a->bind (Skip);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE IndirectBranch (ICpuValue *pTargetPc) override {
        LoadReg (m_R0, SlotOf (pTargetPc));
        StoreWordAt (m_Grf, CPU_STATE_TRAPPC_OFFSET, m_R0);
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetDispatchTarget (ICpuValue *pTargetPc) override {
        LoadReg (m_R0, SlotOf (pTargetPc));
        StoreWordAt (m_Grf, CPU_STATE_DISPPC_OFFSET, m_R0);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetDispatchTarget (ICpuValue **ppValue) override {
        LoadField (m_R0, CPU_STATE_DISPPC_OFFSET);
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, 64, ppValue);
    }
    HRESULT STDMETHODCALLTYPE GetCodeBase (ICpuValue **ppValue) override {
        LoadField (m_R0, CPU_STATE_CODEBASE_OFFSET);
        UINT32 S = NewSlot (); StoreReg (S, m_R0); return Make (S, 64, ppValue);
    }

    // ---- port I/O + system traps (ICpuSystemEmitter / ICpuSyscallEmitter) -
    HRESULT STDMETHODCALLTYPE EmitPortOut (ICpuValue *pPort, ICpuValue *pData, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (UINT64) CPU_IO_MAKE (CPU_IO_OUT, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, SlotOf (pPort));
        StoreField (CPU_STATE_IODATA_OFFSET, SlotOf (pData));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitPortIn (ICpuValue *pPort, UINT32 Width, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (UINT64) CPU_IO_MAKE (CPU_IO_IN, Width));
        StoreField (CPU_STATE_IOPORT_OFFSET, SlotOf (pPort));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrap (UINT32 Reason, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (UINT64) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSystemTrapValue (UINT32 Reason, ICpuValue *pValue, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_IOCTRL_OFFSET, (UINT64) CPU_IO_MAKE (Reason, 0));
        StoreField (CPU_STATE_IODATA_OFFSET, SlotOf (pValue));
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }
    HRESULT STDMETHODCALLTYPE EmitSyscall (UINT32 Vector, ICpuValue *pReturnPc) override {
        StoreFieldImm (CPU_STATE_SYSCALL_OFFSET, (UINT64) Vector);
        StoreField (CPU_STATE_TRAPPC_OFFSET, SlotOf (pReturnPc));
        TrapRet (); return S_OK;
    }

    // ---- time base (ICpuClockEmitter): Cycles += Count, once per guest instruction --------
    HRESULT STDMETHODCALLTYPE EmitTick (UINT32 Count) override {
        LoadField (m_R0, CPU_STATE_CYCLES_OFFSET);
        m_a->add (m_R0, Count);
        StoreWordAt (m_Grf, CPU_STATE_CYCLES_OFFSET, m_R0);
        return S_OK;
    }

    ICpuCode *Build () {
        // Any block created but never inserted (the CFG exit/fallback target) is bound here; control
        // falls into the shared "return ExecOk" tail. Real blocks always end with a branch or trap-return.
        for (AjBlock *pB : m_Blocks) {
            if (!pB->m_Bound) { m_a->bind (pB->m_Label); pB->m_Bound = true; }
        }
        m_a->mov (m_R0.r32 (), ExecOk);          // EAX = return value
        m_a->bind (m_Epilog);                    // trap/exit paths jump here with EAX already set
        m_a->emit_epilog (m_Frame);              // restores callee-saved regs, frees the frame, returns
        if (m_Err.Failed) { return nullptr; }    // an op failed to encode -> host re-tries on another tier
        JittedFn Fn = nullptr;
        if (m_Rt->add (&Fn, &m_Code) != Error::kOk) { return nullptr; }
        JitRuntime *Rt = m_Rt;
        m_Rt = nullptr;   // ownership transferred to AjCode
        return new AjCode (Rt, Fn);
    }

private:
    x86::Mem Slot (UINT32 S) { return x86::ptr (m_a->zsp (), m_LocalOff + (INT32) (S * 8)); }
    void LoadReg (const x86::Gp &Reg, UINT32 S)  { m_a->mov (Reg, Slot (S)); }
    void StoreReg (UINT32 S, const x86::Gp &Reg) { StoreWordAt (m_a->zsp (), m_LocalOff + (INT32) (S * 8), Reg); }
    void LoadField (const x86::Gp &Reg, INT32 Off) { m_a->mov (Reg, x86::ptr (m_Grf, Off)); }
    UINT32 NewSlot () { return m_NextSlot++; }
    // Write a word-sized value into an 8-byte slot or CPU_STATE field. On IA-32 the word is 4 bytes, so
    // clear the high dword too -- 64-bit fields must stay clean for whoever reads them next.
    void StoreWordAt (const x86::Gp &Base, INT32 Off, const x86::Gp &Val) {
        m_a->mov (x86::ptr (Base, Off), Val);
#if ASMJIT_ARCH_BITS == 32
        m_a->mov (x86::dword_ptr (Base, Off + 4), 0);
#endif
    }
    void MaskReg (const x86::Gp &Reg, UINT32 Bits) {
        if (Bits >= (UINT32) ASMJIT_ARCH_BITS) { return; }
        if (Bits == 8)       { m_a->movzx (Reg.r32 (), Reg.r8 ()); }
        else if (Bits == 16) { m_a->movzx (Reg.r32 (), Reg.r16 ()); }
        else if (Bits == 32) { m_a->mov (Reg.r32 (), Reg.r32 ()); }   // zeroes the upper 32 bits (x86-64)
        else                 { m_a->and_ (Reg, (UINT64) Mask (~UINT64_C (0), Bits)); }
    }
    void SignExtend (const x86::Gp &Reg, UINT32 Bits) {
        if (Bits >= (UINT32) ASMJIT_ARCH_BITS) { return; }
        if (Bits == 8)       { m_a->movsx (Reg, Reg.r8 ()); }
        else if (Bits == 16) { m_a->movsx (Reg, Reg.r16 ()); }
#if ASMJIT_ARCH_BITS == 64
        else if (Bits == 32) { m_a->movsxd (Reg, Reg.r32 ()); }
#endif
        else { m_a->shl (Reg, ASMJIT_ARCH_BITS - Bits); m_a->sar (Reg, ASMJIT_ARCH_BITS - Bits); }
    }
    HRESULT Make (UINT32 Slot, UINT32 Bits, ICpuValue **ppValue) { *ppValue = new AjValue (Slot, Bits); return S_OK; }
    // R0 = R0 (op) R1 for divide/remainder, using the x86 RDX:RAX / divisor form. A zero divisor would
    // fault (#DE) on the host, so guard it to yield 0 (matching the interpreter).
    void EmitDivide (CPU_BINOP Op, UINT32 Bits) {
        bool Signed = (Op == BinSDiv || Op == BinSRem);
        bool Rem    = (Op == BinURem || Op == BinSRem);
        if (Signed) { SignExtend (m_R0, Bits); SignExtend (m_R1, Bits); }
        Label NonZero = m_a->new_label ();
        Label Done    = m_a->new_label ();
        m_a->test (m_R1, m_R1);
        m_a->jnz (NonZero);
        m_a->xor_ (m_R0.r32 (), m_R0.r32 ());            // divisor 0 -> 0
        m_a->jmp (Done);
        m_a->bind (NonZero);
        if (Signed) {
#if ASMJIT_ARCH_BITS == 64
            m_a->cqo ();                                 // sign-extend RAX into RDX:RAX
#else
            m_a->cdq ();
#endif
            m_a->idiv (m_R1);
        } else {
            m_a->xor_ (m_R2.r32 (), m_R2.r32 ());         // RDX = 0
            m_a->div (m_R1);
        }
        if (Rem) { m_a->mov (m_R0, m_R2); }              // remainder is in RDX
        m_a->bind (Done);
    }
    void StoreField (UINT32 Off, UINT32 S) {                 // CPU_STATE[Off] = slot value (full word)
        LoadReg (m_R0, S);
        StoreWordAt (m_Grf, (INT32) Off, m_R0);
    }
    void StoreFieldImm (UINT32 Off, UINT64 Imm) {
        m_a->mov (m_R0, (unsigned long long) Imm);
        StoreWordAt (m_Grf, (INT32) Off, m_R0);
    }
    void TrapRet () {                                        // hand control back to the host machine loop
        m_a->mov (m_R0.r32 (), ExecSmc);
        m_a->jmp (m_Epilog);
    }

    JitRuntime    *m_Rt;
    CodeHolder     m_Code;
    AjErrHandler   m_Err;
    x86::Assembler *m_a;
    FuncFrame      m_Frame;
    x86::Gp        m_R0, m_R1, m_R2, m_Ram, m_Grf;
    Label          m_Epilog;          // the function's single ret point
    INT32          m_LocalOff = 0;    // byte offset from zsp to the local slot frame
    UINT32         m_NextSlot = 0;
    AjBlock       *m_Cur = nullptr;
    std::vector<AjBlock *> m_Blocks;
};

#else
#error "AsmJit backend: unsupported host architecture (need AArch64 or x86/x86-64)"
#endif

// Fingerprint of the host this backend emits code for: a hash of the running CPU's asmjit
// arch + sub-arch and, at the native level, its full enabled feature set. A baseline target
// folds in only arch + sub-arch (no feature bits), so its fingerprint is the same on every
// CPU of that architecture -- a baseline artifact runs anywhere of its arch, a native one is
// pinned to a matching feature set. Never returns 0 (which the cache reserves for "host-
// independent"). This is the host half of the on-disk cache key (ICpuBackendTarget).
static UINT64
AsmjitHostFingerprint (UINT32 FeatLevel)
{
    asmjit::CpuInfo CONST &Ci = asmjit::CpuInfo::host ();
    UINT64 Fp = UINT64_C (0xCBF29CE484222325);
    auto Mix = [&] (UINT64 V) { Fp = (Fp ^ V) * UINT64_C (0x00000100000001B3); };
    Mix ((UINT64) (UINT32) Ci.arch ());
    Mix ((UINT64) (UINT32) Ci.sub_arch ());
    if (FeatLevel != LC_FEAT_BASELINE) {
        asmjit::CpuFeatures::Data CONST &D = Ci.features ().data ();
        asmjit::Support::BitWord CONST *pBits = D.bits ();
        for (size_t I = 0; I < D.bit_word_count (); I++) { Mix ((UINT64) pBits[I]); }
    }
    return Fp ? Fp : UINT64_C (1);
}

class AsmjitBackend final : public ComObject<ICpuBackend>, public ICpuBackendTarget {
public:
    HRESULT STDMETHODCALLTYPE QueryInterface (REFIID riid, VOID **ppvObject) override {
        if (ppvObject == nullptr) { return E_POINTER; }
        if (CompareGuid (&riid, &IID_ICpuBackendTarget)) {
            *ppvObject = static_cast<ICpuBackendTarget *> (this); AddRef (); return S_OK;
        }
        return DefaultQuery (riid, IID_ICpuBackend, ppvObject);
    }
    UINT32 STDMETHODCALLTYPE AddRef () override { return ComObject<ICpuBackend>::AddRef (); }
    UINT32 STDMETHODCALLTYPE Release () override { return ComObject<ICpuBackend>::Release (); }
    CHAR8 CONST *STDMETHODCALLTYPE GetName () override { return "asmjit"; }
    HRESULT STDMETHODCALLTYPE CreateEmitter (ICpuArchitecture *, ICpuEmitter **ppEmitter) override { *ppEmitter = new AjEmitter (); return S_OK; }
    HRESULT STDMETHODCALLTYPE Compile (ICpuEmitter *pEmitter, ICpuCode **ppCode) override {
        ICpuCode *pCode = static_cast<AjEmitter *> (pEmitter)->Build ();
        *ppCode = pCode;
        return pCode ? S_OK : E_FAIL;
    }
    UINT64 STDMETHODCALLTYPE GetTargetFingerprint () override { return AsmjitHostFingerprint (m_FeatLevel); }
    HRESULT STDMETHODCALLTYPE SetTargetFeatures (UINT32 Level) override { m_FeatLevel = Level; return S_OK; }
private:
    UINT32 m_FeatLevel = LC_FEAT_NATIVE;   // asmjit emits for the native host by default
};

} // anonymous namespace

ICpuBackend *
CreateAsmjitBackend (VOID)
{
    return new AsmjitBackend ();
}

} // namespace LibCPU
